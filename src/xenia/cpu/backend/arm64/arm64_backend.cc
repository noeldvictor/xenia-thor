/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/arm64/arm64_backend.h"

#include <cstring>

#include "xenia/base/platform.h"

#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
#include <sys/mman.h>
#endif

#include "xbyak_aarch64/xbyak_aarch64.h"
#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif
#include "xenia/base/assert.h"
#include "xenia/base/exception_handler.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/backend/arm64/arm64_assembler.h"
#include "xenia/cpu/backend/arm64/arm64_code_cache.h"
#include "xenia/cpu/backend/arm64/arm64_function.h"
#include "xenia/cpu/backend/assembler.h"
#include "xenia/cpu/breakpoint.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {

uint64_t ResolveFunction(void* raw_context, uint64_t target_address);

namespace {

constexpr uint32_t kArm64Brk0 = 0xD4200000u;
constexpr size_t kArm64ThunkCodeSize = 4096;
constexpr size_t kGuestTrampolineSize = 68;

void EncodeMovImm64(uint32_t* out, uint32_t reg, uint64_t imm) {
  out[0] = 0xD2800000 | (static_cast<uint32_t>(imm & 0xFFFF) << 5) | reg;
  out[1] =
      0xF2A00000 | (static_cast<uint32_t>((imm >> 16) & 0xFFFF) << 5) | reg;
  out[2] =
      0xF2C00000 | (static_cast<uint32_t>((imm >> 32) & 0xFFFF) << 5) | reg;
  out[3] =
      0xF2E00000 | (static_cast<uint32_t>((imm >> 48) & 0xFFFF) << 5) | reg;
}

void BuildGuestTrampoline(uint8_t* buffer, void* proc, void* userdata1,
                          void* userdata2, void* guest_to_host_thunk) {
  auto* code = reinterpret_cast<uint32_t*>(buffer);
  EncodeMovImm64(&code[0], 0, reinterpret_cast<uint64_t>(proc));
  EncodeMovImm64(&code[4], 1, reinterpret_cast<uint64_t>(userdata1));
  EncodeMovImm64(&code[8], 2, reinterpret_cast<uint64_t>(userdata2));
  EncodeMovImm64(&code[12], 9,
                 reinterpret_cast<uint64_t>(guest_to_host_thunk));
  code[16] = 0xD61F0120;  // br x9
}

void* AllocateGuestTrampolineMemory(size_t size) {
#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
  void* result = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return result == MAP_FAILED ? nullptr : result;
#elif XE_PLATFORM_WIN32
  return xe::memory::AllocFixed(
      nullptr, size, xe::memory::AllocationType::kReserveCommit,
      xe::memory::PageAccess::kExecuteReadWrite);
#else
  return nullptr;
#endif
}

void FreeGuestTrampolineMemory(void* address, size_t size) {
  if (!address) {
    return;
  }
#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
  munmap(address, size);
#elif XE_PLATFORM_WIN32
  xe::memory::DeallocFixed(address, 0,
                           xe::memory::DeallocationType::kRelease);
#endif
}

void FlushInstructionRange(void* address, size_t size) {
#if XE_PLATFORM_WIN32
  ::FlushInstructionCache(::GetCurrentProcess(), address, size);
#else
  auto* start = reinterpret_cast<char*>(address);
  __builtin___clear_cache(start, start + size);
#endif
}

bool PatchArm64Instruction(void* ptr, uint32_t instruction) {
  xe::memory::PageAccess old_access = xe::memory::PageAccess::kNoAccess;
  if (!xe::memory::Protect(ptr, sizeof(uint32_t),
                           xe::memory::PageAccess::kExecuteReadWrite,
                           &old_access)) {
    XELOGE("ARM64 breakpoint patch failed to make {:016X} writable",
           static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)));
    return false;
  }

  xe::store<uint32_t>(ptr, instruction);
#if XE_PLATFORM_WIN32
  ::FlushInstructionCache(::GetCurrentProcess(), ptr, sizeof(uint32_t));
#else
  auto* start = reinterpret_cast<char*>(ptr);
  __builtin___clear_cache(start, start + sizeof(uint32_t));
