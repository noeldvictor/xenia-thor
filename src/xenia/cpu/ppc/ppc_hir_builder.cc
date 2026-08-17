/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/ppc/ppc_hir_builder.h"

#include <stddef.h>
#include <cstring>

#include "third_party/fmt/include/fmt/format.h"

#include "xenia/base/byte_order.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/string.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/hir/label.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_decode_data.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"
#include "xenia/cpu/processor.h"

DEFINE_bool(
    break_on_unimplemented_instructions, true,
    "Break to the host debugger (or crash if no debugger attached) if an "
    "unimplemented PowerPC instruction is encountered.",
    "CPU");

DEFINE_bool(
    cpu_shared_function_fastpath, false,
    "Thor cross-title CPU lever: recognize hot, byte-identical XDK runtime "
    "kernels (memset/memcpy/memmove) that recur across titles - by a "
    "relocation-invariant canonical hash - and run a native host implementation "
    "instead of JIT-translating the guest loop. The same statically-linked XDK "
    "library code is duplicated in every title, so a recognized fast-path "
    "amortizes library-wide. Default-off; the hash table is curated + "
    "RE-confirmed (empty until populated via a cpu_shared_function_harvest run), "
    "so this is INERT until verified entries are compiled in.",
    "CPU");
DEFINE_bool(
    cpu_shared_function_harvest, false,
    "Diagnostic for cpu_shared_function_fastpath: log every compiled guest "
    "function's canonical (reloc-invariant) hash + size so the hot shared XDK "
    "kernels can be identified and added to the curated fast-path table. "
    "Default-off (verbose).",
    "CPU");

