/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/ppc/ppc_frontend.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "xenia/base/atomic.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/mutex.h"
#include "xenia/base/xxhash.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_emit.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"
#include "xenia/cpu/ppc/ppc_translator.h"
#include "xenia/cpu/processor.h"

DEFINE_bool(
    cpu_lockfree_check_global_lock, true,
    "Thor CPU: make the guest mfmsr path (CheckGlobalLock) read the global-lock "
    "state lock-free via the atomic owner thread-id/count that "
    "EnterGlobalLock/LeaveGlobalLock already publish, instead of taking a full "
    "recursive_mutex lock+unlock just to read an int. mfmsr only needs to know "
    "whether THIS thread is in an interrupt-disabled critical section "
    "(owner==this thread && count!=0), which the atomics answer exactly; "
    "Enter/Leave still serialize the actual critical sections, so ordering is "
    "unchanged. Removes the mfmsr-driven pthread_mutex cas2/swp2/futex traffic "
    "that tops Blue Dragon's CPU-bound field profile. Device-validated on the "
    "BD heavy field (matched A/B): renders pixel-correct, CheckGlobalLock drops "
    "all mutex callees, core mutex cost 3.80%->2.87% (-0.93pp of the frame, "
    "isolated from Enter/Leave; larger on lock-heavy CPU-bound titles). "
    "Default-on.",
    "CPU");