#endif

  if (!xe::memory::Protect(ptr, sizeof(uint32_t), old_access)) {
    XELOGW("ARM64 breakpoint patch failed to restore page protection at {:016X}",
           static_cast<uint64_t>(reinterpret_cast<uintptr_t>(ptr)));
  }
  return true;
}

class Arm64ThunkEmitter : public Xbyak_aarch64::CodeGenerator {
 public:
  explicit Arm64ThunkEmitter(Arm64Backend* backend)
      : CodeGenerator(kArm64ThunkCodeSize), backend_(backend) {}

  HostToGuestThunk EmitHostToGuestThunk() {
    using namespace Xbyak_aarch64;

    stp(x19, x20, pre_ptr(sp, -16));
    stp(x21, x30, pre_ptr(sp, -16));

    mov(x20, x1);
    ldr(x21, ptr(x20, static_cast<uint32_t>(
                          offsetof(ppc::PPCContext, virtual_membase))));
    mov(x9, x0);
    mov(x0, x2);
    blr(x9);

    ldp(x21, x30, post_ptr(sp, 16));
    ldp(x19, x20, post_ptr(sp, 16));
    ret();

    return reinterpret_cast<HostToGuestThunk>(EmplaceThunk(32));
  }

  GuestToHostThunk EmitGuestToHostThunk() {
    using namespace Xbyak_aarch64;

    stp(x29, x30, pre_ptr(sp, -16));
    mov(x9, x0);
    mov(x0, x20);
    blr(x9);
    ldp(x29, x30, post_ptr(sp, 16));
    ret();

    return reinterpret_cast<GuestToHostThunk>(EmplaceThunk(16));
  }

  ResolveFunctionThunk EmitResolveFunctionThunk() {
    using namespace Xbyak_aarch64;

    Label failed;
    stp(x29, x30, pre_ptr(sp, -16));
    stp(x0, x19, pre_ptr(sp, -16));

    mov(x0, x20);
    mov(x1, x16);
    mov(x9, reinterpret_cast<uint64_t>(&ResolveFunction));
    blr(x9);
    mov(x9, x0);

    ldp(x0, x19, post_ptr(sp, 16));
    ldp(x29, x30, post_ptr(sp, 16));

    cbz(x9, failed);
    br(x9);
    L(failed);
    brk(0xF000);

    return reinterpret_cast<ResolveFunctionThunk>(EmplaceThunk(32));
  }

 private:
  void* EmplaceThunk(size_t stack_size) {
    ready(Xbyak_aarch64::CodeArray::PROTECT_RW);

    Arm64EmitFunctionInfo func_info = {};
    func_info.code_size.total = getSize();
    func_info.code_size.body = getSize();
    func_info.stack_size = stack_size;

    void* code_execute_address = nullptr;
    void* code_write_address = nullptr;
    backend_->code_cache()->PlaceHostCode(
        0, const_cast<void*>(static_cast<const void*>(getCode())), func_info,
        code_execute_address, code_write_address);
    return code_execute_address;
  }

  Arm64Backend* backend_ = nullptr;
};

}  // namespace

uint64_t ResolveFunction(void* raw_context, uint64_t target_address) {
  auto* guest_context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  if (!guest_context || !guest_context->thread_state || !target_address) {
    return 0;
  }

  auto* thread_state = guest_context->thread_state;
  auto* function = thread_state->processor()->ResolveFunction(
      static_cast<uint32_t>(target_address));
  if (!function || !function->is_guest()) {
    return 0;
  }

  auto* guest_function = static_cast<GuestFunction*>(function);
  return reinterpret_cast<uint64_t>(guest_function->machine_code());
}

Arm64Backend::Arm64Backend() = default;

Arm64Backend::~Arm64Backend() {
  ExceptionHandler::Uninstall(&ExceptionCallbackThunk, this);
  FreeGuestTrampolineMemory(
      guest_trampoline_memory_,
      kGuestTrampolineSize * static_cast<size_t>(MAX_GUEST_TRAMPOLINES));
}