namespace xe {
namespace cpu {
namespace ppc {

// TODO(benvanik): remove when enums redefined.
using namespace xe::cpu::hir;

using xe::cpu::hir::Label;
using xe::cpu::hir::TypeName;
using xe::cpu::hir::Value;

// The number of times each opcode has been translated.
// Accumulated across the entire run.
uint32_t opcode_translation_counts[static_cast<int>(PPCOpcode::kInvalid)] = {0};

void DumpAllOpcodeCounts() {
  StringBuffer sb;
  sb.Append("Instruction translation counts:\n");
  for (size_t i = 0; i < xe::countof(opcode_translation_counts); ++i) {
    auto opcode = static_cast<PPCOpcode>(i);
    auto& opcode_info = GetOpcodeInfo(opcode);
    auto& disasm_info = GetOpcodeDisasmInfo(opcode);
    auto translation_count = opcode_translation_counts[i];
    if (translation_count) {
      sb.AppendFormat("{:8d} : {}\n", translation_count, disasm_info.name);
    }
  }
  fprintf(stdout, "%s", sb.to_string().c_str());
  fflush(stdout);
}

PPCHIRBuilder::PPCHIRBuilder(PPCFrontend* frontend)
    : HIRBuilder(), frontend_(frontend), comment_buffer_(4096) {}

PPCHIRBuilder::~PPCHIRBuilder() = default;

PPCBuiltins* PPCHIRBuilder::builtins() const { return frontend_->builtins(); }

void PPCHIRBuilder::Reset() {
  function_ = nullptr;
  start_address_ = 0;
  instr_count_ = 0;
  instr_offset_list_ = NULL;
  label_list_ = NULL;
  with_debug_info_ = false;
  HIRBuilder::Reset();
}

bool PPCHIRBuilder::Emit(GuestFunction* function, uint32_t flags) {
  SCOPE_profile_cpu_f("cpu");

  Memory* memory = frontend_->memory();

  function_ = function;
  start_address_ = function_->address();
  instr_count_ = (function_->end_address() - function_->address()) / 4 + 1;

  with_debug_info_ = (flags & EMIT_DEBUG_COMMENTS) == EMIT_DEBUG_COMMENTS;
  if (with_debug_info_) {
    CommentFormat("{} fn {:08X}-{:08X} {}", function_->module()->name().c_str(),
                  function_->address(), function_->end_address(),
                  function_->name().c_str());
  }

  // Allocate offset list.
  // This is used to quickly map labels to instructions.
  // The list is built as the instructions are traversed, with the values
  // being the previous HIR Instr before the given instruction. An
  // instruction may have a label assigned to it if it hasn't been hit
  // yet.
  size_t list_size = instr_count_ * sizeof(void*);
  instr_offset_list_ = (Instr**)arena_->Alloc(list_size, alignof(void*));
  label_list_ = (Label**)arena_->Alloc(list_size, alignof(void*));
  std::memset(instr_offset_list_, 0, list_size);
  std::memset(label_list_, 0, list_size);

  // Always mark entry with label.
  label_list_[0] = NewLabel();

  // Shared-function fast-path: if this whole function is a recognized hot XDK
  // kernel, substitute a native host implementation for the entire body. The
  // substitute is structurally "<native call>; blr" - both proven HIR patterns.
  if (cvars::cpu_shared_function_fastpath || cvars::cpu_shared_function_harvest) {
    uint32_t fn_start = function_->address();
    uint32_t fn_end = function_->end_address();
    uint64_t canon = CanonicalFunctionHash(memory, fn_start, fn_end);
    uint32_t fn_size = fn_end - fn_start + 4;
    if (cvars::cpu_shared_function_harvest) {
      XELOGI("shared-fn-harvest {:08X}-{:08X} size={} canon={:016X}", fn_start,
             fn_end, fn_size, canon);
    }
    if (cvars::cpu_shared_function_fastpath) {
      SharedFunctionKind kind = LookupSharedFunction(canon, fn_size);
      Function* handler = nullptr;
      switch (kind) {
        case SharedFunctionKind::kMemset:
          handler = builtins()->shared_memset;
          break;
        case SharedFunctionKind::kMemcpy:
          handler = builtins()->shared_memcpy;
          break;
        case SharedFunctionKind::kMemmove:
          handler = builtins()->shared_memmove;
          break;
        default:
          break;
      }
      if (handler) {
        MarkLabel(label_list_[0]);
        CallExtern(handler);                          // native op (reads r3-r5)
        CallIndirect(LoadLR(), CALL_POSSIBLE_RETURN);  // return to LR, like blr
        XELOGD("shared-fn fast-path substituted {:08X} kind={}", fn_start,
               static_cast<uint32_t>(kind));
        return Finalize();
      }
    }
  }

  uint32_t start_address = function_->address();
  uint32_t end_address = function_->end_address();
  for (uint32_t address = start_address, offset = 0; address <= end_address;
       address += 4, offset++) {
    trace_info_.dest_count = 0;
    uint32_t code =
        xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
    auto opcode = LookupOpcode(code);
    auto& opcode_info = GetOpcodeInfo(opcode);

    // Mark label, if we were assigned one earlier on in the walk.
    // We may still get a label, but it'll be inserted by LookupLabel
    // as needed.
    Label* label = label_list_[offset];
    if (label) {
      MarkLabel(label);
    }

    Instr* first_instr = 0;
    if (with_debug_info_) {
      if (label) {
        AnnotateLabel(address, label);
      }
      comment_buffer_.Reset();
      comment_buffer_.AppendFormat("{:08X} {:08X} ", address, code);
      DisasmPPC(address, code, &comment_buffer_);
      Comment(comment_buffer_);
      first_instr = last_instr();
    }

    // Mark source offset for debugging.
    // We could omit this if we never wanted to debug.
    SourceOffset(address);
    if (!first_instr) {
      first_instr = last_instr();
    }

    // Stash instruction offset. It's either the SOURCE_OFFSET or the COMMENT.
    instr_offset_list_[offset] = first_instr;

    if (opcode == PPCOpcode::kInvalid) {
      XELOGE("Invalid instruction {:08X} {:08X}", address, code);
      Comment("INVALID!");
      // TraceInvalidInstruction(i);
      continue;
    }
    ++opcode_translation_counts[static_cast<int>(opcode)];

    // Synchronize the PPC context as required.
    // This will ensure all registers are saved to the PPC context before this
    // instruction executes.
    if (opcode_info.type == PPCOpcodeType::kSync) {
      ContextBarrier();
    }

    MaybeBreakOnInstruction(address);

    InstrData i;
    i.address = address;
    i.code = code;
    i.opcode = opcode;
    i.opcode_info = &opcode_info;
    if (!opcode_info.emit || opcode_info.emit(*this, i)) {
      auto& disasm_info = GetOpcodeDisasmInfo(opcode);
      XELOGE(
          "Unimplemented instr {:08X} {:08X} {} - report the game to Xenia "
          "developers; to skip, disable break_on_unimplemented_instructions",
          address, code, disasm_info.name);
      Comment("UNIMPLEMENTED!");
      if (cvars::break_on_unimplemented_instructions) {
        DebugBreak();
      }
    }
  }

  if (false) {
    DumpAllOpcodeCounts();
  }

  return Finalize();
}

void PPCHIRBuilder::EmitInlineLeaf(uint32_t address, uint32_t body_count) {
  // Splice a straight-line leaf (validated by ScanInlineLeafCandidate) inline:
  // emit its body_count body instructions and SKIP the terminal blr, so control
  // falls through to the caller's continuation. No OPCODE_CALL is emitted, so no
  // context_barrier interposes - the caller's within-block context promotion can
  // fold the per-call register/CR round-trips. The emitted HIR matches what the
  // leaf compiles to normally, minus its return. body_count is bounded and every
  // instruction was pre-validated as a non-branch, non-mtlr, valid op, so this
  // never branches, recurses, or partial-emits a call.
  Memory* memory = frontend_->memory();
  for (uint32_t n = 0; n < body_count; ++n) {
    uint32_t inst_address = address + n * 4;
    trace_info_.dest_count = 0;
    uint32_t code =
        xe::load_and_swap<uint32_t>(memory->TranslateVirtual(inst_address));
    auto opcode = LookupOpcode(code);
    auto& opcode_info = GetOpcodeInfo(opcode);
    // Emit the host->guest PC mapping for this inlined instruction, exactly as
    // the normal Emit loop does. Without it the inlined code has NO source-offset,
    // so a guest fault/exception inside it (e.g. a write-watch hit during boot)
    // maps to the wrong guest PC and is mishandled. OPCODE_SOURCE_OFFSET is
    // FLAG_IGNORE|FLAG_HIDE (not volatile / not a barrier), so it does NOT break
    // the context promotion the inline exists to enable.
    SourceOffset(inst_address);
    if (opcode == PPCOpcode::kInvalid) {
      // Pre-validation rejects invalid ops; comment defensively and continue
      // (never bail mid-body, which would leave a half-spliced leaf).
      Comment("INLINE-LEAF INVALID");
      continue;
    }
    // Synchronize context as required (same as the normal Emit loop).
    if (opcode_info.type == PPCOpcodeType::kSync) {
      ContextBarrier();
    }
    InstrData i;
    i.address = inst_address;
    i.code = code;
    i.opcode = opcode;
    i.opcode_info = &opcode_info;
    if (!opcode_info.emit || opcode_info.emit(*this, i)) {
      // Unimplemented but valid op - comment and continue, exactly as the normal
      // Emit loop does (becomes a no-op, no differently than a normal compile).
      Comment("INLINE-LEAF UNIMPLEMENTED");
    }
  }
}

void PPCHIRBuilder::MaybeBreakOnInstruction(uint32_t address) {
  if (address != cvars::break_on_instruction) {
    return;
  }

  Comment("--break-on-instruction target");

  if (cvars::break_condition_gpr < 0) {
    DebugBreak();
    return;
  }

  auto left = LoadGPR(cvars::break_condition_gpr);
  auto right = LoadConstantUint64(cvars::break_condition_value);
  if (cvars::break_condition_truncate) {
    left = Truncate(left, INT32_TYPE);
    right = Truncate(right, INT32_TYPE);
  }

  auto op = cvars::break_condition_op.c_str();
  // TODO(rick): table?
  if (xe_strcasecmp(op, "eq") == 0) {
    TrapTrue(CompareEQ(left, right));
  } else if (xe_strcasecmp(op, "ne") == 0) {
    TrapTrue(CompareNE(left, right));
  } else if (xe_strcasecmp(op, "slt") == 0) {
    TrapTrue(CompareSLT(left, right));
  } else if (xe_strcasecmp(op, "sle") == 0) {
    TrapTrue(CompareSLE(left, right));
  } else if (xe_strcasecmp(op, "sgt") == 0) {
    TrapTrue(CompareSGT(left, right));
  } else if (xe_strcasecmp(op, "sge") == 0) {
    TrapTrue(CompareSGE(left, right));
  } else if (xe_strcasecmp(op, "ult") == 0) {
    TrapTrue(CompareULT(left, right));
  } else if (xe_strcasecmp(op, "ule") == 0) {
    TrapTrue(CompareULE(left, right));
  } else if (xe_strcasecmp(op, "ugt") == 0) {
    TrapTrue(CompareUGT(left, right));
  } else if (xe_strcasecmp(op, "uge") == 0) {
    TrapTrue(CompareUGE(left, right));
  } else {
    assert_always();
  }
}

void PPCHIRBuilder::AnnotateLabel(uint32_t address, Label* label) {
  char name_buffer[13];
  auto format_result = fmt::format_to_n(name_buffer, 12, "loc_{:08X}", address);
  name_buffer[format_result.size] = '\0';
  label->name = (char*)arena_->Alloc(sizeof(name_buffer), 1);
  memcpy(label->name, name_buffer, sizeof(name_buffer));
}

Function* PPCHIRBuilder::LookupFunction(uint32_t address) {
  return frontend_->processor()->LookupFunction(address);
}

Label* PPCHIRBuilder::LookupLabel(uint32_t address) {
  if (address < start_address_) {
    return nullptr;
  }
  size_t offset = (address - start_address_) / 4;
  if (offset >= instr_count_) {
    return nullptr;
  }
  Label* label = label_list_[offset];
  if (label) {
    return label;
  }
  // No label. If we haven't yet hit the instruction in the walk
  // then create a label. Otherwise, we must go back and insert
  // the label.
  label = NewLabel();
  label_list_[offset] = label;
  Instr* instr = instr_offset_list_[offset];
  if (instr) {
    if (instr->prev) {
      // Insert label, breaking up existing instructions.
      InsertLabel(label, instr->prev);
    } else {
      // Instruction is at the head of a block, so just add the label.
      MarkLabel(label, instr->block);
    }

    // Annotate the label, as we won't do it later.
    if (with_debug_info_) {
      AnnotateLabel(address, label);
    }
  }
  return label;
}

// Value* PPCHIRBuilder::LoadXER() {
//}
//
// void PPCHIRBuilder::StoreXER(Value* value) {
//}

Value* PPCHIRBuilder::LoadLR() {
  return LoadContext(offsetof(PPCContext, lr), INT64_TYPE);
}

void PPCHIRBuilder::StoreLR(Value* value) {
  assert_true(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, lr), value);

