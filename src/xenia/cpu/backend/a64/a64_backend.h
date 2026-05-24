/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_BACKEND_H_
#define XENIA_CPU_BACKEND_A64_A64_BACKEND_H_

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "xenia/base/bit_map.h"
#include "xenia/base/cvar.h"
#include "xenia/cpu/backend/backend.h"

namespace xe {
class Exception;
namespace threading {
class HighResolutionTimer;
}  // namespace threading
}  // namespace xe

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

class A64CodeCache;
class A64Function;

typedef void* (*HostToGuestThunk)(void* target, void* arg0, void* arg1);
typedef void* (*GuestToHostThunk)(void* target, void* arg0, void* arg1);
typedef void (*ResolveFunctionThunk)();

// Place guest trampolines in an address range that the HV normally occupies.
static constexpr uint32_t GUEST_TRAMPOLINE_BASE = 0x80000000;
static constexpr uint32_t GUEST_TRAMPOLINE_END = 0x80040000;
static constexpr uint32_t GUEST_TRAMPOLINE_MIN_LEN = 8;
static constexpr uint32_t MAX_GUEST_TRAMPOLINES =
    (GUEST_TRAMPOLINE_END - GUEST_TRAMPOLINE_BASE) / GUEST_TRAMPOLINE_MIN_LEN;

#define A64_RESERVE_BLOCK_SHIFT 16
#define A64_RESERVE_NUM_ENTRIES \
  ((1024ULL * 1024ULL * 1024ULL * 4ULL) >> A64_RESERVE_BLOCK_SHIFT)

struct ReserveHelper {
  uint64_t blocks[A64_RESERVE_NUM_ENTRIES / 64];

  ReserveHelper() { memset(blocks, 0, sizeof(blocks)); }
};

struct A64BackendStackpoint {
  uint64_t host_stack_;
  unsigned guest_stack_;
  unsigned guest_return_address_;
};

enum : uint32_t {
  kA64BackendFPCRModeBit = 0,
  kA64BackendHasReserveBit = 1,
  kA64BackendNJMOn = 2,
  kA64BackendNonIEEEMode = 3,
};

// Located prior to the context register (x20) in memory.
struct A64BackendContext {
  // Scratch vectors for helper routines.
  // Using uint8_t[16] instead of NEON intrinsic types to avoid including
  // arm_neon.h in the header.
  alignas(16) uint8_t helper_scratch_v128s[4][16];
  union {
    uint64_t helper_scratch_u64s[8];
    uint32_t helper_scratch_u32s[16];
  };
  ReserveHelper* reserve_helper_;
  uint64_t cached_reserve_value_;
  uint64_t* guest_tick_count;
  uint64_t host_uptime_millis_base;
  A64BackendStackpoint* stackpoints;
  uint64_t cached_reserve_offset;
  uint32_t cached_reserve_bit;
  unsigned int current_stackpoint_depth;
  unsigned int fpcr_fpu;
  unsigned int fpcr_vmx;
  // bit 0 = 0 if fpcr is fpu, else it is vmx
  // bit 1 = got reserve
  unsigned int flags;
  unsigned int Ox1000;  // constant 0x1000
  uint32_t blue_dragon_draw_wait_probe_counter;
  uint32_t blue_dragon_draw_wait_yield_counter;
  uint32_t blue_dragon_draw_wait_caller_profile_counter;
  uint32_t last_guest_function;
  uint32_t last_guest_return_address;
  volatile uint32_t* processor_irql;
  uint32_t kf_lower_irql_apc_guard_counter;
};

// Default FPCR for FPU mode (round to nearest, no flush to zero).
constexpr unsigned int DEFAULT_FPU_FPCR = 0;
// Default FPCR for VMX mode (flush to zero, preserve NaN payloads).
constexpr unsigned int DEFAULT_VMX_FPCR = (1 << 24);  // FZ

class A64Backend : public Backend {
 public:
  static constexpr uint32_t kForceReturnAddress = 0x9FFF0000u;

  explicit A64Backend();
  ~A64Backend() override;

  A64CodeCache* code_cache() const { return code_cache_.get(); }
  uintptr_t emitter_data() const { return emitter_data_; }