bool Arm64Backend::Initialize(Processor* processor) {
  if (!Backend::Initialize(processor)) {
    return false;
  }

  machine_info_.supports_extended_load_store = false;

  auto& gprs = machine_info_.register_sets[0];
  gprs.id = 0;
  std::strcpy(gprs.name, "gpr");
  gprs.types = MachineInfo::RegisterSet::INT_TYPES;
  gprs.count = 24;

  auto& vecs = machine_info_.register_sets[1];
  vecs.id = 1;
  std::strcpy(vecs.name, "vec");
  vecs.types = MachineInfo::RegisterSet::FLOAT_TYPES |
               MachineInfo::RegisterSet::VEC_TYPES;
  vecs.count = 32;

  code_cache_ = Arm64CodeCache::Create();
  Backend::code_cache_ = code_cache_.get();
  if (!code_cache_->Initialize()) {
    return false;
  }
  if (!code_cache_->RunSmokeTest()) {
    return false;
  }

  {
    Arm64ThunkEmitter host_to_guest_emitter(this);
    host_to_guest_thunk_ = host_to_guest_emitter.EmitHostToGuestThunk();
    Arm64ThunkEmitter guest_to_host_emitter(this);
    guest_to_host_thunk_ = guest_to_host_emitter.EmitGuestToHostThunk();
    Arm64ThunkEmitter resolve_emitter(this);
    resolve_function_thunk_ = resolve_emitter.EmitResolveFunctionThunk();
  }
  if (!host_to_guest_thunk_ || !guest_to_host_thunk_ ||
      !resolve_function_thunk_) {
    XELOGE("ARM64 backend failed to generate transition thunks");
    return false;
  }
  code_cache_->set_indirection_default_64(
      reinterpret_cast<uint64_t>(resolve_function_thunk_));
  guest_trampoline_memory_ = reinterpret_cast<uint8_t*>(
      AllocateGuestTrampolineMemory(kGuestTrampolineSize *
                                    static_cast<size_t>(MAX_GUEST_TRAMPOLINES)));
  if (!guest_trampoline_memory_) {
    XELOGE("ARM64 backend failed to allocate guest trampoline memory");
    return false;
  }
  guest_trampoline_address_bitmap_.Resize(MAX_GUEST_TRAMPOLINES);
  code_cache_->CommitExecutableRange(GUEST_TRAMPOLINE_BASE,
                                     GUEST_TRAMPOLINE_END);
  code_cache_->CommitExecutableRange(kForceReturnAddress, 0x9FFFFFFF);
  XELOGI(
      "ARM64 transition thunks generated host_to_guest={:016X} "
      "guest_to_host={:016X} resolve={:016X} trampolines={:016X}-{:016X}",
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(host_to_guest_thunk_)),
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(guest_to_host_thunk_)),
      static_cast<uint64_t>(
          reinterpret_cast<uintptr_t>(resolve_function_thunk_)),
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
          guest_trampoline_memory_)),
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(
          guest_trampoline_memory_ + (kGuestTrampolineSize *
                                      MAX_GUEST_TRAMPOLINES))));

  ExceptionHandler::Install(&ExceptionCallbackThunk, this);

  XELOGI(
      "ARM64 CPU backend initialized with executable code cache. This is a "
      "research mini-JIT plus interpreter fallback, not a converted x64-grade "
      "backend.");
  return true;
}

void Arm64Backend::CommitExecutableRange(uint32_t guest_low,
                                         uint32_t guest_high) {
  code_cache_->CommitExecutableRange(guest_low, guest_high);
}

std::unique_ptr<Assembler> Arm64Backend::CreateAssembler() {
  return std::make_unique<Arm64Assembler>(this);
}

std::unique_ptr<GuestFunction> Arm64Backend::CreateGuestFunction(
    Module* module, uint32_t address) {
  return std::make_unique<Arm64Function>(module, address);
}

uint64_t Arm64Backend::CalculateNextHostInstruction(
    ThreadDebugInfo* thread_info, uint64_t current_pc) {
  (void)thread_info;
  return current_pc + 4;
}

