/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_EMITTER_H_
#define XENIA_CPU_BACKEND_A64_A64_EMITTER_H_

#include <atomic>
#include <functional>
#include <unordered_map>
#include <vector>

#include "xenia/base/arena.h"
#include "xenia/cpu/backend/code_cache_base.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/function_trace_data.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/hir/value.h"
#include "xenia/cpu/xex_module.h"
#include "xenia/memory.h"

#include "xbyak_aarch64.h"

namespace xe {
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {
using namespace arm64;
class A64Backend;
class A64CodeCache;
class A64Function;
struct A64ProfileBlockMetadata;

enum class FPCRMode : uint32_t { Unknown, Fpu, Vmx };

// Unfortunately due to the design of xbyak we have to pass this to the ctor.
class XbyakA64Allocator : public Xbyak_aarch64::Allocator {
 public:
  virtual bool useProtect() const { return false; }
};

class A64Emitter;
using TailEmitCallback =
    std::function<void(A64Emitter& e, Xbyak_aarch64::Label& lbl)>;
struct TailEmitter {
  Xbyak_aarch64::Label label;
  uint32_t alignment;
  TailEmitCallback func;
};

class A64Emitter : public Xbyak_aarch64::CodeGenerator {
 public:
  A64Emitter(A64Backend* backend, XbyakA64Allocator* allocator);
  virtual ~A64Emitter();

  Processor* processor() const { return processor_; }
  A64Backend* backend() const { return backend_; }

  bool Emit(GuestFunction* function, hir::HIRBuilder* builder,
            uint32_t debug_info_flags, FunctionDebugInfo* debug_info,
            void** out_code_address, size_t* out_code_size,
            std::vector<SourceMapEntry>* out_source_map);

 public:
  // Reserved: sp, x19 (backend context), x20 (context), x21 (membase)
  // Scratch: x0-x18 (caller-saved), v0-v3
  // Available GPRs for register allocator: x22-x28
  static constexpr int GPR_COUNT = 7;
  // Available VEC regs: v4-v15, v16-v31
  static constexpr int VEC_COUNT = 28;
  static constexpr size_t kStashOffset = 32;

  // a64_spill_gprs_to_vector: how many of the TOP vector registers are held
  // back as integer spill slots (A710 SWOG 4.3). Clamped so the allocator
  // always keeps a usable vector set.
  static uint32_t ReservedSpillVecs();
  // Physical vector register backing integer spill slot `index`, or -1.
  static int SpillVecForSlot(uint32_t local_offset);

  static void SetupReg(const hir::Value* v, Xbyak_aarch64::WReg& r) {
    auto idx = gpr_reg_map_[v->reg.index];
    r = Xbyak_aarch64::WReg(idx);
  }
  static void SetupReg(const hir::Value* v, Xbyak_aarch64::XReg& r) {
    auto idx = gpr_reg_map_[v->reg.index];
    r = Xbyak_aarch64::XReg(idx);
  }
  static void SetupReg(const hir::Value* v, Xbyak_aarch64::SReg& r) {
    auto idx = vec_reg_map_[v->reg.index];
    r = Xbyak_aarch64::SReg(idx);
  }
  static void SetupReg(const hir::Value* v, Xbyak_aarch64::DReg& r) {
    auto idx = vec_reg_map_[v->reg.index];
    r = Xbyak_aarch64::DReg(idx);
  }
  static void SetupReg(const hir::Value* v, Xbyak_aarch64::QReg& r) {
    auto idx = vec_reg_map_[v->reg.index];
    r = Xbyak_aarch64::QReg(idx);
  }
  static void SetupReg(const hir::Value* v, Xbyak_aarch64::VReg& r) {
    auto idx = vec_reg_map_[v->reg.index];
    r = Xbyak_aarch64::VReg(idx);
  }

  Xbyak_aarch64::Label& epilog_label() { return *epilog_label_; }

  FunctionDebugInfo* debug_info() const { return debug_info_; }
  size_t stack_size() const { return stack_size_; }

  void MarkSourceOffset(const hir::Instr* i);

  void DebugBreak();
  // Emits a cooperative-scheduler preemption safepoint: yields the fiber once
  // the context's preempt_requested flag is raised. Only valid at a block
  // head.
  void EmitPreemptCheck();
  void Trap(uint16_t trap_type = 0);
  void UnimplementedInstr(const hir::Instr* i);

  bool TryEmitGprLrHelperCall(const hir::Instr* instr,
                              GuestFunction* function);
  bool TryEmitFprVmxHelperCall(const hir::Instr* instr,
                               GuestFunction* function);
  bool TryEmitPpcThreadFieldLeafHelperCall(const hir::Instr* instr,
                                            GuestFunction* function);
  void EmitKernelExternHostCall(const GuestFunction* function);
  bool TryEmitKernelHighFrequencyExternCall(const hir::Instr* instr,
                                            const GuestFunction* function);
  bool TryEmitBlueDragonDrawWaitFunctionBody();
  bool TryEmitBlueDragonDrawWaitInlineCall(GuestFunction* function);
  bool EmitBlueDragonDrawWaitFastpathBody();
  bool TryEmitBlueDragonMemcpyFunctionBody();
  bool TryEmitBlueDragonStricmpFunctionBody();
  bool TryEmitBlueDragonJumpTableFunctionBody();
  bool TryEmitBlueDragonJumpTableInlineCall(const hir::Instr* instr,
                                            GuestFunction* function);
  bool EmitBlueDragonJumpTableDispatch();
  bool TryEmitBlueDragonVmxCopyLoopBlock(const hir::Block* block);
  bool TryEmitBlueDragonWordCopyLoopBlock(const hir::Block* block);
  void MaybeEmitBlueDragonDrawWaitCallerProfile();
  void MaybeEmitBlueDragonStricmpReturnProfile();
  void AddGuestAddressToMembase(Xbyak_aarch64::WReg guest_reg,
                                Xbyak_aarch64::XReg host_reg);
  void Call(const hir::Instr* instr, GuestFunction* function);
  void CallIndirect(const hir::Instr* instr, int reg_index);
  void CallExtern(const hir::Instr* instr, const Function* function);
  void CallNative(void* fn);
  void CallNativeSafe(void* fn);
  void SetReturnAddress(uint64_t value);
  // Spin-loop backoff hint. YIELD or ISB depending on a64_spin_hint_isb - see
  // that cvar for why YIELD is usually worthless on ARM. Hint only.
  void EmitSpinHint();

  // Compare a 32-bit register against a constant using the cheapest encoding.
  //
  // The naive form is MOV a scratch + CMP, which costs two ARITHMETIC-port
  // instructions and a serial dependency between them. On this SoC the
  // arithmetic ports are the scarce resource (the A715/A710 mid-cores have
  // three 128-bit load ports against two arithmetic ports), so folding the
  // constant into the compare wins on both port pressure and dependency depth.
  // Encodings tried, in order:
  //   imm <= 4095                  -> CMP rn, #imm
  //   imm == x << 12, x <= 4095    -> CMP rn, #x, LSL #12
  //   (-imm) <= 4095               -> CMN rn, #-imm   (e.g. 0xFFFFFFFF -> CMN #1)
  //   otherwise                    -> MOV scratch, #imm; CMP rn, scratch
  // Only the last form touches `scratch`.
  //
  // All four forms are FLAG-EXACT, so this is safe under any condition code,
  // not just EQ/NE. CMP rn,#K is architecturally rn + ~K + 1 and CMN rn,#M is
  // rn + M; when M == -K those are the same addition, so N/Z/C/V match. That
  // matters here because callers branch on LO and GE, not only on equality.
  void EmitCmpImm32(const Xbyak_aarch64::WReg& rn, uint32_t imm,
                    const Xbyak_aarch64::WReg& scratch);

  void EmitAtomicIncrement64(std::atomic<uint64_t>* counter);
  void EmitAtomicAdd64(std::atomic<uint64_t>* counter,
                       const Xbyak_aarch64::XReg& value_reg);
  void MaybeEmitCallEdgeProfileStart(std::atomic<uint64_t>* entry_counter);
  void MaybeEmitCallEdgeProfileEnd(std::atomic<uint64_t>* body_ticks_counter);
  void MaybeEmitBlockBodyTimeProfileInit();
  void MaybeEmitBlockBodyTimeTransition(const hir::Block* block);
  void MaybeEmitBlockBodyTimeEnd();
  void MaybeEmitBodyTimeProfileStart();
  void MaybeEmitBodyTimeProfileEnd();
  void MaybeEmitEntryExitTimeProfileStartInX15();
  void MaybeEmitEntryExitTimeProfileStoreStartFromX15();
  void MaybeEmitEntryExitTimeProfilePrologEnd();
  void MaybeEmitEntryExitTimeProfileEpilogEndFromX15();
  uint32_t FindBlockGuestAddress(const hir::Block* block) const;
  A64ProfileBlockMetadata CollectBlockProfileMetadata(
      const hir::Block* block) const;
  void MaybeEmitBlockProfileEntry(const hir::Block* block);
  void MaybeLogContextTrafficAudit(hir::HIRBuilder* builder);
  void MaybeLogGuestCallFastEntryAudit(hir::HIRBuilder* builder);
  void MaybeLogGuestStackArgHandoffAudit(hir::HIRBuilder* builder);

  // Backend context register = x19.
  // Points to A64BackendContext (immediately before PPCContext in memory).
  const Xbyak_aarch64::XReg& GetBackendCtxReg() const { return x19; }
  // Context register = x20.
  const Xbyak_aarch64::XReg& GetContextReg() const { return x20; }
  // Memory base register = x21.
  const Xbyak_aarch64::XReg& GetMembaseReg() const { return x21; }

  void ReloadMembase();

  void PushStackpoint();
  void PopStackpoint();
  void EnsureSynchronizedGuestAndHostStack();

  static void HandleStackpointOverflowError(ppc::PPCContext* context);

  void ForgetFpcrMode() {
    if (fpcr_mode_ == FPCRMode::Vmx) {
      ChangeFpcrMode(FPCRMode::Fpu);
    }
    fpcr_mode_ = FPCRMode::Unknown;
  }
  bool ChangeFpcrMode(FPCRMode new_mode, bool already_set = false);
  bool IsFeatureEnabled(uint64_t feature_flag) const {
    return (feature_flags_ & feature_flag) == feature_flag;
  }

  Xbyak_aarch64::Label& AddToTail(TailEmitCallback callback,
                                  uint32_t alignment = 0);
  Xbyak_aarch64::Label& NewCachedLabel();

  // 128-bit constant pool. ARM64 has no way to encode a 128-bit immediate, so
  // the fallback is MOVZ + up to three MOVK per half, then FMOV/INS into the
  // vector register - up to 10 instructions, 8 of them on the ARITHMETIC ports
  // and serially dependent. A PC-relative LDR Qd,literal is ONE instruction on
  // the LOAD ports, which are the abundant resource on this SoC (three 128-bit
  // load ports against two arithmetic on the A715/A710 mid-cores).
  //
  // Returns a label for `value`, deduplicating identical constants within the
  // function. The pool is emitted after the tail code and is copied with the
  // function, so the PC-relative distance survives the code-cache relocation.
  Xbyak_aarch64::Label& GetV128ConstLabel(const vec128_t& value);

  // Get or create a xbyak_aarch64 label for a HIR label ID.
  Xbyak_aarch64::Label& GetLabel(uint32_t label_id);

  XexModule* GuestModule() { return guest_module_; }
  uint32_t current_guest_function() const { return current_guest_function_; }

  // VMX PRESSURE CENSUS (a64_vmx_pressure_census, default off).
  // Decides whether the 128-guest-vector -> 28-host-register squeeze is
  // theoretical or dominant. Records which of the guest's 128 VMX registers a
  // function actually touches; the distinct count is the number the allocator
  // would have to keep resident to avoid PPCContext.v[] traffic entirely.
  void NoteVmxContextAccess(uint32_t context_offset);
  bool blue_dragon_f1_carrier_stack_slot_enabled() const {
    return blue_dragon_f1_carrier_stack_slot_offset_ != 0;
  }
  size_t blue_dragon_f1_carrier_stack_slot_offset() const {
    return blue_dragon_f1_carrier_stack_slot_offset_;
  }

 protected:
  void* Emplace(const EmitFunctionInfo& func_info,
                GuestFunction* function = nullptr);
  bool Emit(hir::HIRBuilder* builder, EmitFunctionInfo& func_info);

 protected:
  Processor* processor_ = nullptr;
  A64Backend* backend_ = nullptr;
  A64CodeCache* code_cache_ = nullptr;
  XbyakA64Allocator* allocator_ = nullptr;
  XexModule* guest_module_ = nullptr;
  uint64_t feature_flags_ = 0;
  uint32_t current_guest_function_ = 0;
  uint64_t vmx_touched_lo_ = 0;
  uint64_t vmx_touched_hi_ = 0;
  A64Function* current_a64_function_ = nullptr;
  std::atomic<uint64_t>* current_guest_function_entry_count_ = nullptr;
  std::atomic<uint64_t>* current_guest_function_body_ticks_ = nullptr;
  std::atomic<uint64_t>* current_guest_function_prolog_ticks_ = nullptr;
  std::atomic<uint64_t>* current_guest_function_epilog_ticks_ = nullptr;
  bool current_guest_function_block_body_ticks_ = false;
  bool current_guest_function_call_edge_profile_ = false;
  size_t body_time_start_stack_offset_ = 0;
  size_t entry_exit_time_start_stack_offset_ = 0;
  size_t block_body_time_start_stack_offset_ = 0;
  size_t block_body_time_counter_stack_offset_ = 0;
  size_t call_edge_time_start_stack_offset_ = 0;
  size_t blue_dragon_f1_carrier_stack_slot_offset_ = 0;
  size_t current_call_edge_ordinal_ = 0;
  uint32_t current_block_guest_address_ = 0;

  Xbyak_aarch64::Label* epilog_label_ = nullptr;

  hir::Instr* current_instr_ = nullptr;

  FunctionDebugInfo* debug_info_ = nullptr;
  uint32_t debug_info_flags_ = 0;
  FunctionTraceData* trace_data_ = nullptr;
  Arena source_map_arena_;

  size_t stack_size_ = 0;

  static const uint32_t gpr_reg_map_[GPR_COUNT];
  static const uint32_t vec_reg_map_[VEC_COUNT];

  std::vector<TailEmitter> tail_code_;
  std::vector<Xbyak_aarch64::Label*> label_cache_;
  std::vector<std::pair<vec128_t, Xbyak_aarch64::Label*>> v128_const_pool_;

  // Map from HIR label IDs to xbyak_aarch64 Labels.
  std::unordered_map<uint32_t, Xbyak_aarch64::Label*> label_map_;

  FPCRMode fpcr_mode_ = FPCRMode::Unknown;
  bool synchronize_stack_on_next_instruction_ = false;
};

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_EMITTER_H_
