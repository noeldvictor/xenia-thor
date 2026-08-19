/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_PPC_HIR_BUILDER_H_
#define XENIA_CPU_PPC_PPC_HIR_BUILDER_H_

#include "xenia/base/string_buffer.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/hir/hir_builder.h"

namespace xe {
namespace cpu {
namespace ppc {

struct PPCBuiltins;
class PPCFrontend;

class PPCHIRBuilder : public hir::HIRBuilder {
  using Instr = xe::cpu::hir::Instr;
  using Label = xe::cpu::hir::Label;
  using Value = xe::cpu::hir::Value;

 public:
  explicit PPCHIRBuilder(PPCFrontend* frontend);
  ~PPCHIRBuilder() override;

  PPCBuiltins* builtins() const;

  void Reset() override;

  enum EmitFlags {
    // Emit comment nodes.
    EMIT_DEBUG_COMMENTS = 1 << 0,
  };
  bool Emit(GuestFunction* function, uint32_t flags);

  // JIT inlining (Unit 1): splice a straight-line leaf's body_count body
  // instructions inline (skipping its terminal blr). Pre-validated by
  // ScanInlineLeafCandidate; never branches/recurses/partial-emits.
  void EmitInlineLeaf(uint32_t address, uint32_t body_count);

  GuestFunction* function() const { return function_; }
  PPCFrontend* frontend() const { return frontend_; }
  Function* LookupFunction(uint32_t address);
  Label* LookupLabel(uint32_t address);

  Value* LoadLR();
  void StoreLR(Value* value);
  Value* LoadCTR();
  void StoreCTR(Value* value);
  Value* LoadCR();
  Value* LoadCR(uint32_t n);
  Value* LoadCRField(uint32_t n, uint32_t bit);
  void StoreCR(Value* value);
  void StoreCR(uint32_t n, Value* value);
  void StoreCRField(uint32_t n, uint32_t bit, Value* value);
  void UpdateCR(uint32_t n, Value* lhs, bool is_signed = true);
  void UpdateCR(uint32_t n, Value* lhs, Value* rhs, bool is_signed = true);
  void UpdateCR6(Value* src_value);
  // vcmpbfp: only CR6[2] is defined, and CR6[0] is never set.
  void UpdateCR6BoundsOnly(Value* result);
  Value* LoadFPSCR();
  void StoreFPSCR(Value* value);
  void ClearFPSCRExceptions(bool update_cr1);
  // Call before the arithmetic so the status the host reports afterwards
  // belongs to that operation alone. Only the recording forms pay for it.
  void BeginFPSCRUpdate(bool update_cr1);
  // Derives the summary from what the host raised, plus the invalid a
  // signalling NaN operand always means. Rc=0 clears the exception bits as
  // ClearFPSCRExceptions does.
  // `suppress`, when given, is a condition that clears everything raised.
  void UpdateFPSCR(std::initializer_list<Value*> operands, bool update_cr1,
                   Value* suppress = nullptr);
  // As UpdateFPSCR, plus the 0 x inf the host is allowed to leave unsignalled.
  void UpdateFPSCRForMultiplyAdd(Value* a, Value* c, Value* b, bool update_cr1,
                                 Value* suppress = nullptr);
  // The single-precision quirk: a denormalized operand answers with the
  // default QNaN and raises nothing. Feed the condition to both of the above.
  Value* SingleDenormalOperand(std::initializer_list<Value*> operands);
  Value* ApplySingleDenormalOperand(Value* quirk, Value* result);
  // For the estimates, whose host stand-ins raise nothing of their own.
  void UpdateFPSCRForEstimate(Value* b, bool is_sqrt_estimate, bool update_cr1);
  void CopyFPSCRToCR1();
  Value* LoadXER();
  void StoreXER(Value* value);
  // void UpdateXERWithOverflow();
  // void UpdateXERWithOverflowAndCarry();
  // void StoreOV(Value* value);
  Value* LoadCA();
  void StoreCA(Value* value);
  Value* LoadSAT();
  void StoreSAT(Value* value);

  Value* LoadGPR(uint32_t reg);
  void StoreGPR(uint32_t reg, Value* value);
  Value* LoadFPR(uint32_t reg);
  void StoreFPR(uint32_t reg, Value* value);
  Value* LoadVR(uint32_t reg);
  void StoreVR(uint32_t reg, Value* value);

  void StoreReserved(Value* val);
  Value* LoadReserved();

 private:
  void MaybeBreakOnInstruction(uint32_t address);
  void AnnotateLabel(uint32_t address, Label* label);
  void StoreFPSCRSummary(Value* raised, bool update_cr1);
  Value* FpInvalidFromOperands(std::initializer_list<Value*> operands);

  PPCFrontend* frontend_;

  // Reset whenever needed:
  StringBuffer comment_buffer_;

  // Reset each Emit:
  bool with_debug_info_;
  GuestFunction* function_;
  uint64_t start_address_;
  uint64_t instr_count_;
  Instr** instr_offset_list_;
  Label** label_list_;

  // Reset each instruction. Sized for what a typical PowerPC instruction
  // writes. Some instructions write far more - lmw stores up to 32 GPRs, and
  // an inlined helper body is a longer run again - so the Store*R helpers must
  // stop the counter at the end instead of running past it. trace_info_ is the
  // last member of this class, so an overrun corrupts the heap behind it.
  static constexpr uint32_t kMaxTraceDests = 4;
  struct {
    uint32_t dest_count;
    struct {
      uint8_t reg;
      Value* value;
    } dests[kMaxTraceDests];
  } trace_info_;
};

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_HIR_BUILDER_H_