  if (trace_info_.dest_count >= kMaxTraceDests) {
    return;  // trace slots are per-instruction; extra stores stay untraced
  }
  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 64;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadCTR() {
  return LoadContext(offsetof(PPCContext, ctr), INT64_TYPE);
}

void PPCHIRBuilder::StoreCTR(Value* value) {
  assert_true(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, ctr), value);

  if (trace_info_.dest_count >= kMaxTraceDests) {
    return;  // trace slots are per-instruction; extra stores stay untraced
  }
  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 65;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadCR() {
  // All bits. This is expensive, but seems to be less used than the
  // field-specific LoadCR.
  Value* v = LoadCR(0);
  for (int i = 1; i <= 7; ++i) {
    v = Or(v, LoadCR(i));
  }
  return v;
}

Value* PPCHIRBuilder::LoadCR(uint32_t n) {
  // Construct the entire word of just the bits we care about.
  // This makes it easier for the optimizer to exclude things, though
  // we could be even more clever and watch sequences.
  Value* v = Shl(ZeroExtend(LoadContext(offsetof(PPCContext, cr0) + (4 * n) + 0,
                                        INT8_TYPE),
                            INT64_TYPE),
                 4 * (7 - n) + 3);
  v = Or(v, Shl(ZeroExtend(LoadContext(offsetof(PPCContext, cr0) + (4 * n) + 1,
                                       INT8_TYPE),
                           INT64_TYPE),
                4 * (7 - n) + 2));
  v = Or(v, Shl(ZeroExtend(LoadContext(offsetof(PPCContext, cr0) + (4 * n) + 2,
                                       INT8_TYPE),
                           INT64_TYPE),
                4 * (7 - n) + 1));
  v = Or(v, Shl(ZeroExtend(LoadContext(offsetof(PPCContext, cr0) + (4 * n) + 3,
                                       INT8_TYPE),
                           INT64_TYPE),
                4 * (7 - n) + 0));
  return v;
}

Value* PPCHIRBuilder::LoadCRField(uint32_t n, uint32_t bit) {
  return LoadContext(offsetof(PPCContext, cr0) + (4 * n) + bit, INT8_TYPE);
}

void PPCHIRBuilder::StoreCR(Value* value) {
  // All bits. This is expensive, but seems to be less used than the
  // field-specific StoreCR.
  for (int i = 0; i <= 7; ++i) {
    StoreCR(i, value);
  }
}

void PPCHIRBuilder::StoreCR(uint32_t n, Value* value) {
  // Pull out the bits we are interested in.
  // Optimization passes will kill any unneeded stores (mostly).
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 0,
               And(Truncate(Shr(value, 4 * (7 - n) + 3), INT8_TYPE),
                   LoadConstantUint8(1)));
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 1,
               And(Truncate(Shr(value, 4 * (7 - n) + 2), INT8_TYPE),
                   LoadConstantUint8(1)));
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 2,
               And(Truncate(Shr(value, 4 * (7 - n) + 1), INT8_TYPE),
                   LoadConstantUint8(1)));
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 3,
               And(Truncate(Shr(value, 4 * (7 - n) + 0), INT8_TYPE),
                   LoadConstantUint8(1)));
}

