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

#include "xenia/base/atomic.h"
#include "xenia/base/logging.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_emit.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"
#include "xenia/cpu/ppc/ppc_translator.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {
namespace ppc {

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