  HostToGuestThunk host_to_guest_thunk() const { return host_to_guest_thunk_; }
  GuestToHostThunk guest_to_host_thunk() const { return guest_to_host_thunk_; }
  ResolveFunctionThunk resolve_function_thunk() const {
    return resolve_function_thunk_;
  }
  void* synchronize_guest_and_host_stack_helper() const {
    return synchronize_guest_and_host_stack_helper_;
  }
  bool speed_profile_enabled() const;
  std::atomic<uint64_t>* speed_profile_host_to_guest_entries() {
    return &speed_profile_host_to_guest_entries_;
  }
  std::atomic<uint64_t>* speed_profile_guest_to_host_calls() {
    return &speed_profile_guest_to_host_calls_;
  }
  std::atomic<uint64_t>* speed_profile_direct_guest_calls() {
    return &speed_profile_direct_guest_calls_;
  }
  std::atomic<uint64_t>* speed_profile_indirect_guest_calls() {
    return &speed_profile_indirect_guest_calls_;
  }
  std::atomic<uint64_t>* speed_profile_extern_calls() {
    return &speed_profile_extern_calls_;
  }
  std::atomic<uint32_t>* speed_profile_body_time_active() {
    return &speed_profile_body_time_active_;
  }
  std::atomic<uint64_t>* rtl_leave_recursive_inline_count() {
    return &rtl_leave_recursive_inline_count_;
  }
  std::atomic<uint64_t>* rtl_leave_final_inline_count() {
    return &rtl_leave_final_inline_count_;
  }
  std::atomic<uint64_t>* rtl_leave_restore_slow_count() {
    return &rtl_leave_restore_slow_count_;
  }
  std::atomic<uint64_t>* rtl_leave_native_fallback_count() {
    return &rtl_leave_native_fallback_count_;
  }
  std::atomic<uint64_t>* kf_lower_irql_apc_fastpath_count() {
    return &kf_lower_irql_apc_fastpath_count_;
  }
  std::atomic<uint64_t>* kf_lower_irql_apc_pending_fallback_count() {
    return &kf_lower_irql_apc_pending_fallback_count_;
  }
  std::atomic<uint64_t>* kf_lower_irql_apc_poll_fallback_count() {
    return &kf_lower_irql_apc_poll_fallback_count_;
  }
  std::atomic<uint64_t>* kf_lower_irql_apc_missing_fallback_count() {
    return &kf_lower_irql_apc_missing_fallback_count_;
  }
  bool BodyTimeProfileEnabledForFunction(A64Function* function) const;
  bool EntryExitTimeProfileEnabledForFunction(A64Function* function) const;
  bool BlockProfileEnabledForFunction(A64Function* function) const;
  bool BlockBodyTimeProfileEnabledForFunction(A64Function* function) const;
  bool CallEdgeProfileEnabledForFunction(A64Function* function) const;
  bool CallEdgeAuditOnlyEnabledForFunction(A64Function* function) const;

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
  void InitializeBackendContext(void* ctx) override;
  void DeinitializeBackendContext(void* ctx) override;
  void PrepareForReentry(void* ctx) override;

  A64BackendContext* BackendContextForGuestContext(void* ctx) {
    return reinterpret_cast<A64BackendContext*>(
        reinterpret_cast<intptr_t>(ctx) - sizeof(A64BackendContext));
  }

  uint32_t CreateGuestTrampoline(GuestTrampolineProc proc, void* userdata1,
                                 void* userdata2, bool long_term) override;
  void FreeGuestTrampoline(uint32_t trampoline_addr) override;
  void SetGuestRoundingMode(void* ctx, unsigned int mode) override;
  bool PopulatePseudoStacktrace(GuestPseudoStackTrace* st) override;

  void RecordMMIOExceptionForGuestInstruction(void* host_address);
  void RecordResolveFunction(bool success);
  void RegisterProfiledFunction(A64Function* function);
  void UnregisterProfiledFunction(A64Function* function);