void PPCHIRBuilder::StoreCRField(uint32_t n, uint32_t bit, Value* value) {
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + bit, value);

  // TODO(benvanik): trace CR.
}

void PPCHIRBuilder::UpdateCR(uint32_t n, Value* lhs, bool is_signed) {
  UpdateCR(n, Truncate(lhs, INT32_TYPE), LoadZeroInt32(), is_signed);
}

void PPCHIRBuilder::UpdateCR(uint32_t n, Value* lhs, Value* rhs,
                             bool is_signed) {
  if (is_signed) {
    Value* lt = CompareSLT(lhs, rhs);
    StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 0, lt);
    Value* gt = CompareSGT(lhs, rhs);
    StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 1, gt);
  } else {
    Value* lt = CompareULT(lhs, rhs);
    StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 0, lt);
    Value* gt = CompareUGT(lhs, rhs);
    StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 1, gt);
  }
  Value* eq = CompareEQ(lhs, rhs);
  StoreContext(offsetof(PPCContext, cr0) + (4 * n) + 2, eq);

  // Value* so = AllocValue(UINT8_TYPE);
  // StoreContext(offsetof(PPCContext, cr) + (4 * n) + 3, so);

  // TOOD(benvanik): trace CR.
}

void PPCHIRBuilder::UpdateCR6(Value* src_value) {
  // Testing for all 1's and all 0's.
  // if (Rc) CR6 = all_equal | 0 | none_equal | 0
  // TODO(benvanik): efficient instruction?
  StoreContext(offsetof(PPCContext, cr6.cr6_1), LoadZeroInt8());
  StoreContext(offsetof(PPCContext, cr6.cr6_3), LoadZeroInt8());
  StoreContext(offsetof(PPCContext, cr6.cr6_all_equal),
               IsFalse(Not(src_value)));
  StoreContext(offsetof(PPCContext, cr6.cr6_none_equal), IsFalse(src_value));

  // TOOD(benvanik): trace CR.
}