void Arm64Backend::InstallBreakpoint(Breakpoint* breakpoint) {
  breakpoint->ForEachHostAddress([breakpoint](uint64_t host_address) {
    auto* ptr = reinterpret_cast<void*>(host_address);
    auto original_bytes = xe::load<uint32_t>(ptr);
    assert_true(original_bytes != kArm64Brk0);
    if (PatchArm64Instruction(ptr, kArm64Brk0)) {
      breakpoint->backend_data().emplace_back(host_address, original_bytes);
    }
  });
}

void Arm64Backend::InstallBreakpoint(Breakpoint* breakpoint, Function* fn) {
  assert_true(breakpoint->address_type() == Breakpoint::AddressType::kGuest);
  assert_true(fn->is_guest());
  auto* guest_function = reinterpret_cast<cpu::GuestFunction*>(fn);
  auto host_address =
      guest_function->MapGuestAddressToMachineCode(breakpoint->guest_address());
  if (!host_address) {
    assert_always();
    return;
  }

  auto* ptr = reinterpret_cast<void*>(host_address);
  auto original_bytes = xe::load<uint32_t>(ptr);
  assert_true(original_bytes != kArm64Brk0);
  if (PatchArm64Instruction(ptr, kArm64Brk0)) {
    breakpoint->backend_data().emplace_back(host_address, original_bytes);
  }
}

void Arm64Backend::UninstallBreakpoint(Breakpoint* breakpoint) {
  for (auto& pair : breakpoint->backend_data()) {
    auto* ptr = reinterpret_cast<void*>(pair.first);
    auto instruction_bytes = xe::load<uint32_t>(ptr);
    assert_true(instruction_bytes == kArm64Brk0);
    PatchArm64Instruction(ptr, static_cast<uint32_t>(pair.second));
  }
  breakpoint->backend_data().clear();
}

uint32_t Arm64Backend::CreateGuestTrampoline(GuestTrampolineProc proc,
                                             void* userdata1, void* userdata2,
                                             bool long_term) {
  if (!guest_trampoline_memory_ || !guest_to_host_thunk_) {
    XELOGE("ARM64 guest trampoline requested before backend is ready");
    return 0;
  }

  size_t index = long_term ? guest_trampoline_address_bitmap_.AcquireFromBack()
                           : guest_trampoline_address_bitmap_.Acquire();
  if (index == static_cast<size_t>(-1)) {
    XELOGE("ARM64 guest trampoline pool exhausted");
    return 0;
  }

  uint8_t* write_pos = guest_trampoline_memory_ + kGuestTrampolineSize * index;
  BuildGuestTrampoline(write_pos, reinterpret_cast<void*>(proc), userdata1,
                       userdata2,
                       reinterpret_cast<void*>(guest_to_host_thunk_));
  FlushInstructionRange(write_pos, kGuestTrampolineSize);

  uint32_t indirection_guest_addr =
      GUEST_TRAMPOLINE_BASE +
      static_cast<uint32_t>(index) * GUEST_TRAMPOLINE_MIN_LEN;
  code_cache()->AddIndirection64(
      indirection_guest_addr,
      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(write_pos)));
  return indirection_guest_addr;
}

void Arm64Backend::FreeGuestTrampoline(uint32_t trampoline_addr) {
  if (trampoline_addr < GUEST_TRAMPOLINE_BASE ||
      trampoline_addr >= GUEST_TRAMPOLINE_END) {
    return;
  }
  size_t index =
      (trampoline_addr - GUEST_TRAMPOLINE_BASE) / GUEST_TRAMPOLINE_MIN_LEN;
  guest_trampoline_address_bitmap_.Release(index);
}

bool Arm64Backend::ExceptionCallbackThunk(Exception* ex, void* data) {
  auto* backend = reinterpret_cast<Arm64Backend*>(data);
  return backend->ExceptionCallback(ex);
}

bool Arm64Backend::ExceptionCallback(Exception* ex) {
  if (ex->code() != Exception::Code::kIllegalInstruction) {
    return false;
  }

  auto instruction_bytes =
      xe::load<uint32_t>(reinterpret_cast<void*>(ex->pc()));
  if (instruction_bytes != kArm64Brk0) {
    return false;
  }

  return processor()->OnThreadBreakpointHit(ex);
}

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
