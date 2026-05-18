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
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {
namespace {

constexpr uint32_t kArm64Brk0 = 0xD4200000u;

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

}  // namespace

Arm64Backend::Arm64Backend() = default;

Arm64Backend::~Arm64Backend() {
  ExceptionHandler::Uninstall(&ExceptionCallbackThunk, this);
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

  ExceptionHandler::Install(&ExceptionCallbackThunk, this);

  XELOGI(
      "ARM64 CPU backend initialized with executable code cache. This is a "
      "research mini-JIT plus interpreter fallback, not a converted x64-grade "
      "backend.");
  return true;
}

void Arm64Backend::CommitExecutableRange(uint32_t guest_low,
                                         uint32_t guest_high) {
  (void)guest_low;
  (void)guest_high;
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