Value* PPCHIRBuilder::LoadFPSCR() {
  return LoadContext(offsetof(PPCContext, fpscr), INT32_TYPE);
}

void PPCHIRBuilder::StoreFPSCR(Value* value) {
  assert_true(value->type == INT32_TYPE);
  StoreContext(offsetof(PPCContext, fpscr), value);

  if (trace_info_.dest_count >= kMaxTraceDests) {
    return;  // trace slots are per-instruction; extra stores stay untraced
  }
  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 67;
  trace_reg.value = value;
}

void PPCHIRBuilder::StoreFPSCRSummary(Value* vx, bool update_cr1) {
  // TODO(benvanik): detect overflow and the inexact cases; the latter needs a
  // status register read after every operation.
  if (update_cr1) {
    // Store into the CR1 field directly rather than calling CopyFPSCRToCR1, so
    // we do not have to read the bits back and shift them.
    StoreContext(offsetof(PPCContext, cr1.cr1_fx), vx);
    StoreContext(offsetof(PPCContext, cr1.cr1_fex), LoadConstantInt8(0));
    StoreContext(offsetof(PPCContext, cr1.cr1_vx), vx);
    StoreContext(offsetof(PPCContext, cr1.cr1_ox), LoadConstantInt8(0));
  }
  // FX summarizes VX, so both carry the same value here.
  Value* vx32 = ZeroExtend(vx, INT32_TYPE);
  Value* new_bits = Or(Shl(vx32, 31), Shl(vx32, 29));
  // Hardware ACCUMULATES these until software clears them, but VX is only
  // derived for the recording forms, so an accumulated summary would report a
  // stale answer rather than a missing one. Each instruction keeps its own.
  Value* bits = LoadFPSCR();
  bits = Or(And(bits, LoadConstantUint32(0x1FFFFFFF)), new_bits);
  StoreFPSCR(bits);
}

