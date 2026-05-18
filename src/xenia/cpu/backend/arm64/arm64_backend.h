/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_ARM64_ARM64_BACKEND_H_
#define XENIA_CPU_BACKEND_ARM64_ARM64_BACKEND_H_

#include <memory>

#include "xenia/cpu/backend/backend.h"

namespace xe {
class Exception;
}  // namespace xe

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {

class Arm64CodeCache;

typedef void* (*HostToGuestThunk)(void* target, void* arg0, void* arg1);
typedef void* (*GuestToHostThunk)(void* target, void* arg0, void* arg1);
typedef void (*ResolveFunctionThunk)();

class Arm64Backend : public Backend {
 public:
  static constexpr uint32_t kForceReturnAddress = 0x9FFF0000u;

  explicit Arm64Backend();
  ~Arm64Backend() override;

  Arm64CodeCache* code_cache() const { return code_cache_.get(); }
  uintptr_t emitter_data() const { return emitter_data_; }
  HostToGuestThunk host_to_guest_thunk() const {
    return host_to_guest_thunk_;
  }
  GuestToHostThunk guest_to_host_thunk() const {
    return guest_to_host_thunk_;
  }
  ResolveFunctionThunk resolve_function_thunk() const {
    return resolve_function_thunk_;
  }

  bool Initialize(Processor* processor) override;

  void CommitExecutableRange(uint32_t guest_low, uint32_t guest_high) override;

  std::unique_ptr<Assembler> CreateAssembler() override;

  std::unique_ptr<GuestFunction> CreateGuestFunction(Module* module,
                                                     uint32_t address) override;

  uint64_t CalculateNextHostInstruction(ThreadDebugInfo* thread_info,
                                        uint64_t current_pc) override;

  void InstallBreakpoint(Breakpoint* breakpoint) override;
  void InstallBreakpoint(Breakpoint* breakpoint, Function* fn) override;
  void UninstallBreakpoint(Breakpoint* breakpoint) override;

 private:
  static bool ExceptionCallbackThunk(Exception* ex, void* data);
  bool ExceptionCallback(Exception* ex);

  uintptr_t capstone_handle_ = 0;
  std::unique_ptr<Arm64CodeCache> code_cache_;
  uintptr_t emitter_data_ = 0;

  HostToGuestThunk host_to_guest_thunk_ = nullptr;
  GuestToHostThunk guest_to_host_thunk_ = nullptr;
  ResolveFunctionThunk resolve_function_thunk_ = nullptr;
};

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_ARM64_ARM64_BACKEND_H_