namespace xe {
namespace cpu {
namespace ppc {

// ---------------------------------------------------------------------------
// Shared-function fast-path (cpu_shared_function_fastpath): recognize hot,
// byte-identical XDK runtime kernels that recur across titles and run a native
// host implementation instead of JIT-translating the guest loop. The recognizer
// + native handlers live here; the substitution hook is in PPCHIRBuilder::Emit.
// ---------------------------------------------------------------------------

uint64_t CanonicalFunctionHashRaw(const void* code, uint32_t size_bytes) {
  // Mask the relocation-prone LI field of b/bl (primary opcode 18) so the same
  // library function hashes identically across titles; hash everything else raw.
  const uint32_t word_count = size_bytes / 4;
  std::vector<uint32_t> canon(word_count);
  const uint8_t* p = reinterpret_cast<const uint8_t*>(code);
  for (uint32_t i = 0; i < word_count; ++i) {
    // Guest code is big-endian; normalize to a host value for decoding/hashing.
    uint32_t word = xe::load_and_swap<uint32_t>(p + i * 4);
    const uint32_t primary = (word >> 26) & 0x3F;
    if (primary == 18) {
      // I-form b/ba/bl/bla: keep opcode (top 6) + AA/LK (bottom 2), zero LI.
      word &= 0xFC000003u;
    }
    canon[i] = word;
  }
  return XXH3_64bits(canon.data(),
                     static_cast<size_t>(word_count) * sizeof(uint32_t));
}

uint64_t CanonicalFunctionHash(const Memory* memory, uint32_t start_address,
                               uint32_t end_address) {
  if (end_address < start_address) {
    return 0;
  }
  const uint32_t size_bytes = end_address - start_address + 4;
  const uint8_t* code =
      const_cast<Memory*>(memory)->TranslateVirtual<const uint8_t*>(
          start_address);
  return CanonicalFunctionHashRaw(code, size_bytes);
}

namespace {
struct SharedFunctionEntry {
  uint32_t size_bytes;
  SharedFunctionKind kind;
};
// hash -> {size, kind}. Curated; populated by RE-confirmed harvest. Empty by
// default, so the fast-path is INERT on real titles until verified entries are
// compiled in (then it becomes a stacking, default-off XeniaOptimizations win).
std::unordered_map<uint64_t, SharedFunctionEntry>& SharedFunctionTable() {
  static std::unordered_map<uint64_t, SharedFunctionEntry> table;
  return table;
}
std::mutex& SharedFunctionTableMutex() {
  static std::mutex m;
  return m;
}
}  // namespace

SharedFunctionKind LookupSharedFunction(uint64_t canonical_hash,
                                        uint32_t size_bytes) {
  std::lock_guard<std::mutex> lock(SharedFunctionTableMutex());
  auto& table = SharedFunctionTable();
  auto it = table.find(canonical_hash);
  if (it == table.end()) {
    return SharedFunctionKind::kNone;
  }
  // Size must also match: a defense against a hash collision selecting a
  // differently-sized function (the handler would otherwise run wrong bounds).
  if (it->second.size_bytes != size_bytes) {
    return SharedFunctionKind::kNone;
  }
  return it->second.kind;
}

void RegisterSharedFunctionForTesting(uint64_t canonical_hash,
                                      uint32_t size_bytes,
                                      SharedFunctionKind kind) {
  std::lock_guard<std::mutex> lock(SharedFunctionTableMutex());
  SharedFunctionTable()[canonical_hash] = {size_bytes, kind};
}

// Native handlers. PPC ABI: r3/r4/r5 = first three args; the result (dest) stays
// in r3. arg0 = Memory*. Operates byte-exactly on guest memory (raw bytes, no
// endianness concern), leaving volatile registers as-is (the caller treats them
// as clobbered anyway, so not updating them is ABI-safe).
// A bulk native write to guest memory BYPASSES the per-store store-watch the JIT
// store path fires (GPU resource + code-cache invalidation). Re-fire it once over
// the written range so a substituted memset/memcpy stays observationally correct
// for watched memory (GPU upload buffers, self-modifying/JIT'd code pages). No-op
// for unwatched ranges. Codex-flagged correctness gate; required before populating
// the table with ANY entry. (Currently inert: the table ships empty.)
static void TriggerStoreWatch(Memory* memory, uint32_t address, uint32_t length) {
  memory->TriggerPhysicalMemoryCallbacks(
      xe::global_critical_region::AcquireDirect(), address, length,
      /*is_write=*/true, /*unwatch_exact_range=*/false);
}

void SharedMemsetHandler(PPCContext* ppc_context, void* arg0, void* arg1) {
  auto memory = reinterpret_cast<Memory*>(arg0);
  uint32_t dest = static_cast<uint32_t>(ppc_context->r[3]);
  int value = static_cast<int>(static_cast<uint32_t>(ppc_context->r[4]));
  uint32_t count = static_cast<uint32_t>(ppc_context->r[5]);
  if (count) {
    std::memset(memory->TranslateVirtual<uint8_t*>(dest), value, count);
    TriggerStoreWatch(memory, dest, count);
  }
}

void SharedMemcpyHandler(PPCContext* ppc_context, void* arg0, void* arg1) {
  auto memory = reinterpret_cast<Memory*>(arg0);
  uint32_t dest = static_cast<uint32_t>(ppc_context->r[3]);
  uint32_t src = static_cast<uint32_t>(ppc_context->r[4]);
  uint32_t count = static_cast<uint32_t>(ppc_context->r[5]);
  if (count) {
    std::memcpy(memory->TranslateVirtual<uint8_t*>(dest),
                memory->TranslateVirtual<const uint8_t*>(src), count);
    TriggerStoreWatch(memory, dest, count);
  }
}

void SharedMemmoveHandler(PPCContext* ppc_context, void* arg0, void* arg1) {
  auto memory = reinterpret_cast<Memory*>(arg0);
  uint32_t dest = static_cast<uint32_t>(ppc_context->r[3]);
  uint32_t src = static_cast<uint32_t>(ppc_context->r[4]);
  uint32_t count = static_cast<uint32_t>(ppc_context->r[5]);
  if (count) {
    std::memmove(memory->TranslateVirtual<uint8_t*>(dest),
                 memory->TranslateVirtual<const uint8_t*>(src), count);
    TriggerStoreWatch(memory, dest, count);
  }
}

namespace {

std::atomic<int32_t> global_lock_owner_count{0};
std::atomic<uint32_t> global_lock_owner_thread_id{0};
std::atomic<uint32_t> global_lock_owner_lr{0};
std::atomic<uint32_t> global_lock_owner_ctr{0};
std::atomic<uint32_t> global_lock_owner_r1{0};
std::atomic<uint32_t> global_lock_owner_r3{0};
std::atomic<uint32_t> global_lock_owner_r4{0};

void StoreGlobalLockOwner(PPCContext* ppc_context, int32_t count) {
  global_lock_owner_lr.store(static_cast<uint32_t>(ppc_context->lr),
                             std::memory_order_relaxed);
  global_lock_owner_ctr.store(static_cast<uint32_t>(ppc_context->ctr),
                              std::memory_order_relaxed);
  global_lock_owner_r1.store(static_cast<uint32_t>(ppc_context->r[1]),
                             std::memory_order_relaxed);
  global_lock_owner_r3.store(static_cast<uint32_t>(ppc_context->r[3]),
                             std::memory_order_relaxed);
  global_lock_owner_r4.store(static_cast<uint32_t>(ppc_context->r[4]),
                             std::memory_order_relaxed);
  global_lock_owner_thread_id.store(ppc_context->thread_id,
                                    std::memory_order_relaxed);
  global_lock_owner_count.store(count, std::memory_order_release);
}

void ClearGlobalLockOwner() {
  global_lock_owner_count.store(0, std::memory_order_release);
  global_lock_owner_thread_id.store(0, std::memory_order_relaxed);
  global_lock_owner_lr.store(0, std::memory_order_relaxed);
  global_lock_owner_ctr.store(0, std::memory_order_relaxed);
  global_lock_owner_r1.store(0, std::memory_order_relaxed);
  global_lock_owner_r3.store(0, std::memory_order_relaxed);
  global_lock_owner_r4.store(0, std::memory_order_relaxed);
}

}  // namespace

void InitializeIfNeeded();
void CleanupOnShutdown();

void InitializeIfNeeded() {
  static bool has_initialized = false;
  if (has_initialized) {
    return;
  }
  has_initialized = true;

  RegisterEmitCategoryAltivec();
  RegisterEmitCategoryALU();
  RegisterEmitCategoryControl();
  RegisterEmitCategoryFPU();
  RegisterEmitCategoryMemory();

  atexit(CleanupOnShutdown);
}

void CleanupOnShutdown() {}

GlobalLockOwnerSnapshot QueryGlobalLockOwnerSnapshot() {
  GlobalLockOwnerSnapshot snapshot = {};
  snapshot.count = global_lock_owner_count.load(std::memory_order_acquire);
  snapshot.thread_id =
      global_lock_owner_thread_id.load(std::memory_order_relaxed);
  snapshot.lr = global_lock_owner_lr.load(std::memory_order_relaxed);
  snapshot.ctr = global_lock_owner_ctr.load(std::memory_order_relaxed);
  snapshot.r1 = global_lock_owner_r1.load(std::memory_order_relaxed);
  snapshot.r3 = global_lock_owner_r3.load(std::memory_order_relaxed);
  snapshot.r4 = global_lock_owner_r4.load(std::memory_order_relaxed);
  return snapshot;
}

PPCFrontend::PPCFrontend(Processor* processor) : processor_(processor) {
  InitializeIfNeeded();
}

PPCFrontend::~PPCFrontend() {
  // Force cleanup now before we deinit.
  translator_pool_.Reset();
}

Memory* PPCFrontend::memory() const { return processor_->memory(); }

// Checks the state of the global lock and sets scratch to the current MSR
// value.
void CheckGlobalLock(PPCContext* ppc_context, void* arg0, void* arg1) {
  if (cvars::cpu_lockfree_check_global_lock) {
    // Lock-free fast path: mfmsr only needs to know whether THIS thread is
    // currently inside a global-lock (interrupt-disabled) critical section.
    // EnterGlobalLock/LeaveGlobalLock publish the owner thread-id + recursion
    // count atomically (StoreGlobalLockOwner/ClearGlobalLockOwner clears both),
    // and only this thread can change its own ownership while it executes this
    // read, so the owner check is exact - no recursive_mutex round-trip needed.
    // The acquire load of count pairs with the release store in Store/Clear,
    // making the (relaxed-stored) thread-id visible.
    int32_t count = global_lock_owner_count.load(std::memory_order_acquire);
    uint32_t owner =
        global_lock_owner_thread_id.load(std::memory_order_acquire);
    bool in_critical_section = count != 0 && owner == ppc_context->thread_id;
    ppc_context->scratch = in_critical_section ? 0 : 0x8000;
    return;
  }
  auto global_mutex = reinterpret_cast<std::recursive_mutex*>(arg0);
  auto global_lock_count = reinterpret_cast<int32_t*>(arg1);
  std::lock_guard<std::recursive_mutex> lock(*global_mutex);
  ppc_context->scratch = *global_lock_count ? 0 : 0x8000;
}

// Enters the global lock. Safe to recursion.
void EnterGlobalLock(PPCContext* ppc_context, void* arg0, void* arg1) {
  auto global_mutex = reinterpret_cast<std::recursive_mutex*>(arg0);
  auto global_lock_count = reinterpret_cast<int32_t*>(arg1);
  global_mutex->lock();
  int32_t new_lock_count = xe::atomic_inc(global_lock_count);
  StoreGlobalLockOwner(ppc_context, new_lock_count);
}

// Leaves the global lock. Safe to recursion.
void LeaveGlobalLock(PPCContext* ppc_context, void* arg0, void* arg1) {
  auto global_mutex = reinterpret_cast<std::recursive_mutex*>(arg0);
  auto global_lock_count = reinterpret_cast<int32_t*>(arg1);
  auto new_lock_count = xe::atomic_dec(global_lock_count);
  assert_true(new_lock_count >= 0);
  if (new_lock_count == 0) {
    ClearGlobalLockOwner();
  } else {
    StoreGlobalLockOwner(ppc_context, new_lock_count);
  }
  global_mutex->unlock();
}

void SyscallHandler(PPCContext* ppc_context, void* arg0, void* arg1) {
  uint64_t syscall_number = ppc_context->r[0];
  switch (syscall_number) {
    default:
      assert_unhandled_case(syscall_number);
      XELOGE("Unhandled syscall {}!", syscall_number);
      break;
#pragma warning(suppress : 4065)
  }
}

bool PPCFrontend::Initialize() {
  void* arg0 = reinterpret_cast<void*>(&xe::global_critical_region::mutex());
  void* arg1 = reinterpret_cast<void*>(&builtins_.global_lock_count);
  builtins_.check_global_lock =
      processor_->DefineBuiltin("CheckGlobalLock", CheckGlobalLock, arg0, arg1);
  builtins_.enter_global_lock =
      processor_->DefineBuiltin("EnterGlobalLock", EnterGlobalLock, arg0, arg1);
  builtins_.leave_global_lock =
      processor_->DefineBuiltin("LeaveGlobalLock", LeaveGlobalLock, arg0, arg1);
  builtins_.syscall_handler = processor_->DefineBuiltin(
      "SyscallHandler", SyscallHandler, nullptr, nullptr);
  // Shared-function fast-path native kernels (arg0 = Memory* for guest<->host
  // address translation; arg1 unused).
  void* mem_arg = reinterpret_cast<void*>(processor_->memory());
  builtins_.shared_memset = processor_->DefineBuiltin(
      "SharedMemset", SharedMemsetHandler, mem_arg, nullptr);
  builtins_.shared_memcpy = processor_->DefineBuiltin(
      "SharedMemcpy", SharedMemcpyHandler, mem_arg, nullptr);
  builtins_.shared_memmove = processor_->DefineBuiltin(
      "SharedMemmove", SharedMemmoveHandler, mem_arg, nullptr);
  return true;
}

bool PPCFrontend::DeclareFunction(GuestFunction* function) {
  // Could scan or something here.
  // Could also check to see if it's a well-known function type and classify
  // for later.
  // Could also kick off a precompiler, since we know it's likely the function
  // will be demanded soon.
  return true;
}

bool PPCFrontend::DefineFunction(GuestFunction* function,
                                 uint32_t debug_info_flags) {
  auto translator = translator_pool_.Allocate(this);
  bool result = translator->Translate(function, debug_info_flags);
  translator_pool_.Release(translator);
  return result;
}

}  // namespace ppc
}  // namespace cpu
}  // namespace xe
