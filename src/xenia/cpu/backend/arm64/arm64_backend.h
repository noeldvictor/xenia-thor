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

#include "xenia/base/bit_map.h"
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

static constexpr uint32_t GUEST_TRAMPOLINE_BASE = 0x80000000u;
static constexpr uint32_t GUEST_TRAMPOLINE_END = 0x80040000u;
static constexpr uint32_t GUEST_TRAMPOLINE_MIN_LEN = 8u;
static constexpr uint32_t MAX_GUEST_TRAMPOLINES =
    (GUEST_TRAMPOLINE_END - GUEST_TRAMPOLINE_BASE) /
    GUEST_TRAMPOLINE_MIN_LEN;

static constexpr uint32_t kArm64ReserveBlockShift = 16;
static constexpr uint64_t kArm64ReserveNumEntries =
    (1024ull * 1024ull * 1024ull * 4ull) >> kArm64ReserveBlockShift;

struct Arm64ReserveHelper {
  uint64_t blocks[kArm64ReserveNumEntries / 64] = {};
};

struct Arm64BackendStackpoint {
  uint64_t host_stack = 0;
  uint32_t guest_stack = 0;
  uint32_t guest_return_address = 0;
};

enum : uint32_t {
  kArm64BackendFPCRModeBit = 0,
  kArm64BackendHasReserveBit = 1,
  kArm64BackendNJMOn = 2,
  kArm64BackendNonIEEEMode = 3,
};

struct Arm64BackendContext {
  alignas(16) uint8_t helper_scratch_v128s[4][16] = {};
  union {
    uint64_t helper_scratch_u64s[8];
    uint32_t helper_scratch_u32s[16];
  };
  Arm64ReserveHelper* reserve_helper = nullptr;
  uint64_t cached_reserve_value = 0;
  uint64_t* guest_tick_count = nullptr;
  Arm64BackendStackpoint* stackpoints = nullptr;
  uint64_t cached_reserve_offset = 0;
  uint32_t cached_reserve_bit = 0;
  uint32_t current_stackpoint_depth = 0;
  uint32_t fpcr_fpu = 0;
  uint32_t fpcr_vmx = 0;
  uint32_t flags = 0;
  uint32_t constant_0x1000 = 0;
};

static_assert(sizeof(Arm64BackendContext) <= 256,
              "ThreadState reserves 256 bytes before PPCContext.");

constexpr uint32_t kDefaultFpuFpcr = 0;
constexpr uint32_t kDefaultVmxFpcr = 1u << 24;

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
  uint32_t CreateGuestTrampoline(GuestTrampolineProc proc, void* userdata1,
                                 void* userdata2,
                                 bool long_term = false) override;
  void FreeGuestTrampoline(uint32_t trampoline_addr) override;
  void InitializeBackendContext(void* ctx) override;
  void DeinitializeBackendContext(void* ctx) override;
  void PrepareForReentry(void* ctx) override;
  void SetGuestRoundingMode(void* ctx, unsigned int mode) override;

  Arm64BackendContext* BackendContextForGuestContext(void* ctx) {
    return reinterpret_cast<Arm64BackendContext*>(
        reinterpret_cast<uint8_t*>(ctx) - sizeof(Arm64BackendContext));
  }

 private:
  static bool ExceptionCallbackThunk(Exception* ex, void* data);
  bool ExceptionCallback(Exception* ex);

  uintptr_t capstone_handle_ = 0;
  std::unique_ptr<Arm64CodeCache> code_cache_;
  uintptr_t emitter_data_ = 0;

  HostToGuestThunk host_to_guest_thunk_ = nullptr;
  GuestToHostThunk guest_to_host_thunk_ = nullptr;
  ResolveFunctionThunk resolve_function_thunk_ = nullptr;

  BitMap guest_trampoline_address_bitmap_;
  uint8_t* guest_trampoline_memory_ = nullptr;
  alignas(64) Arm64ReserveHelper reserve_helper_;
};

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_ARM64_ARM64_BACKEND_H_