 private:
  struct ProfiledFunctionEntry {
    A64Function* function = nullptr;
    uint64_t last_entry_count = 0;
    uint64_t last_body_ticks = 0;
    uint64_t last_prolog_ticks = 0;
    uint64_t last_epilog_ticks = 0;
    std::vector<uint64_t> last_block_counts;
    std::vector<uint64_t> last_block_body_ticks;
    std::vector<uint64_t> last_call_edge_counts;
    std::vector<uint64_t> last_call_edge_body_ticks;
  };

  static bool ExceptionCallbackThunk(Exception* ex, void* data);
  bool ExceptionCallback(Exception* ex);
  void StartSpeedProfiler();
  void LogSpeedProfile();

  uintptr_t capstone_handle_ = 0;

  std::unique_ptr<A64CodeCache> code_cache_;
  uintptr_t emitter_data_ = 0;

  HostToGuestThunk host_to_guest_thunk_ = nullptr;
  GuestToHostThunk guest_to_host_thunk_ = nullptr;
  ResolveFunctionThunk resolve_function_thunk_ = nullptr;
  void* synchronize_guest_and_host_stack_helper_ = nullptr;

 public:
  void* try_acquire_reservation_helper_ = nullptr;
  void* reserved_store_32_helper = nullptr;
  void* reserved_store_64_helper = nullptr;

 private:
  alignas(64) ReserveHelper reserve_helper_;
  BitMap guest_trampoline_address_bitmap_;
  uint8_t* guest_trampoline_memory_ = nullptr;

  std::mutex speed_profile_mutex_;
  std::vector<ProfiledFunctionEntry> speed_profile_functions_;
  std::unique_ptr<xe::threading::HighResolutionTimer> speed_profile_timer_;
  std::atomic<uint64_t> speed_profile_host_to_guest_entries_{0};
  std::atomic<uint64_t> speed_profile_guest_to_host_calls_{0};
  std::atomic<uint64_t> speed_profile_direct_guest_calls_{0};
  std::atomic<uint64_t> speed_profile_indirect_guest_calls_{0};
  std::atomic<uint64_t> speed_profile_extern_calls_{0};
  std::atomic<uint64_t> speed_profile_resolve_calls_{0};
  std::atomic<uint64_t> speed_profile_resolve_misses_{0};
  std::atomic<uint32_t> speed_profile_body_time_active_{1};
  std::atomic<uint64_t> rtl_leave_recursive_inline_count_{0};
  std::atomic<uint64_t> rtl_leave_final_inline_count_{0};
  std::atomic<uint64_t> rtl_leave_restore_slow_count_{0};
  std::atomic<uint64_t> rtl_leave_native_fallback_count_{0};
  std::atomic<uint64_t> kf_lower_irql_apc_fastpath_count_{0};
  std::atomic<uint64_t> kf_lower_irql_apc_pending_fallback_count_{0};
  std::atomic<uint64_t> kf_lower_irql_apc_poll_fallback_count_{0};
  std::atomic<uint64_t> kf_lower_irql_apc_missing_fallback_count_{0};
  uint64_t last_speed_profile_host_to_guest_entries_ = 0;
  uint64_t last_speed_profile_guest_to_host_calls_ = 0;
  uint64_t last_speed_profile_direct_guest_calls_ = 0;
  uint64_t last_speed_profile_indirect_guest_calls_ = 0;
  uint64_t last_speed_profile_extern_calls_ = 0;
  uint64_t last_speed_profile_resolve_calls_ = 0;
  uint64_t last_speed_profile_resolve_misses_ = 0;
  uint64_t speed_profile_start_host_uptime_ms_ = 0;
  uint64_t last_rtl_leave_recursive_inline_count_ = 0;
  uint64_t last_rtl_leave_final_inline_count_ = 0;
  uint64_t last_rtl_leave_restore_slow_count_ = 0;
  uint64_t last_rtl_leave_native_fallback_count_ = 0;
  uint64_t last_kf_lower_irql_apc_fastpath_count_ = 0;
  uint64_t last_kf_lower_irql_apc_pending_fallback_count_ = 0;
  uint64_t last_kf_lower_irql_apc_poll_fallback_count_ = 0;
  uint64_t last_kf_lower_irql_apc_missing_fallback_count_ = 0;
  bool speed_profile_seen_activity_ = false;
  bool speed_profile_idle_snapshot_emitted_ = false;
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_BACKEND_H_