void PPCHIRBuilder::ClearFPSCRExceptions(bool update_cr1) {
  StoreFPSCRSummary(LoadConstantInt8(0), update_cr1);
}

void PPCHIRBuilder::UpdateFPSCR(Value* result,
                                std::initializer_list<Value*> operands,
                                bool update_cr1) {
  if (!update_cr1) {
    // Rc=0 pays nothing: it clears as before.
    ClearFPSCRExceptions(false);
    return;
  }
  // An invalid operation is the only way a NaN can appear that did not come
  // from an operand, and an SNaN operand is invalid however it propagates.
  Value* any_nan = nullptr;
  Value* any_snan = nullptr;
  for (Value* operand : operands) {
    Value* is_nan = IsNan(operand);
    Value* quiet_bit = And(Cast(operand, INT64_TYPE),
                           LoadConstantUint64(0x0008000000000000ull));
    Value* is_snan = And(is_nan, IsFalse(quiet_bit));
    any_nan = any_nan ? Or(any_nan, is_nan) : is_nan;
    any_snan = any_snan ? Or(any_snan, is_snan) : is_snan;
  }
  Value* vx;
  if (any_nan) {
    vx = And(IsNan(result), Or(any_snan, IsFalse(any_nan)));
  } else {
    vx = LoadConstantInt8(0);
  }
  StoreFPSCRSummary(vx, true);
}

void PPCHIRBuilder::UpdateFPSCR(Value* result, bool update_cr1) {
  ClearFPSCRExceptions(update_cr1);
}

void PPCHIRBuilder::CopyFPSCRToCR1() {
  // Pull out of FPSCR.
  Value* fpscr = LoadFPSCR();
  StoreContext(offsetof(PPCContext, cr1.cr1_fx),
               And(Truncate(Shr(fpscr, 31), INT8_TYPE), LoadConstantInt8(1)));
  StoreContext(offsetof(PPCContext, cr1.cr1_fex),
               And(Truncate(Shr(fpscr, 30), INT8_TYPE), LoadConstantInt8(1)));
  StoreContext(offsetof(PPCContext, cr1.cr1_vx),
               And(Truncate(Shr(fpscr, 29), INT8_TYPE), LoadConstantInt8(1)));
  StoreContext(offsetof(PPCContext, cr1.cr1_ox),
               And(Truncate(Shr(fpscr, 28), INT8_TYPE), LoadConstantInt8(1)));
}

Value* PPCHIRBuilder::LoadXER() {
  Value* v = Shl(ZeroExtend(LoadCA(), INT64_TYPE), 29);
  // TODO(benvanik): construct with other flags; overflow, etc?
  return v;
}

void PPCHIRBuilder::StoreXER(Value* value) {
  // TODO(benvanik): use other fields? For now, just pull out CA.
  StoreCA(Truncate(And(Shr(value, 29), LoadConstantInt64(1)), INT8_TYPE));
}

Value* PPCHIRBuilder::LoadCA() {
  return LoadContext(offsetof(PPCContext, xer_ca), INT8_TYPE);
}

void PPCHIRBuilder::StoreCA(Value* value) {
  assert_true(value->type == INT8_TYPE);
  StoreContext(offsetof(PPCContext, xer_ca), value);

  if (trace_info_.dest_count >= kMaxTraceDests) {
    return;  // trace slots are per-instruction; extra stores stay untraced
  }
  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 66;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadSAT() {
  return LoadContext(offsetof(PPCContext, vscr_sat), INT8_TYPE);
}

void PPCHIRBuilder::StoreSAT(Value* value) {
  value = Truncate(value, INT8_TYPE);
  StoreContext(offsetof(PPCContext, vscr_sat), value);

  if (trace_info_.dest_count >= kMaxTraceDests) {
    return;  // trace slots are per-instruction; extra stores stay untraced
  }
  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 44;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadGPR(uint32_t reg) {
  return LoadContext(offsetof(PPCContext, r) + reg * 8, INT64_TYPE);
}

void PPCHIRBuilder::StoreGPR(uint32_t reg, Value* value) {
  assert_true(value->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, r) + reg * 8, value);

  if (trace_info_.dest_count >= kMaxTraceDests) {
    return;  // trace slots are per-instruction; extra stores stay untraced
  }
  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = reg;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadFPR(uint32_t reg) {
  return LoadContext(offsetof(PPCContext, f) + reg * 8, FLOAT64_TYPE);
}

void PPCHIRBuilder::StoreFPR(uint32_t reg, Value* value) {
  assert_true(value->type == FLOAT64_TYPE);
  StoreContext(offsetof(PPCContext, f) + reg * 8, value);

  if (trace_info_.dest_count >= kMaxTraceDests) {
    return;  // trace slots are per-instruction; extra stores stay untraced
  }
  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = reg + 32;
  trace_reg.value = value;
}

Value* PPCHIRBuilder::LoadVR(uint32_t reg) {
  return LoadContext(offsetof(PPCContext, v) + reg * 16, VEC128_TYPE);
}

void PPCHIRBuilder::StoreVR(uint32_t reg, Value* value) {
  assert_true(value->type == VEC128_TYPE);
  StoreContext(offsetof(PPCContext, v) + reg * 16, value);

  if (trace_info_.dest_count >= kMaxTraceDests) {
    return;  // trace slots are per-instruction; extra stores stay untraced
  }
  auto& trace_reg = trace_info_.dests[trace_info_.dest_count++];
  trace_reg.reg = 128 + reg;
  trace_reg.value = value;
}

void PPCHIRBuilder::StoreReserved(Value* val) {
  assert_true(val->type == INT64_TYPE);
  StoreContext(offsetof(PPCContext, reserved_val), val);
}

Value* PPCHIRBuilder::LoadReserved() {
  return LoadContext(offsetof(PPCContext, reserved_val), INT64_TYPE);
}

}  // namespace ppc
}  // namespace cpu
}  // namespace xe
