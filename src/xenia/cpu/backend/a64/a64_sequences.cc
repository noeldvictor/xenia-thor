/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_sequences.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <type_traits>

#include "xenia/base/byte_order.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_emitter.h"
#include "xenia/cpu/backend/a64/a64_op.h"
#include "xenia/cpu/backend/a64/a64_seq_util.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"
#include "xenia/cpu/backend/a64/a64_tracers.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/ppc/ppc_context.h"

DECLARE_bool(arm64_add_sub_imm_audit);
DECLARE_uint32(arm64_add_sub_imm_audit_function);
DECLARE_uint32(arm64_add_sub_imm_audit_budget);
DECLARE_bool(arm64_add_i64_wrapped_imm_fastpath);
DECLARE_uint32(arm64_add_i64_wrapped_imm_fastpath_function);
DECLARE_bool(arm64_immediate_lowering_audit);
DECLARE_uint32(arm64_immediate_lowering_audit_function);
DECLARE_uint32(arm64_immediate_lowering_audit_budget);
DECLARE_bool(arm64_cr_compare_branch_across_context_barrier);
DECLARE_bool(arm64_cr_store_elide_for_fused_branch);
DECLARE_uint32(arm64_cr_store_elide_for_fused_branch_function);
DECLARE_bool(arm64_blue_dragon_mul_add_v128_fastpath);
DECLARE_bool(arm64_blue_dragon_mul_add_v128_audit);
DECLARE_bool(arm64_blue_dragon_call_boundary_state_audit);
DECLARE_bool(arm64_blue_dragon_call_boundary_state_suppress_dead_stores);
DECLARE_bool(arm64_blue_dragon_f1_carrier_audit);
DECLARE_bool(arm64_blue_dragon_f1_carrier_fastpath);
DECLARE_bool(arm64_blue_dragon_state_carrier_design_audit);
DECLARE_bool(arm64_blue_dragon_edge_variant_audit);
DECLARE_bool(arm64_blue_dragon_edge_payload_storage_audit);
DECLARE_bool(arm64_blue_dragon_fpscr_cfg_writeback_audit);
DECLARE_bool(arm64_vmx_dot_f32_fastpath);

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using namespace xe::cpu::hir;
using namespace Xbyak_aarch64;

std::unordered_map<uint32_t, SequenceSelectFn>& SequenceTable() {
  static auto* sequence_table =
      new std::unordered_map<uint32_t, SequenceSelectFn>();
  return *sequence_table;
}

// ============================================================================
// Debug validation helpers
// ============================================================================
// Validates that a binary op with constant src1 won't clobber src2
// when dest and src2 share the same physical register.
// Call this at JIT-compile time (not in emitted code).
template <typename DEST, typename SRC2>
static void AssertNoClobber(const DEST& dest, const SRC2& src2) {
  // If src2 is a register (not constant) and dest is the same register,
  // the caller must use a scratch register for the constant.
  if (!src2.is_constant) {
    assert_true(dest.reg().getIdx() != src2.reg().getIdx() &&
                "Binary op with constant src1: dest == src2 would clobber! "
                "Use a scratch register for the constant.");
  }
}

// ============================================================================
// Safe binary operation helpers
// ============================================================================
// Emits dest = op(src1_const, src2_reg) safely, using a scratch register
// to avoid clobbering src2 when dest and src2 are the same register.
// Usage: EmitSafeBinaryConst1(e, i.dest, imm, i.src2, op_fn)
template <typename REG, typename FN>
static void EmitBinaryConstLhs(A64Emitter& e, const REG& dest,
                               uint64_t src1_const, const REG& src2,
                               const FN& op_fn) {
  // Always use scratch to avoid clobbering src2 if dest == src2.
  if constexpr (std::is_same_v<REG, WReg>) {
    e.mov(e.w17, src1_const);
    op_fn(e, dest, WReg(17), src2);
  } else {
    e.mov(e.x17, src1_const);
    op_fn(e, dest, XReg(17), src2);
  }
}

namespace {

std::atomic<uint32_t> a64_add_sub_imm_audit_count{0};
std::atomic<uint32_t> a64_immediate_lowering_audit_count{0};

static bool HasWrappedSmallInverse(uint64_t imm, uint32_t bits,
                                   uint64_t* out_inverse) {
  uint64_t mask = bits == 32 ? uint64_t{0xFFFFFFFFu} : ~uint64_t{0};
  imm &= mask;
  uint64_t inverse = (uint64_t{0} - imm) & mask;
  if (imm <= 4095 || inverse == 0 || inverse > 4095) {
    return false;
  }
  *out_inverse = inverse;
  return true;
}

static uint64_t MaskBits(uint32_t bits) {
  return bits == 64 ? ~uint64_t{0} : ((uint64_t{1} << bits) - 1);
}

static uint64_t RotateRightBits(uint64_t value, uint32_t rotate,
                                uint32_t bits) {
  uint64_t mask = MaskBits(bits);
  value &= mask;
  rotate &= bits - 1;
  if (rotate == 0) {
    return value;
  }
  return ((value >> rotate) | (value << (bits - rotate))) & mask;
}

enum class BlueDragonCallBoundaryStoreKind {
  kNone,
  kDeadVmx,
  kDeadGpr,
  kDeadFpr,
  kLiveIn,
};

static BlueDragonCallBoundaryStoreKind GetBlueDragonCallBoundaryStoreKind(
    A64Emitter& e, const hir::Instr* instr, uint32_t offset) {
  if ((!cvars::arm64_blue_dragon_call_boundary_state_audit &&
       !cvars::arm64_blue_dragon_call_boundary_state_suppress_dead_stores) ||
      !instr ||
      e.current_guest_function() != 0x82282490) {
    return BlueDragonCallBoundaryStoreKind::kNone;
  }

  const uint32_t guest_pc = instr->GuestAddressFor();
  switch (guest_pc) {
    case 0x82282534:
      return offset == 720 ? BlueDragonCallBoundaryStoreKind::kDeadVmx
                           : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x8228253C:
      return offset == 688 ? BlueDragonCallBoundaryStoreKind::kDeadVmx
                           : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x82282540:
      return offset == 672 ? BlueDragonCallBoundaryStoreKind::kDeadVmx
                           : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x82282554:
      return offset == 704 ? BlueDragonCallBoundaryStoreKind::kDeadVmx
                           : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x82282574:
      return offset == 752 ? BlueDragonCallBoundaryStoreKind::kDeadVmx
                           : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x82282578:
      return offset == 736 ? BlueDragonCallBoundaryStoreKind::kDeadVmx
                           : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x8228257C:
      return offset == 544 ? BlueDragonCallBoundaryStoreKind::kDeadVmx
                           : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x8228252C:
      return offset == 72 ? BlueDragonCallBoundaryStoreKind::kDeadGpr
                          : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x82282544:
      return offset == 96 ? BlueDragonCallBoundaryStoreKind::kDeadGpr
                          : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x82282548:
      return offset == 88 ? BlueDragonCallBoundaryStoreKind::kDeadGpr
                          : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x82282558:
      return offset == 104 ? BlueDragonCallBoundaryStoreKind::kDeadGpr
                           : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x8228258C:
      return offset == 392 ? BlueDragonCallBoundaryStoreKind::kDeadFpr
                           : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x82282590:
      return offset == 288 ? BlueDragonCallBoundaryStoreKind::kDeadFpr
                           : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x82282550:
      return offset == 56 ? BlueDragonCallBoundaryStoreKind::kLiveIn
                          : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x82282594:
      return (offset == 296 || offset == 2628)
                 ? BlueDragonCallBoundaryStoreKind::kLiveIn
                 : BlueDragonCallBoundaryStoreKind::kNone;
    case 0x82282598:
      return offset == 16 ? BlueDragonCallBoundaryStoreKind::kLiveIn
                          : BlueDragonCallBoundaryStoreKind::kNone;
    default:
      return BlueDragonCallBoundaryStoreKind::kNone;
  }
}

static bool IsBlueDragonCallBoundaryDeadStore(
    BlueDragonCallBoundaryStoreKind kind) {
  return kind == BlueDragonCallBoundaryStoreKind::kDeadVmx ||
         kind == BlueDragonCallBoundaryStoreKind::kDeadGpr ||
         kind == BlueDragonCallBoundaryStoreKind::kDeadFpr;
}

static bool EmitBlueDragonCallBoundaryStateProbe(
    A64Emitter& e, BlueDragonCallBoundaryStoreKind kind) {
  if (kind == BlueDragonCallBoundaryStoreKind::kNone) {
    return false;
  }
  if (cvars::arm64_blue_dragon_call_boundary_state_audit) {
    switch (kind) {
      case BlueDragonCallBoundaryStoreKind::kDeadVmx:
        e.EmitAtomicIncrement64(
            e.backend()->blue_dragon_call_boundary_state_dead_count());
        e.EmitAtomicIncrement64(
            e.backend()->blue_dragon_call_boundary_state_dead_vmx_count());
        break;
      case BlueDragonCallBoundaryStoreKind::kDeadGpr:
        e.EmitAtomicIncrement64(
            e.backend()->blue_dragon_call_boundary_state_dead_count());
        e.EmitAtomicIncrement64(
            e.backend()->blue_dragon_call_boundary_state_dead_gpr_count());
        break;
      case BlueDragonCallBoundaryStoreKind::kDeadFpr:
        e.EmitAtomicIncrement64(
            e.backend()->blue_dragon_call_boundary_state_dead_count());
        e.EmitAtomicIncrement64(
            e.backend()->blue_dragon_call_boundary_state_dead_fpr_count());
        break;
      case BlueDragonCallBoundaryStoreKind::kLiveIn:
        e.EmitAtomicIncrement64(
            e.backend()->blue_dragon_call_boundary_state_live_count());
        break;
      case BlueDragonCallBoundaryStoreKind::kNone:
        break;
    }
  }
  if (!cvars::arm64_blue_dragon_call_boundary_state_suppress_dead_stores ||
      !IsBlueDragonCallBoundaryDeadStore(kind)) {
    return false;
  }
  if (cvars::arm64_blue_dragon_call_boundary_state_audit) {
    auto* backend = e.backend();
    e.EmitAtomicIncrement64(
        backend->blue_dragon_call_boundary_state_suppressed_count());
    switch (kind) {
      case BlueDragonCallBoundaryStoreKind::kDeadVmx:
        e.EmitAtomicIncrement64(
            backend->blue_dragon_call_boundary_state_suppressed_vmx_count());
        break;
      case BlueDragonCallBoundaryStoreKind::kDeadGpr:
        e.EmitAtomicIncrement64(
            backend->blue_dragon_call_boundary_state_suppressed_gpr_count());
        break;
      case BlueDragonCallBoundaryStoreKind::kDeadFpr:
        e.EmitAtomicIncrement64(
            backend->blue_dragon_call_boundary_state_suppressed_fpr_count());
        break;
      case BlueDragonCallBoundaryStoreKind::kLiveIn:
      case BlueDragonCallBoundaryStoreKind::kNone:
        break;
    }
  }
  return true;
}

enum class BlueDragonF1CarrierLoadKind {
  kNone,
  kHelperPreserved,
  kChildPreserved,
};

static BlueDragonF1CarrierLoadKind GetBlueDragonF1CarrierLoadKind(
    A64Emitter& e, const hir::Instr* instr, uint32_t offset) {
  if (!instr || e.current_guest_function() != 0x82287788 || offset != 296) {
    return BlueDragonF1CarrierLoadKind::kNone;
  }

  switch (instr->GuestAddressFor()) {
    case 0x82287798:
    case 0x82287828:
      return BlueDragonF1CarrierLoadKind::kHelperPreserved;
    case 0x82287A1C:
    case 0x82287A2C:
    case 0x82287AA4:
    case 0x82287CF8:
    case 0x82287D10:
    case 0x82287D8C:
    case 0x82287EA8:
    case 0x82287F1C:
      return BlueDragonF1CarrierLoadKind::kChildPreserved;
    default:
      return BlueDragonF1CarrierLoadKind::kNone;
  }
}

static bool EmitBlueDragonF1CarrierFastpath(
    A64Emitter& e, const hir::Instr* instr, const DReg& dest, uint32_t offset,
    BlueDragonF1CarrierLoadKind kind) {
  if (!cvars::arm64_blue_dragon_f1_carrier_fastpath ||
      kind == BlueDragonF1CarrierLoadKind::kNone || !instr ||
      !e.blue_dragon_f1_carrier_stack_slot_enabled() || offset != 296) {
    return false;
  }

  const uint32_t guest_pc = instr->GuestAddressFor();
  const uint32_t carrier_offset =
      static_cast<uint32_t>(e.blue_dragon_f1_carrier_stack_slot_offset());
  if (guest_pc == 0x82287798) {
    e.ldr(dest, ptr(e.GetContextReg(), offset));
    e.str(dest, ptr(e.sp, carrier_offset));
    if (cvars::arm64_blue_dragon_f1_carrier_audit) {
      e.EmitAtomicIncrement64(e.backend()->blue_dragon_f1_carrier_seed_count());
    }
    return true;
  }

  switch (guest_pc) {
    case 0x82287828:
    case 0x82287A1C:
    case 0x82287A2C:
    case 0x82287AA4:
    case 0x82287CF8:
    case 0x82287D10:
    case 0x82287D8C:
    case 0x82287EA8:
    case 0x82287F1C:
      e.ldr(dest, ptr(e.sp, carrier_offset));
      if (cvars::arm64_blue_dragon_f1_carrier_audit) {
        e.EmitAtomicIncrement64(
            e.backend()->blue_dragon_f1_carrier_reuse_count());
      }
      return true;
    default:
      return false;
  }
}

static void EmitBlueDragonF1CarrierAudit(
    A64Emitter& e, BlueDragonF1CarrierLoadKind kind) {
  if (kind == BlueDragonF1CarrierLoadKind::kNone) {
    return;
  }
  auto* backend = e.backend();
  e.EmitAtomicIncrement64(backend->blue_dragon_f1_carrier_total_count());
  switch (kind) {
    case BlueDragonF1CarrierLoadKind::kHelperPreserved:
      e.EmitAtomicIncrement64(
          backend->blue_dragon_f1_carrier_helper_preserved_count());
      break;
    case BlueDragonF1CarrierLoadKind::kChildPreserved:
      e.EmitAtomicIncrement64(
          backend->blue_dragon_f1_carrier_child_preserved_count());
      break;
    case BlueDragonF1CarrierLoadKind::kNone:
      break;
  }
}

static void EmitBlueDragonStateCarrierDesignLoadAudit(
    A64Emitter& e, const hir::Instr* instr, uint32_t offset) {
  if (!cvars::arm64_blue_dragon_state_carrier_design_audit || !instr ||
      e.current_guest_function() != 0x82287788) {
    return;
  }

  auto* backend = e.backend();
  const uint32_t guest_pc = instr->GuestAddressFor();
  if (offset == 296) {
    e.EmitAtomicIncrement64(backend->blue_dragon_state_carrier_f1_read_count());
    switch (guest_pc) {
      case 0x82287798:
      case 0x82287828:
        e.EmitAtomicIncrement64(
            backend->blue_dragon_state_carrier_f1_helper_read_count());
        break;
      case 0x82287A1C:
      case 0x82287A2C:
      case 0x82287AA4:
      case 0x82287CF8:
      case 0x82287D10:
      case 0x82287D8C:
      case 0x82287EA8:
      case 0x82287F1C:
        e.EmitAtomicIncrement64(
            backend->blue_dragon_state_carrier_f1_child_read_count());
        break;
      default:
        e.EmitAtomicIncrement64(
            backend->blue_dragon_state_carrier_f1_fallback_count());
        break;
    }
    return;
  }

  if (offset == 2628) {
    e.EmitAtomicIncrement64(
        backend->blue_dragon_state_carrier_fpscr_read_count());
  }
}

static void EmitBlueDragonEdgeVariantF1ReadAudit(A64Emitter& e,
                                                 const hir::Instr* instr,
                                                 uint32_t offset) {
  if (!cvars::arm64_blue_dragon_edge_variant_audit || !instr ||
      e.current_guest_function() != 0x82287788 || offset != 296) {
    return;
  }

  auto* backend = e.backend();
  std::atomic<uint64_t>* active_site_counter = nullptr;
  switch (instr->GuestAddressFor()) {
    case 0x82287798:
      active_site_counter =
          backend->blue_dragon_edge_variant_active_f1_read_site_count(0);
      break;
    case 0x82287828:
      active_site_counter =
          backend->blue_dragon_edge_variant_active_f1_read_site_count(1);
      break;
    case 0x82287A1C:
      active_site_counter =
          backend->blue_dragon_edge_variant_active_f1_read_site_count(2);
      break;
    case 0x82287A2C:
      active_site_counter =
          backend->blue_dragon_edge_variant_active_f1_read_site_count(3);
      break;
    case 0x82287AA4:
      active_site_counter =
          backend->blue_dragon_edge_variant_active_f1_read_site_count(4);
      break;
    case 0x82287CF8:
      active_site_counter =
          backend->blue_dragon_edge_variant_active_f1_read_site_count(5);
      break;
    case 0x82287D10:
      active_site_counter =
          backend->blue_dragon_edge_variant_active_f1_read_site_count(6);
      break;
    case 0x82287D8C:
      active_site_counter =
          backend->blue_dragon_edge_variant_active_f1_read_site_count(7);
      break;
    case 0x82287EA8:
      active_site_counter =
          backend->blue_dragon_edge_variant_active_f1_read_site_count(8);
      break;
    case 0x82287F1C:
      active_site_counter =
          backend->blue_dragon_edge_variant_active_f1_read_site_count(9);
      break;
    default:
      break;
  }
  auto& inactive = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();
  e.ldr(e.w11, ptr(e.GetBackendCtxReg(),
                   static_cast<uint32_t>(offsetof(
                       A64BackendContext,
                       blue_dragon_edge_variant_payload_active))));
  e.cbz(e.w11, inactive);
  e.EmitAtomicIncrement64(
      backend->blue_dragon_edge_variant_active_f1_read_count());
  if (active_site_counter) {
    e.EmitAtomicIncrement64(active_site_counter);
  }
  e.b(done);
  e.L(inactive);
  e.EmitAtomicIncrement64(
      backend->blue_dragon_edge_variant_inactive_f1_read_count());
  e.L(done);
}

static void EmitBlueDragonEdgePayloadStorageF1ReadAudit(
    A64Emitter& e, const hir::Instr* instr, uint32_t offset) {
  if (!cvars::arm64_blue_dragon_edge_payload_storage_audit || !instr ||
      e.current_guest_function() != 0x82287788 || offset != 296) {
    return;
  }

  auto* backend = e.backend();
  auto& inactive = e.NewCachedLabel();
  auto& after_kill = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();
  e.ldr(e.w11, ptr(e.GetBackendCtxReg(),
                   static_cast<uint32_t>(offsetof(
                       A64BackendContext,
                       blue_dragon_edge_payload_storage_active))));
  e.cbz(e.w11, inactive);
  e.EmitAtomicIncrement64(
      backend->blue_dragon_edge_payload_storage_f1_active_read_covered_count());
  e.ldr(e.w11, ptr(e.GetBackendCtxReg(),
                   static_cast<uint32_t>(offsetof(
                       A64BackendContext,
                       blue_dragon_edge_payload_storage_lifetime_live))));
  e.cbz(e.w11, after_kill);
  e.EmitAtomicIncrement64(
      backend
          ->blue_dragon_edge_payload_storage_lifetime_f1_read_before_kill_count());
  e.b(done);
  e.L(after_kill);
  e.EmitAtomicIncrement64(
      backend
          ->blue_dragon_edge_payload_storage_lifetime_f1_read_after_kill_count());
  e.b(done);
  e.L(inactive);
  e.EmitAtomicIncrement64(
      backend->blue_dragon_edge_payload_storage_f1_inactive_read_count());
  e.L(done);
}

static void EmitBlueDragonEdgePayloadStorageR3ReadAudit(
    A64Emitter& e, const hir::Instr* instr, uint32_t offset) {
  if (!cvars::arm64_blue_dragon_edge_payload_storage_audit || !instr ||
      e.current_guest_function() != 0x82287788 || offset != 56) {
    return;
  }

  auto* backend = e.backend();
  auto& inactive = e.NewCachedLabel();
  auto& after_kill = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();
  e.ldr(e.w11, ptr(e.GetBackendCtxReg(),
                   static_cast<uint32_t>(offsetof(
                       A64BackendContext,
                       blue_dragon_edge_payload_storage_active))));
  e.cbz(e.w11, inactive);
  e.EmitAtomicIncrement64(
      backend->blue_dragon_edge_payload_storage_r3_active_read_covered_count());
  e.ldr(e.w11, ptr(e.GetBackendCtxReg(),
                   static_cast<uint32_t>(offsetof(
                       A64BackendContext,
                       blue_dragon_edge_payload_storage_lifetime_live))));
  e.cbz(e.w11, after_kill);
  e.EmitAtomicIncrement64(
      backend
          ->blue_dragon_edge_payload_storage_lifetime_r3_read_before_kill_count());
  e.b(done);
  e.L(after_kill);
  e.EmitAtomicIncrement64(
      backend
          ->blue_dragon_edge_payload_storage_lifetime_r3_read_after_kill_count());
  e.b(done);
  e.L(inactive);
  e.EmitAtomicIncrement64(
      backend->blue_dragon_edge_payload_storage_r3_inactive_read_count());
  e.L(done);
}

static bool IsBlueDragonEdgePayloadStorageFpscrExternalStorePc(
    uint32_t guest_pc) {
  switch (guest_pc) {
    case 0x82287E6C:
    case 0x822881FC:
      return true;
    default:
      return false;
  }
}

static void EmitBlueDragonEdgePayloadStorageActiveCounter(
    A64Emitter& e, std::atomic<uint64_t>* counter,
    std::atomic<uint64_t>* secondary_counter = nullptr) {
  auto& inactive = e.NewCachedLabel();
  e.ldr(e.w11, ptr(e.GetBackendCtxReg(),
                   static_cast<uint32_t>(offsetof(
                       A64BackendContext,
                       blue_dragon_edge_payload_storage_active))));
  e.cbz(e.w11, inactive);
  e.EmitAtomicIncrement64(counter);
  if (secondary_counter) {
    e.EmitAtomicIncrement64(secondary_counter);
  }
  e.L(inactive);
}

static void EmitBlueDragonEdgePayloadStorageLifetimeSplitCounter(
    A64Emitter& e, std::atomic<uint64_t>* before_kill_counter,
    std::atomic<uint64_t>* after_kill_counter) {
  auto& inactive = e.NewCachedLabel();
  auto& after_kill = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();
  e.ldr(e.w11, ptr(e.GetBackendCtxReg(),
                   static_cast<uint32_t>(offsetof(
                       A64BackendContext,
                       blue_dragon_edge_payload_storage_active))));
  e.cbz(e.w11, inactive);
  e.ldr(e.w11, ptr(e.GetBackendCtxReg(),
                   static_cast<uint32_t>(offsetof(
                       A64BackendContext,
                       blue_dragon_edge_payload_storage_lifetime_live))));
  e.cbz(e.w11, after_kill);
  e.EmitAtomicIncrement64(before_kill_counter);
  e.b(done);
  e.L(after_kill);
  e.EmitAtomicIncrement64(after_kill_counter);
  e.b(done);
  e.L(inactive);
  e.L(done);
}

static void EmitBlueDragonEdgePayloadStorageFirstKill(
    A64Emitter& e, std::atomic<uint64_t>* first_kill_counter) {
  auto& done = e.NewCachedLabel();
  e.ldr(e.w11, ptr(e.GetBackendCtxReg(),
                   static_cast<uint32_t>(offsetof(
                       A64BackendContext,
                       blue_dragon_edge_payload_storage_active))));
  e.cbz(e.w11, done);
  e.ldr(e.w11, ptr(e.GetBackendCtxReg(),
                   static_cast<uint32_t>(offsetof(
                       A64BackendContext,
                       blue_dragon_edge_payload_storage_lifetime_live))));
  e.cbz(e.w11, done);
  e.EmitAtomicIncrement64(first_kill_counter);
  e.str(e.wzr, ptr(e.GetBackendCtxReg(),
                   static_cast<uint32_t>(offsetof(
                       A64BackendContext,
                       blue_dragon_edge_payload_storage_lifetime_live))));
  e.L(done);
}

static void EmitBlueDragonEdgePayloadStorageStoreAudit(
    A64Emitter& e, const hir::Instr* instr, uint32_t offset) {
  if (!cvars::arm64_blue_dragon_edge_payload_storage_audit || !instr) {
    return;
  }

  auto* backend = e.backend();
  const uint32_t guest_function = e.current_guest_function();
  const uint32_t guest_pc = instr->GuestAddressFor();
  if (guest_function == 0x82282490) {
    if (guest_pc == 0x82282594 && offset == 296) {
      e.EmitAtomicIncrement64(
          backend->blue_dragon_edge_payload_storage_f1_seed_candidate_count());
    } else if (guest_pc == 0x82282594 && offset == 2628) {
      e.EmitAtomicIncrement64(backend
                                  ->blue_dragon_edge_payload_storage_fpscr_seed_candidate_count());
    } else if (guest_pc == 0x82282550 && offset == 56) {
      e.EmitAtomicIncrement64(
          backend->blue_dragon_edge_payload_storage_r3_seed_candidate_count());
    }
    return;
  }

  if (guest_function != 0x82287788) {
    return;
  }
  if (offset == 2628) {
    EmitBlueDragonEdgePayloadStorageActiveCounter(
        e, backend->blue_dragon_edge_payload_storage_fpscr_dirty_write_count(),
        IsBlueDragonEdgePayloadStorageFpscrExternalStorePc(guest_pc)
            ? backend->blue_dragon_edge_payload_storage_external_visibility_count()
            : nullptr);
    if (IsBlueDragonEdgePayloadStorageFpscrExternalStorePc(guest_pc)) {
      EmitBlueDragonEdgePayloadStorageFirstKill(
          e, backend
                 ->blue_dragon_edge_payload_storage_lifetime_first_kill_external_visibility_count());
    }
  } else if (offset == 56) {
    EmitBlueDragonEdgePayloadStorageActiveCounter(
        e, backend->blue_dragon_edge_payload_storage_r3_mutable_write_count());
    EmitBlueDragonEdgePayloadStorageLifetimeSplitCounter(
        e,
        backend
            ->blue_dragon_edge_payload_storage_lifetime_r3_write_before_kill_count(),
        backend
            ->blue_dragon_edge_payload_storage_lifetime_r3_write_after_kill_count());
  }
}

static void EmitBlueDragonStateCarrierDesignStoreAudit(
    A64Emitter& e, const hir::Instr* instr, uint32_t offset) {
  if (!cvars::arm64_blue_dragon_state_carrier_design_audit || !instr) {
    return;
  }

  auto* backend = e.backend();
  const uint32_t guest_function = e.current_guest_function();
  const uint32_t guest_pc = instr->GuestAddressFor();
  if (guest_function == 0x82282490 && guest_pc == 0x82282594) {
    if (offset == 296) {
      e.EmitAtomicIncrement64(
          backend->blue_dragon_state_carrier_f1_seed_count());
    } else if (offset == 2628) {
      e.EmitAtomicIncrement64(
          backend->blue_dragon_state_carrier_fpscr_seed_count());
    }
    return;
  }

  if (guest_function != 0x82287788) {
    return;
  }
  if (offset == 296) {
    e.EmitAtomicIncrement64(
        backend->blue_dragon_state_carrier_f1_fallback_count());
  } else if (offset == 2628) {
    e.EmitAtomicIncrement64(
        backend->blue_dragon_state_carrier_fpscr_dirty_write_count());
  }
}

enum class BlueDragonFpscrCfgStoreKind {
  kNone,
  kCfgTransition,
  kExternalTransition,
  kUnclassified,
};

static BlueDragonFpscrCfgStoreKind GetBlueDragonFpscrCfgStoreKind(
    A64Emitter& e, const hir::Instr* instr, uint32_t offset) {
  if (!cvars::arm64_blue_dragon_fpscr_cfg_writeback_audit || !instr ||
      e.current_guest_function() != 0x82287788 || offset != 2628) {
    return BlueDragonFpscrCfgStoreKind::kNone;
  }

  switch (instr->GuestAddressFor()) {
    case 0x82287A1C:
    case 0x82287A2C:
    case 0x82287B08:
    case 0x82287B14:
    case 0x82287B24:
    case 0x82287B30:
    case 0x82287CF8:
    case 0x82287D10:
    case 0x82287DE0:
    case 0x82287E08:
    case 0x82287E1C:
    case 0x82287E30:
    case 0x82287E40:
    case 0x82287E54:
    case 0x82287E60:
    case 0x82287F1C:
    case 0x822880A4:
    case 0x822880BC:
    case 0x82288194:
    case 0x822881A0:
    case 0x822881A8:
    case 0x822881D8:
    case 0x822881E4:
    case 0x822881EC:
      return BlueDragonFpscrCfgStoreKind::kCfgTransition;
    case 0x82287E6C:
    case 0x822881FC:
      return BlueDragonFpscrCfgStoreKind::kExternalTransition;
    default:
      return BlueDragonFpscrCfgStoreKind::kUnclassified;
  }
}

static void EmitBlueDragonFpscrCfgWritebackLoadAudit(
    A64Emitter& e, const hir::Instr* instr, uint32_t offset) {
  if (!cvars::arm64_blue_dragon_fpscr_cfg_writeback_audit || !instr ||
      e.current_guest_function() != 0x82287788 || offset != 2628) {
    return;
  }
  e.EmitAtomicIncrement64(e.backend()->blue_dragon_fpscr_cfg_load_count());
}

static void EmitBlueDragonFpscrCfgWritebackStoreAudit(
    A64Emitter& e, BlueDragonFpscrCfgStoreKind kind) {
  if (kind == BlueDragonFpscrCfgStoreKind::kNone) {
    return;
  }
  auto* backend = e.backend();
  e.EmitAtomicIncrement64(backend->blue_dragon_fpscr_cfg_store_count());
  switch (kind) {
    case BlueDragonFpscrCfgStoreKind::kCfgTransition:
      e.EmitAtomicIncrement64(
          backend->blue_dragon_fpscr_cfg_transition_store_count());
      break;
    case BlueDragonFpscrCfgStoreKind::kExternalTransition:
      e.EmitAtomicIncrement64(
          backend->blue_dragon_fpscr_cfg_external_store_count());
      break;
    case BlueDragonFpscrCfgStoreKind::kUnclassified:
      e.EmitAtomicIncrement64(
          backend->blue_dragon_fpscr_cfg_unclassified_store_count());
      break;
    case BlueDragonFpscrCfgStoreKind::kNone:
      break;
  }
}

static bool IsRotatedRunOfOnes(uint64_t value, uint32_t bits) {
  uint64_t mask = MaskBits(bits);
  value &= mask;
  if (value == 0 || value == mask) {
    return false;
  }
  for (uint32_t rotate = 0; rotate < bits; ++rotate) {
    uint64_t rotated = RotateRightBits(value, rotate, bits);
    if ((rotated & (rotated + 1)) == 0) {
      return true;
    }
  }
  return false;
}

static bool IsA64LogicalImmediate(uint64_t imm, uint32_t reg_bits) {
  uint64_t reg_mask = MaskBits(reg_bits);
  imm &= reg_mask;
  if (imm == 0 || imm == reg_mask) {
    return false;
  }
  for (uint32_t bits = 2; bits <= reg_bits; bits <<= 1) {
    uint64_t mask = MaskBits(bits);
    uint64_t pattern = imm & mask;
    uint64_t repeated = 0;
    for (uint32_t shift = 0; shift < reg_bits; shift += bits) {
      repeated |= pattern << shift;
    }
    if (repeated == imm && IsRotatedRunOfOnes(pattern, bits)) {
      return true;
    }
  }
  return false;
}

enum class LogicalImmOp {
  kAnd,
  kOrr,
  kEor,
};

static void MaybeAuditLogicalImmediate(A64Emitter& e, const char* opcode,
                                       uint32_t reg_bits, uint64_t imm,
                                       const char* constant_side,
                                       bool logical_imm, const char* action) {
  if (!cvars::arm64_immediate_lowering_audit) {
    return;
  }
  uint32_t function = e.current_guest_function();
  uint32_t filter = cvars::arm64_immediate_lowering_audit_function;
  if (filter != 0 && function != filter) {
    return;
  }
  uint32_t log_index = a64_immediate_lowering_audit_count.fetch_add(1);
  if (log_index >= cvars::arm64_immediate_lowering_audit_budget) {
    return;
  }
  uint64_t masked = imm & MaskBits(reg_bits);
  XELOGW(
      "A64 immediate lowering audit {:03}: fn {:08X} op {} width {} "
      "const_side {} imm 0x{:016X} masked 0x{:016X} logical_imm {} action {}",
      log_index + 1, function, opcode, reg_bits, constant_side, imm, masked,
      logical_imm ? "yes" : "no", action);
}

static void EmitLogicalImm32(A64Emitter& e, LogicalImmOp op,
                             const char* opcode, const WReg& dest,
                             const WReg& src, uint32_t imm,
                             const char* constant_side) {
  if (imm == 0) {
    switch (op) {
      case LogicalImmOp::kAnd:
        MaybeAuditLogicalImmediate(e, opcode, 32, imm, constant_side, false,
                                   "zero");
        e.mov(dest, uint64_t{0});
        return;
      case LogicalImmOp::kOrr:
      case LogicalImmOp::kEor:
        MaybeAuditLogicalImmediate(e, opcode, 32, imm, constant_side, false,
                                   "identity");
        e.mov(dest, src);
        return;
    }
  }
  bool logical_imm = IsA64LogicalImmediate(imm, 32);
  MaybeAuditLogicalImmediate(e, opcode, 32, imm, constant_side, logical_imm,
                             logical_imm ? "logical-imm" : "mov+reg");
  switch (op) {
    case LogicalImmOp::kAnd:
      e.and_imm(dest, src, imm, e.w0);
      return;
    case LogicalImmOp::kOrr:
      e.orr_imm(dest, src, imm, e.w0);
      return;
    case LogicalImmOp::kEor:
      e.eor_imm(dest, src, imm, e.w0);
      return;
  }
}

static void EmitLogicalImm64(A64Emitter& e, LogicalImmOp op,
                             const char* opcode, const XReg& dest,
                             const XReg& src, uint64_t imm,
                             const char* constant_side) {
  if (imm == 0) {
    switch (op) {
      case LogicalImmOp::kAnd:
        MaybeAuditLogicalImmediate(e, opcode, 64, imm, constant_side, false,
                                   "zero");
        e.mov(dest, uint64_t{0});
        return;
      case LogicalImmOp::kOrr:
      case LogicalImmOp::kEor:
        MaybeAuditLogicalImmediate(e, opcode, 64, imm, constant_side, false,
                                   "identity");
        e.mov(dest, src);
        return;
    }
  }
  bool logical_imm = IsA64LogicalImmediate(imm, 64);
  MaybeAuditLogicalImmediate(e, opcode, 64, imm, constant_side, logical_imm,
                             logical_imm ? "logical-imm" : "mov+reg");
  if (logical_imm) {
    switch (op) {
      case LogicalImmOp::kAnd:
        e.and_(dest, src, imm);
        return;
      case LogicalImmOp::kOrr:
        e.orr(dest, src, imm);
        return;
      case LogicalImmOp::kEor:
        e.eor(dest, src, imm);
        return;
    }
  }

  e.mov(e.x0, imm);
  switch (op) {
    case LogicalImmOp::kAnd:
      e.and_(dest, src, e.x0);
      return;
    case LogicalImmOp::kOrr:
      e.orr(dest, src, e.x0);
      return;
    case LogicalImmOp::kEor:
      e.eor(dest, src, e.x0);
      return;
  }
}

static bool TryEmitAddI64WrappedSmallImmediate(A64Emitter& e, const XReg& dest,
                                               const XReg& src,
                                               uint64_t imm) {
  if (!cvars::arm64_add_i64_wrapped_imm_fastpath) {
    return false;
  }
  uint32_t filter = cvars::arm64_add_i64_wrapped_imm_fastpath_function;
  if (filter != 0 && e.current_guest_function() != filter) {
    return false;
  }
  uint64_t inverse = 0;
  if (!HasWrappedSmallInverse(imm, 64, &inverse)) {
    return false;
  }
  e.sub(dest, src, static_cast<uint32_t>(inverse));
  return true;
}

static void MaybeAuditWrappedAddSubImm(A64Emitter& e, const char* opcode,
                                       uint32_t bits, uint64_t imm,
                                       const char* constant_side,
                                       const char* would_emit) {
  if (!cvars::arm64_add_sub_imm_audit) {
    return;
  }
  uint64_t inverse = 0;
  if (!HasWrappedSmallInverse(imm, bits, &inverse)) {
    return;
  }
  uint32_t function = e.current_guest_function();
  uint32_t filter = cvars::arm64_add_sub_imm_audit_function;
  if (filter != 0 && function != filter) {
    return;
  }
  uint32_t log_index = a64_add_sub_imm_audit_count.fetch_add(1);
  if (log_index >= cvars::arm64_add_sub_imm_audit_budget) {
    return;
  }
  XELOGW(
      "A64 ADD/SUB immediate audit {:03}: fn {:08X} op {} width {} "
      "const_side {} imm 0x{:016X} inverse 0x{:X} current mov+reg would {}",
      log_index + 1, function, opcode, bits, constant_side, imm, inverse,
      would_emit);
}

template <typename T>
static bool IsUnsignedZeroConstant(const T& op) {
  if (!op.is_constant) {
    return false;
  }
  using ConstantType =
      typename std::make_unsigned<std::remove_cv_t<decltype(op.constant())>>::
          type;
  return static_cast<ConstantType>(op.constant()) == 0;
}

template <typename SRC1, typename SRC2>
static bool TryEmitUnsignedZeroCompareFold(A64Emitter& e, hir::Opcode opcode,
                                           const WReg& dest, const SRC1& src1,
                                           const SRC2& src2) {
  if (IsUnsignedZeroConstant(src2)) {
    if (opcode == OPCODE_COMPARE_ULT) {
      e.mov(dest, uint64_t{0});
      return true;
    }
    if (opcode == OPCODE_COMPARE_UGE) {
      e.mov(dest, uint64_t{1});
      return true;
    }
  }
  if (IsUnsignedZeroConstant(src1)) {
    if (opcode == OPCODE_COMPARE_UGT) {
      e.mov(dest, uint64_t{0});
      return true;
    }
    if (opcode == OPCODE_COMPARE_ULE) {
      e.mov(dest, uint64_t{1});
      return true;
    }
  }
  return false;
}

}  // namespace

// ============================================================================
// OPCODE_COMMENT
// ============================================================================
struct COMMENT : Sequence<COMMENT, I<OPCODE_COMMENT, VoidOp, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (IsTracingInstr()) {
      auto str = reinterpret_cast<const char*>(i.src1.value);
      auto str_copy = strdup(str);
      e.mov(e.x1, reinterpret_cast<uint64_t>(str_copy));
      e.CallNative(reinterpret_cast<void*>(TraceString));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_COMMENT, COMMENT);

// ============================================================================
// OPCODE_NOP
// ============================================================================
struct NOP : Sequence<NOP, I<OPCODE_NOP, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) { e.nop(); }
};
EMITTER_OPCODE_TABLE(OPCODE_NOP, NOP);

// ============================================================================
// OPCODE_SOURCE_OFFSET
// ============================================================================
struct SOURCE_OFFSET
    : Sequence<SOURCE_OFFSET, I<OPCODE_SOURCE_OFFSET, VoidOp, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.MarkSourceOffset(i.instr);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SOURCE_OFFSET, SOURCE_OFFSET);

// ============================================================================
// OPCODE_CONTEXT_BARRIER
// ============================================================================
struct CONTEXT_BARRIER
    : Sequence<CONTEXT_BARRIER, I<OPCODE_CONTEXT_BARRIER, VoidOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (cvars::arm64_blue_dragon_edge_payload_storage_audit &&
        e.current_guest_function() == 0x82287788) {
      EmitBlueDragonEdgePayloadStorageActiveCounter(
          e, e.backend()
                 ->blue_dragon_edge_payload_storage_context_barrier_count());
      EmitBlueDragonEdgePayloadStorageFirstKill(
          e, e.backend()
                 ->blue_dragon_edge_payload_storage_lifetime_first_kill_context_barrier_count());
    }
    // No-op on ARM64 (context is always in x20).
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CONTEXT_BARRIER, CONTEXT_BARRIER);

// ============================================================================
// OPCODE_ASSIGN
// ============================================================================
struct ASSIGN_I8 : Sequence<ASSIGN_I8, I<OPCODE_ASSIGN, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    } else {
      e.mov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_I16 : Sequence<ASSIGN_I16, I<OPCODE_ASSIGN, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
    } else {
      e.mov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_I32 : Sequence<ASSIGN_I32, I<OPCODE_ASSIGN, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
    } else {
      e.mov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_I64 : Sequence<ASSIGN_I64, I<OPCODE_ASSIGN, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
    } else {
      e.mov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_F32 : Sequence<ASSIGN_F32, I<OPCODE_ASSIGN, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      // Load constant float via GPR.
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(i.dest, e.w0);
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_F64 : Sequence<ASSIGN_F64, I<OPCODE_ASSIGN, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(i.dest, e.x0);
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
struct ASSIGN_V128 : Sequence<ASSIGN_V128, I<OPCODE_ASSIGN, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      LoadV128Const(e, i.dest.reg().getIdx(), i.src1.constant());
    } else {
      // mov vD.16b, vS.16b (via ORR trick: orr vD.16b, vS.16b, vS.16b)
      auto src_vreg = VReg(i.src1.reg().getIdx());
      auto dst_vreg = VReg(i.dest.reg().getIdx());
      e.orr(dst_vreg.b16, src_vreg.b16, src_vreg.b16);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ASSIGN, ASSIGN_I8, ASSIGN_I16, ASSIGN_I32,
                     ASSIGN_I64, ASSIGN_F32, ASSIGN_F64, ASSIGN_V128);

// ============================================================================
// OPCODE_LOAD_CONTEXT
// ============================================================================
struct LOAD_CONTEXT_I8
    : Sequence<LOAD_CONTEXT_I8, I<OPCODE_LOAD_CONTEXT, I8Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // ldrb wD, [x20, #offset]
    auto offset = static_cast<uint32_t>(i.src1.value);
    e.ldrb(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_I16
    : Sequence<LOAD_CONTEXT_I16, I<OPCODE_LOAD_CONTEXT, I16Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    e.ldrh(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_I32
    : Sequence<LOAD_CONTEXT_I32, I<OPCODE_LOAD_CONTEXT, I32Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    EmitBlueDragonStateCarrierDesignLoadAudit(e, i.instr, offset);
    EmitBlueDragonEdgePayloadStorageR3ReadAudit(e, i.instr, offset);
    EmitBlueDragonFpscrCfgWritebackLoadAudit(e, i.instr, offset);
    e.ldr(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_I64
    : Sequence<LOAD_CONTEXT_I64, I<OPCODE_LOAD_CONTEXT, I64Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    EmitBlueDragonEdgePayloadStorageR3ReadAudit(e, i.instr, offset);
    e.ldr(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_F32
    : Sequence<LOAD_CONTEXT_F32, I<OPCODE_LOAD_CONTEXT, F32Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    e.ldr(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_F64
    : Sequence<LOAD_CONTEXT_F64, I<OPCODE_LOAD_CONTEXT, F64Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    EmitBlueDragonStateCarrierDesignLoadAudit(e, i.instr, offset);
    const auto f1_carrier_kind =
        GetBlueDragonF1CarrierLoadKind(e, i.instr, offset);
    EmitBlueDragonEdgeVariantF1ReadAudit(e, i.instr, offset);
    EmitBlueDragonEdgePayloadStorageF1ReadAudit(e, i.instr, offset);
    EmitBlueDragonF1CarrierAudit(e, f1_carrier_kind);
    if (EmitBlueDragonF1CarrierFastpath(e, i.instr, i.dest, offset,
                                        f1_carrier_kind)) {
      return;
    }
    e.ldr(i.dest, ptr(e.GetContextReg(), offset));
  }
};
struct LOAD_CONTEXT_V128
    : Sequence<LOAD_CONTEXT_V128, I<OPCODE_LOAD_CONTEXT, V128Op, OffsetOp>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    e.ldr(i.dest, ptr(e.GetContextReg(), offset));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_CONTEXT, LOAD_CONTEXT_I8, LOAD_CONTEXT_I16,
                     LOAD_CONTEXT_I32, LOAD_CONTEXT_I64, LOAD_CONTEXT_F32,
                     LOAD_CONTEXT_F64, LOAD_CONTEXT_V128);

// ============================================================================
// OPCODE_STORE_CONTEXT
// ============================================================================
struct STORE_CONTEXT_I8
    : Sequence<STORE_CONTEXT_I8,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    if (EmitBlueDragonCallBoundaryStateProbe(
            e, GetBlueDragonCallBoundaryStoreKind(e, i.instr, offset))) {
      return;
    }
    if (i.src2.is_constant) {
      if ((i.src2.constant() & 0xFF) == 0) {
        e.strb(e.wzr, ptr(e.GetContextReg(), offset));
      } else {
        e.mov(e.w0, static_cast<uint64_t>(i.src2.constant() & 0xFF));
        e.strb(e.w0, ptr(e.GetContextReg(), offset));
      }
    } else {
      e.strb(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_I16
    : Sequence<STORE_CONTEXT_I16,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    if (EmitBlueDragonCallBoundaryStateProbe(
            e, GetBlueDragonCallBoundaryStoreKind(e, i.instr, offset))) {
      return;
    }
    if (i.src2.is_constant) {
      if ((i.src2.constant() & 0xFFFF) == 0) {
        e.strh(e.wzr, ptr(e.GetContextReg(), offset));
      } else {
        e.mov(e.w0, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
        e.strh(e.w0, ptr(e.GetContextReg(), offset));
      }
    } else {
      e.strh(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_I32
    : Sequence<STORE_CONTEXT_I32,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    EmitBlueDragonStateCarrierDesignStoreAudit(e, i.instr, offset);
    EmitBlueDragonEdgePayloadStorageStoreAudit(e, i.instr, offset);
    EmitBlueDragonFpscrCfgWritebackStoreAudit(
        e, GetBlueDragonFpscrCfgStoreKind(e, i.instr, offset));
    if (EmitBlueDragonCallBoundaryStateProbe(
            e, GetBlueDragonCallBoundaryStoreKind(e, i.instr, offset))) {
      return;
    }
    if (i.src2.is_constant) {
      if (i.src2.constant() == 0) {
        e.str(e.wzr, ptr(e.GetContextReg(), offset));
      } else {
        e.mov(e.w0,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
        e.str(e.w0, ptr(e.GetContextReg(), offset));
      }
    } else {
      e.str(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_I64
    : Sequence<STORE_CONTEXT_I64,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    EmitBlueDragonEdgePayloadStorageStoreAudit(e, i.instr, offset);
    if (EmitBlueDragonCallBoundaryStateProbe(
            e, GetBlueDragonCallBoundaryStoreKind(e, i.instr, offset))) {
      return;
    }
    if (i.src2.is_constant) {
      if (i.src2.constant() == 0) {
        e.str(e.xzr, ptr(e.GetContextReg(), offset));
      } else {
        e.mov(e.x0, static_cast<uint64_t>(i.src2.constant()));
        e.str(e.x0, ptr(e.GetContextReg(), offset));
      }
    } else {
      e.str(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_F32
    : Sequence<STORE_CONTEXT_F32,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    if (EmitBlueDragonCallBoundaryStateProbe(
            e, GetBlueDragonCallBoundaryStoreKind(e, i.instr, offset))) {
      return;
    }
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.str(e.w0, ptr(e.GetContextReg(), offset));
    } else {
      e.str(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_F64
    : Sequence<STORE_CONTEXT_F64,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    EmitBlueDragonStateCarrierDesignStoreAudit(e, i.instr, offset);
    EmitBlueDragonEdgePayloadStorageStoreAudit(e, i.instr, offset);
    if (EmitBlueDragonCallBoundaryStateProbe(
            e, GetBlueDragonCallBoundaryStoreKind(e, i.instr, offset))) {
      return;
    }
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.str(e.x0, ptr(e.GetContextReg(), offset));
    } else {
      e.str(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
struct STORE_CONTEXT_V128
    : Sequence<STORE_CONTEXT_V128,
               I<OPCODE_STORE_CONTEXT, VoidOp, OffsetOp, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto offset = static_cast<uint32_t>(i.src1.value);
    if (EmitBlueDragonCallBoundaryStateProbe(
            e, GetBlueDragonCallBoundaryStoreKind(e, i.instr, offset))) {
      return;
    }
    if (i.src2.is_constant) {
      LoadV128Const(e, 0, i.src2.constant());
      e.str(QReg(0), ptr(e.GetContextReg(), offset));
    } else {
      e.str(i.src2, ptr(e.GetContextReg(), offset));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_CONTEXT, STORE_CONTEXT_I8, STORE_CONTEXT_I16,
                     STORE_CONTEXT_I32, STORE_CONTEXT_I64, STORE_CONTEXT_F32,
                     STORE_CONTEXT_F64, STORE_CONTEXT_V128);

// ============================================================================
// OPCODE_ADD (Integer)
// ============================================================================
struct ADD_I8 : Sequence<ADD_I8, I<OPCODE_ADD, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() + i.src2.constant()) & 0xFF));
    } else if (i.src2.is_constant) {
      e.add(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0xFF));
    } else if (i.src1.is_constant) {
      e.add(i.dest, i.src2, static_cast<uint32_t>(i.src1.constant() & 0xFF));
    } else {
      e.add(i.dest, i.src1, i.src2);
    }
  }
};
struct ADD_I16 : Sequence<ADD_I16, I<OPCODE_ADD, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() + i.src2.constant()) & 0xFFFF));
    } else if (i.src2.is_constant) {
      uint32_t imm = static_cast<uint32_t>(i.src2.constant() & 0xFFFF);
      if (imm <= 4095) {
        e.add(i.dest, i.src1, imm);
      } else {
        e.mov(e.w0, static_cast<uint64_t>(imm));
        e.add(i.dest, i.src1, e.w0);
      }
    } else if (i.src1.is_constant) {
      uint32_t imm = static_cast<uint32_t>(i.src1.constant() & 0xFFFF);
      if (imm <= 4095) {
        e.add(i.dest, i.src2, imm);
      } else {
        e.mov(e.w0, static_cast<uint64_t>(imm));
        e.add(i.dest, i.src2, e.w0);
      }
    } else {
      e.add(i.dest, i.src1, i.src2);
    }
  }
};
struct ADD_I32 : Sequence<ADD_I32, I<OPCODE_ADD, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() + i.src2.constant())));
    } else if (i.src2.is_constant) {
      uint32_t imm = static_cast<uint32_t>(i.src2.constant());
      MaybeAuditWrappedAddSubImm(e, "ADD_I32", 32, imm, "src2", "SUB #inverse");
      if (imm <= 4095) {
        e.add(i.dest, i.src1, imm);
      } else {
        e.mov(e.w0, static_cast<uint64_t>(imm));
        e.add(i.dest, i.src1, e.w0);
      }
    } else if (i.src1.is_constant) {
      uint32_t imm = static_cast<uint32_t>(i.src1.constant());
      MaybeAuditWrappedAddSubImm(e, "ADD_I32", 32, imm, "src1", "SUB #inverse");
      if (imm <= 4095) {
        e.add(i.dest, i.src2, imm);
      } else {
        e.mov(e.w0, static_cast<uint64_t>(imm));
        e.add(i.dest, i.src2, e.w0);
      }
    } else {
      e.add(i.dest, i.src1, i.src2);
    }
  }
};
struct ADD_I64 : Sequence<ADD_I64, I<OPCODE_ADD, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() + i.src2.constant()));
    } else if (i.src2.is_constant) {
      uint64_t imm = static_cast<uint64_t>(i.src2.constant());
      MaybeAuditWrappedAddSubImm(e, "ADD_I64", 64, imm, "src2", "SUB #inverse");
      if (TryEmitAddI64WrappedSmallImmediate(e, i.dest, i.src1, imm)) {
        return;
      }
      if (imm <= 4095) {
        e.add(i.dest, i.src1, static_cast<uint32_t>(imm));
      } else {
        e.mov(e.x0, imm);
        e.add(i.dest, i.src1, e.x0);
      }
    } else if (i.src1.is_constant) {
      uint64_t imm = static_cast<uint64_t>(i.src1.constant());
      MaybeAuditWrappedAddSubImm(e, "ADD_I64", 64, imm, "src1", "SUB #inverse");
      if (TryEmitAddI64WrappedSmallImmediate(e, i.dest, i.src2, imm)) {
        return;
      }
      if (imm <= 4095) {
        e.add(i.dest, i.src2, static_cast<uint32_t>(imm));
      } else {
        e.mov(e.x0, imm);
        e.add(i.dest, i.src2, e.x0);
      }
    } else {
      e.add(i.dest, i.src1, i.src2);
    }
  }
};
// NaN canonicalization helpers.
// PPC NaN selection for 2-operand FP ops (add, sub, mul, div):
// First NaN by operand position wins, quieted if SNaN.
// If no input is NaN, use hardware; generated NaN becomes PPC default QNaN.
// ARM64 may propagate a different NaN than PPC's positional rule, so NaN
// inputs are handled entirely in software.
enum class FpBinOp { Add, Sub, Mul, Div };

static void EmitFpBinOpWithPpcNan_F32(A64Emitter& e, SReg dest, SReg s1,
                                      SReg s2, FpBinOp op) {
  // Ensure FPU FPCR (no flush-to-zero) for scalar operations.
  e.ChangeFpcrMode(FPCRMode::Fpu);
  auto& nan_path = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();

  // Check if either input is NaN. fccmp sets NZCV from immediate if the
  // condition is false (i.e. s1 was already NaN), preserving V=1.
  e.fcmp(s1, s1);
  e.fccmp(s2, s2, 0b0001, VC);
  e.b(VS, nan_path);

  // Fast path: no NaN input — hardware op.
  switch (op) {
    case FpBinOp::Add:
      e.fadd(dest, s1, s2);
      break;
    case FpBinOp::Sub:
      e.fsub(dest, s1, s2);
      break;
    case FpBinOp::Mul:
      e.fmul(dest, s1, s2);
      break;
    case FpBinOp::Div:
      e.fdiv(dest, s1, s2);
      break;
  }
  e.fcmp(dest, dest);
  e.b(VC, done);
  e.mov(e.w0, static_cast<uint64_t>(0xFFC00000u));
  e.fmov(dest, e.w0);
  e.b(done);

  // Slow path: first NaN by position wins, quiet if SNaN.
  e.L(nan_path);
  auto& s1_not_nan = e.NewCachedLabel();
  e.fcmp(s1, s1);
  e.b(VC, s1_not_nan);
  e.fmov(e.w0, s1);
  e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
  e.fmov(dest, e.w0);
  e.b(done);
  e.L(s1_not_nan);
  e.fmov(e.w0, s2);
  e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
  e.fmov(dest, e.w0);

  e.L(done);
}

static void EmitFpBinOpWithPpcNan_F64(A64Emitter& e, DReg dest, DReg s1,
                                      DReg s2, FpBinOp op) {
  e.ChangeFpcrMode(FPCRMode::Fpu);
  auto& nan_path = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();

  // Check if either input is NaN. fccmp sets NZCV from immediate if the
  // condition is false (i.e. s1 was already NaN), preserving V=1.
  e.fcmp(s1, s1);
  e.fccmp(s2, s2, 0b0001, VC);
  e.b(VS, nan_path);

  // Fast path: no NaN input — hardware op.
  switch (op) {
    case FpBinOp::Add:
      e.fadd(dest, s1, s2);
      break;
    case FpBinOp::Sub:
      e.fsub(dest, s1, s2);
      break;
    case FpBinOp::Mul:
      e.fmul(dest, s1, s2);
      break;
    case FpBinOp::Div:
      e.fdiv(dest, s1, s2);
      break;
  }
  e.fcmp(dest, dest);
  e.b(VC, done);
  e.mov(e.x0, static_cast<uint64_t>(0xFFF8000000000000ull));
  e.fmov(dest, e.x0);
  e.b(done);

  // Slow path: first NaN by position wins, quiet if SNaN.
  e.L(nan_path);
  auto& s1_not_nan = e.NewCachedLabel();
  e.fcmp(s1, s1);
  e.b(VC, s1_not_nan);
  e.fmov(e.x0, s1);
  e.orr(e.x0, e.x0, static_cast<uint64_t>(1ull << 51));
  e.fmov(dest, e.x0);
  e.b(done);
  e.L(s1_not_nan);
  e.fmov(e.x0, s2);
  e.orr(e.x0, e.x0, static_cast<uint64_t>(1ull << 51));
  e.fmov(dest, e.x0);

  e.L(done);
}
// PPC FMA NaN selection (PowerISA 4.6.7.2):
// The first NaN operand by position (frA=s1, frC=s2, frB=s3) wins,
// regardless of QNaN vs SNaN.  If it's an SNaN, quiet it (set the
// quiet bit).  If no operand is NaN, use hardware FMA; generated NaN
// (from 0*inf or inf-inf) becomes the PPC default QNaN.
// ARM64's fmadd may propagate a different NaN than PPC's positional
// rule, so NaN inputs are handled entirely in software.
static void EmitFmaWithPpcNan_F64(A64Emitter& e, DReg dest, DReg s1, DReg s2,
                                  DReg s3, bool is_sub) {
  e.ChangeFpcrMode(FPCRMode::Fpu);
  auto& nan_path = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();

  // Quick check: any NaN among the three operands?
  e.fcmp(s1, s1);
  e.fccmp(s2, s2, 0b0001, VC);
  e.fccmp(s3, s3, 0b0001, VC);
  e.b(VS, nan_path);

  // Fast path: no NaN input → hardware FMA.
  if (is_sub)
    e.fnmsub(dest, s1, s2, s3);
  else
    e.fmadd(dest, s1, s2, s3);
  // If result is NaN (0*inf or inf-inf), canonicalize to PPC default.
  e.fcmp(dest, dest);
  e.b(VC, done);
  e.mov(e.x0, static_cast<uint64_t>(0xFFF8000000000000ull));
  e.fmov(dest, e.x0);
  e.b(done);

  // Slow path: first NaN by position wins (quiet if SNaN).
  e.L(nan_path);
  auto& s1_not_nan = e.NewCachedLabel();
  e.fcmp(s1, s1);
  e.b(VC, s1_not_nan);
  e.fmov(e.x0, s1);
  e.orr(e.x0, e.x0, static_cast<uint64_t>(1ull << 51));  // ensure quiet
  e.fmov(dest, e.x0);
  e.b(done);
  e.L(s1_not_nan);

  auto& s2_not_nan = e.NewCachedLabel();
  e.fcmp(s2, s2);
  e.b(VC, s2_not_nan);
  e.fmov(e.x0, s2);
  e.orr(e.x0, e.x0, static_cast<uint64_t>(1ull << 51));
  e.fmov(dest, e.x0);
  e.b(done);
  e.L(s2_not_nan);

  // Must be s3 (at least one NaN exists).
  e.fmov(e.x0, s3);
  e.orr(e.x0, e.x0, static_cast<uint64_t>(1ull << 51));
  e.fmov(dest, e.x0);

  e.L(done);
}

static void EmitFmaWithPpcNan_F32(A64Emitter& e, SReg dest, SReg s1, SReg s2,
                                  SReg s3, bool is_sub) {
  e.ChangeFpcrMode(FPCRMode::Fpu);
  auto& nan_path = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();

  e.fcmp(s1, s1);
  e.fccmp(s2, s2, 0b0001, VC);
  e.fccmp(s3, s3, 0b0001, VC);
  e.b(VS, nan_path);

  if (is_sub)
    e.fnmsub(dest, s1, s2, s3);
  else
    e.fmadd(dest, s1, s2, s3);
  e.fcmp(dest, dest);
  e.b(VC, done);
  e.mov(e.w0, static_cast<uint64_t>(0xFFC00000u));
  e.fmov(dest, e.w0);
  e.b(done);

  e.L(nan_path);
  auto& s1_not_nan = e.NewCachedLabel();
  e.fcmp(s1, s1);
  e.b(VC, s1_not_nan);
  e.fmov(e.w0, s1);
  e.orr(e.w0, e.w0, static_cast<uint32_t>(1u << 22));
  e.fmov(dest, e.w0);
  e.b(done);
  e.L(s1_not_nan);

  auto& s2_not_nan = e.NewCachedLabel();
  e.fcmp(s2, s2);
  e.b(VC, s2_not_nan);
  e.fmov(e.w0, s2);
  e.orr(e.w0, e.w0, static_cast<uint32_t>(1u << 22));
  e.fmov(dest, e.w0);
  e.b(done);
  e.L(s2_not_nan);

  e.fmov(e.w0, s3);
  e.orr(e.w0, e.w0, static_cast<uint32_t>(1u << 22));
  e.fmov(dest, e.w0);

  e.L(done);
}

struct ADD_F32 : Sequence<ADD_F32, I<OPCODE_ADD, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg s1 = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    SReg s2 = i.src2.is_constant ? e.s1 : SReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w0);
    }
    EmitFpBinOpWithPpcNan_F32(e, i.dest, s1, s2, FpBinOp::Add);
  }
};
struct ADD_F64 : Sequence<ADD_F64, I<OPCODE_ADD, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    DReg s1 = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    DReg s2 = i.src2.is_constant ? e.d1 : DReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d1, e.x0);
    }
    EmitFpBinOpWithPpcNan_F64(e, i.dest, s1, s2, FpBinOp::Add);
  }
};
struct ADD_V128 : Sequence<ADD_V128, I<OPCODE_ADD, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitVmxFpBinOp_V128(e, i.dest.reg().getIdx(), i.src1, i.src2,
                        VmxFpBinOp::Add);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD, ADD_I8, ADD_I16, ADD_I32, ADD_I64, ADD_F32,
                     ADD_F64, ADD_V128);

// ============================================================================
// OPCODE_ZERO_EXTEND
// ============================================================================
struct ZERO_EXTEND_I16_I8
    : Sequence<ZERO_EXTEND_I16_I8, I<OPCODE_ZERO_EXTEND, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // uxtb wD, wS (same as and wD, wS, #0xFF)
    e.uxtb(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I32_I8
    : Sequence<ZERO_EXTEND_I32_I8, I<OPCODE_ZERO_EXTEND, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxtb(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I8
    : Sequence<ZERO_EXTEND_I64_I8, I<OPCODE_ZERO_EXTEND, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Zero-extend 8-bit to 64-bit: AND with 0xFF in 32-bit clears upper 32.
    auto w_dest = WReg(i.dest.reg().getIdx());
    e.uxtb(w_dest, i.src1);
  }
};
struct ZERO_EXTEND_I32_I16
    : Sequence<ZERO_EXTEND_I32_I16, I<OPCODE_ZERO_EXTEND, I32Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxth(i.dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I16
    : Sequence<ZERO_EXTEND_I64_I16, I<OPCODE_ZERO_EXTEND, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto w_dest = WReg(i.dest.reg().getIdx());
    e.uxth(w_dest, i.src1);
  }
};
struct ZERO_EXTEND_I64_I32
    : Sequence<ZERO_EXTEND_I64_I32, I<OPCODE_ZERO_EXTEND, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // mov wD, wS implicitly zero-extends to 64 bits on ARM64.
    auto w_dest = WReg(i.dest.reg().getIdx());
    e.mov(w_dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ZERO_EXTEND, ZERO_EXTEND_I16_I8, ZERO_EXTEND_I32_I8,
                     ZERO_EXTEND_I64_I8, ZERO_EXTEND_I32_I16,
                     ZERO_EXTEND_I64_I16, ZERO_EXTEND_I64_I32);

// ============================================================================
// OPCODE_SIGN_EXTEND
// ============================================================================
struct SIGN_EXTEND_I16_I8
    : Sequence<SIGN_EXTEND_I16_I8, I<OPCODE_SIGN_EXTEND, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtb(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I32_I8
    : Sequence<SIGN_EXTEND_I32_I8, I<OPCODE_SIGN_EXTEND, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtb(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I8
    : Sequence<SIGN_EXTEND_I64_I8, I<OPCODE_SIGN_EXTEND, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtb(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I32_I16
    : Sequence<SIGN_EXTEND_I32_I16, I<OPCODE_SIGN_EXTEND, I32Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxth(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I16
    : Sequence<SIGN_EXTEND_I64_I16, I<OPCODE_SIGN_EXTEND, I64Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxth(i.dest, i.src1);
  }
};
struct SIGN_EXTEND_I64_I32
    : Sequence<SIGN_EXTEND_I64_I32, I<OPCODE_SIGN_EXTEND, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.sxtw(i.dest, i.src1);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SIGN_EXTEND, SIGN_EXTEND_I16_I8, SIGN_EXTEND_I32_I8,
                     SIGN_EXTEND_I64_I8, SIGN_EXTEND_I32_I16,
                     SIGN_EXTEND_I64_I16, SIGN_EXTEND_I64_I32);

// ============================================================================
// OPCODE_TRUNCATE
// ============================================================================
struct TRUNCATE_I8_I16
    : Sequence<TRUNCATE_I8_I16, I<OPCODE_TRUNCATE, I8Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Keep only low 8 bits.
    e.uxtb(i.dest, i.src1);
  }
};
struct TRUNCATE_I8_I32
    : Sequence<TRUNCATE_I8_I32, I<OPCODE_TRUNCATE, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxtb(i.dest, i.src1);
  }
};
struct TRUNCATE_I8_I64
    : Sequence<TRUNCATE_I8_I64, I<OPCODE_TRUNCATE, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto w_src = WReg(i.src1.reg().getIdx());
    e.uxtb(i.dest, w_src);
  }
};
struct TRUNCATE_I16_I32
    : Sequence<TRUNCATE_I16_I32, I<OPCODE_TRUNCATE, I16Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.uxth(i.dest, i.src1);
  }
};
struct TRUNCATE_I16_I64
    : Sequence<TRUNCATE_I16_I64, I<OPCODE_TRUNCATE, I16Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    auto w_src = WReg(i.src1.reg().getIdx());
    e.uxth(i.dest, w_src);
  }
};
struct TRUNCATE_I32_I64
    : Sequence<TRUNCATE_I32_I64, I<OPCODE_TRUNCATE, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // mov wD, wS — implicitly truncates (upper 32 bits zeroed).
    auto w_src = WReg(i.src1.reg().getIdx());
    e.mov(i.dest, w_src);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_TRUNCATE, TRUNCATE_I8_I16, TRUNCATE_I8_I32,
                     TRUNCATE_I8_I64, TRUNCATE_I16_I32, TRUNCATE_I16_I64,
                     TRUNCATE_I32_I64);

// ============================================================================
// OPCODE_SUB
// ============================================================================
template <typename T, typename REG>
static void EmitSubInt(A64Emitter& e, const T& i) {
  if (i.src1.is_constant && i.src2.is_constant) {
    e.mov(
        i.dest,
        static_cast<uint64_t>(
            static_cast<
                typename std::make_unsigned<decltype(i.src1.constant())>::type>(
                i.src1.constant() - i.src2.constant())));
  } else if (i.src2.is_constant) {
    uint64_t imm = static_cast<uint64_t>(
        static_cast<
            typename std::make_unsigned<decltype(i.src2.constant())>::type>(
            i.src2.constant()));
    if (imm <= 4095) {
      e.sub(i.dest, i.src1, static_cast<uint32_t>(imm));
    } else {
      e.mov(e.w0, static_cast<uint64_t>(imm));
      e.sub(i.dest, i.src1, REG(0));
    }
  } else if (i.src1.is_constant) {
    uint64_t imm = static_cast<uint64_t>(
        static_cast<
            typename std::make_unsigned<decltype(i.src1.constant())>::type>(
            i.src1.constant()));
    // Use scratch register to avoid clobbering src2 when dest == src2.
    e.mov(e.w17, imm);
    e.sub(i.dest, REG(17), i.src2);
  } else {
    e.sub(i.dest, i.src1, i.src2);
  }
}
struct SUB_I8 : Sequence<SUB_I8, I<OPCODE_SUB, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitSubInt<EmitArgType, WReg>(e, i);
  }
};
struct SUB_I16 : Sequence<SUB_I16, I<OPCODE_SUB, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitSubInt<EmitArgType, WReg>(e, i);
  }
};
struct SUB_I32 : Sequence<SUB_I32, I<OPCODE_SUB, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (!(i.src1.is_constant && i.src2.is_constant) && i.src2.is_constant) {
      MaybeAuditWrappedAddSubImm(e, "SUB_I32", 32,
                                 static_cast<uint32_t>(i.src2.constant()),
                                 "src2", "ADD #inverse");
    }
    EmitSubInt<EmitArgType, WReg>(e, i);
  }
};
struct SUB_I64 : Sequence<SUB_I64, I<OPCODE_SUB, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() - i.src2.constant()));
    } else if (i.src2.is_constant) {
      uint64_t imm = static_cast<uint64_t>(i.src2.constant());
      MaybeAuditWrappedAddSubImm(e, "SUB_I64", 64, imm, "src2", "ADD #inverse");
      if (imm <= 4095) {
        e.sub(i.dest, i.src1, static_cast<uint32_t>(imm));
      } else {
        e.mov(e.x0, imm);
        e.sub(i.dest, i.src1, e.x0);
      }
    } else if (i.src1.is_constant) {
      // Use scratch register to avoid clobbering src2 when dest == src2.
      e.mov(e.x17, static_cast<uint64_t>(i.src1.constant()));
      e.sub(i.dest, e.x17, i.src2);
    } else {
      e.sub(i.dest, i.src1, i.src2);
    }
  }
};
struct SUB_F32 : Sequence<SUB_F32, I<OPCODE_SUB, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg s1 = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    SReg s2 = i.src2.is_constant ? e.s1 : SReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w0);
    }
    EmitFpBinOpWithPpcNan_F32(e, i.dest, s1, s2, FpBinOp::Sub);
  }
};
struct SUB_F64 : Sequence<SUB_F64, I<OPCODE_SUB, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    DReg s1 = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    DReg s2 = i.src2.is_constant ? e.d1 : DReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d1, e.x0);
    }
    EmitFpBinOpWithPpcNan_F64(e, i.dest, s1, s2, FpBinOp::Sub);
  }
};
struct SUB_V128 : Sequence<SUB_V128, I<OPCODE_SUB, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitVmxFpBinOp_V128(e, i.dest.reg().getIdx(), i.src1, i.src2,
                        VmxFpBinOp::Sub);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SUB, SUB_I8, SUB_I16, SUB_I32, SUB_I64, SUB_F32,
                     SUB_F64, SUB_V128);

// ============================================================================
// OPCODE_ADD_CARRY
// ============================================================================
struct ADD_CARRY_I8
    : Sequence<ADD_CARRY_I8, I<OPCODE_ADD_CARRY, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 + src2 + src3 (carry in)
    if (i.src1.is_constant && i.src2.is_constant && i.src3.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(
                (i.src1.constant() + i.src2.constant() + i.src3.constant()) &
                0xFF));
    } else {
      // Load src1 into dest (or w0 if constant).
      if (i.src1.is_constant) {
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
      } else {
        e.mov(e.w0, i.src1);
      }
      // Add src2.
      if (i.src2.is_constant) {
        e.add(e.w0, e.w0, static_cast<uint32_t>(i.src2.constant() & 0xFF));
      } else {
        e.add(e.w0, e.w0, i.src2);
      }
      // Add carry.
      if (i.src3.is_constant) {
        if (i.src3.constant()) {
          e.add(e.w0, e.w0, 1);
        }
      } else {
        e.add(e.w0, e.w0, i.src3);
      }
      e.mov(i.dest, e.w0);
    }
  }
};
struct ADD_CARRY_I16
    : Sequence<ADD_CARRY_I16, I<OPCODE_ADD_CARRY, I16Op, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
    } else {
      e.mov(e.w0, i.src1);
    }
    if (i.src2.is_constant) {
      e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
      e.add(e.w0, e.w0, e.w1);
    } else {
      e.add(e.w0, e.w0, i.src2);
    }
    if (i.src3.is_constant) {
      if (i.src3.constant()) e.add(e.w0, e.w0, 1);
    } else {
      e.add(e.w0, e.w0, i.src3);
    }
    e.mov(i.dest, e.w0);
  }
};
struct ADD_CARRY_I32
    : Sequence<ADD_CARRY_I32, I<OPCODE_ADD_CARRY, I32Op, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
    } else {
      e.mov(e.w0, i.src1);
    }
    if (i.src2.is_constant) {
      e.mov(e.w1,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
      e.add(e.w0, e.w0, e.w1);
    } else {
      e.add(e.w0, e.w0, i.src2);
    }
    if (i.src3.is_constant) {
      if (i.src3.constant()) e.add(e.w0, e.w0, 1);
    } else {
      e.add(e.w0, e.w0, i.src3);
    }
    e.mov(i.dest, e.w0);
  }
};
struct ADD_CARRY_I64
    : Sequence<ADD_CARRY_I64, I<OPCODE_ADD_CARRY, I64Op, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
    } else {
      e.mov(e.x0, i.src1);
    }
    if (i.src2.is_constant) {
      e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));
      e.add(e.x0, e.x0, e.x1);
    } else {
      e.add(e.x0, e.x0, i.src2);
    }
    if (i.src3.is_constant) {
      if (i.src3.constant()) e.add(e.x0, e.x0, 1);
    } else {
      // Zero-extend the I8 carry to 64-bit.
      e.mov(e.w1, i.src3);
      e.uxtb(e.w1, e.w1);
      e.add(e.x0, e.x0, e.x1);
    }
    e.mov(i.dest, e.x0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ADD_CARRY, ADD_CARRY_I8, ADD_CARRY_I16,
                     ADD_CARRY_I32, ADD_CARRY_I64);

// ============================================================================
// OPCODE_MUL
// ============================================================================
struct MUL_I32 : Sequence<MUL_I32, I<OPCODE_MUL, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() * i.src2.constant())));
    } else if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      e.mul(i.dest, e.w0, i.src2);
    } else if (i.src2.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
      e.mul(i.dest, i.src1, e.w0);
    } else {
      e.mul(i.dest, i.src1, i.src2);
    }
  }
};
struct MUL_I64 : Sequence<MUL_I64, I<OPCODE_MUL, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() * i.src2.constant()));
    } else if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
      e.mul(i.dest, e.x0, i.src2);
    } else if (i.src2.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src2.constant()));
      e.mul(i.dest, i.src1, e.x0);
    } else {
      e.mul(i.dest, i.src1, i.src2);
    }
  }
};
struct MUL_F32 : Sequence<MUL_F32, I<OPCODE_MUL, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg s1 = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    SReg s2 = i.src2.is_constant ? e.s1 : SReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w0);
    }
    EmitFpBinOpWithPpcNan_F32(e, i.dest, s1, s2, FpBinOp::Mul);
  }
};
struct MUL_F64 : Sequence<MUL_F64, I<OPCODE_MUL, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    DReg s1 = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    DReg s2 = i.src2.is_constant ? e.d1 : DReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d1, e.x0);
    }
    EmitFpBinOpWithPpcNan_F64(e, i.dest, s1, s2, FpBinOp::Mul);
  }
};
struct MUL_V128 : Sequence<MUL_V128, I<OPCODE_MUL, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitVmxFpBinOp_V128(e, i.dest.reg().getIdx(), i.src1, i.src2,
                        VmxFpBinOp::Mul);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL, MUL_I32, MUL_I64, MUL_F32, MUL_F64, MUL_V128);

// ============================================================================
// OPCODE_MUL_HI
// ============================================================================
struct MUL_HI_I8 : Sequence<MUL_HI_I8, I<OPCODE_MUL_HI, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      if (i.src1.is_constant) {
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
      } else {
        e.uxtb(e.w0, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFF));
      } else {
        e.uxtb(e.w1, i.src2);
      }
      e.mul(e.w0, e.w0, e.w1);
      e.lsr(e.w0, e.w0, static_cast<uint32_t>(8));
    } else {
      if (i.src1.is_constant) {
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
        e.sxtb(e.w0, e.w0);
      } else {
        e.sxtb(e.w0, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFF));
        e.sxtb(e.w1, e.w1);
      } else {
        e.sxtb(e.w1, i.src2);
      }
      e.mul(e.w0, e.w0, e.w1);
      e.asr(e.w0, e.w0, static_cast<uint32_t>(8));
    }
    e.uxtb(i.dest, e.w0);
  }
};
struct MUL_HI_I16
    : Sequence<MUL_HI_I16, I<OPCODE_MUL_HI, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      if (i.src1.is_constant) {
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
      } else {
        e.uxth(e.w0, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
      } else {
        e.uxth(e.w1, i.src2);
      }
      e.mul(e.w0, e.w0, e.w1);
      e.lsr(e.w0, e.w0, static_cast<uint32_t>(16));
    } else {
      if (i.src1.is_constant) {
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
        e.sxth(e.w0, e.w0);
      } else {
        e.sxth(e.w0, i.src1);
      }
      if (i.src2.is_constant) {
        e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
        e.sxth(e.w1, e.w1);
      } else {
        e.sxth(e.w1, i.src2);
      }
      e.mul(e.w0, e.w0, e.w1);
      e.asr(e.w0, e.w0, static_cast<uint32_t>(16));
    }
    e.uxth(i.dest, e.w0);
  }
};
struct MUL_HI_I32
    : Sequence<MUL_HI_I32, I<OPCODE_MUL_HI, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
    } else {
      e.mov(e.w0, i.src1);
    }
    if (i.src2.is_constant) {
      e.mov(e.w1,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
    } else {
      e.mov(e.w1, i.src2);
    }
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.umull(e.x0, e.w0, e.w1);
    } else {
      e.smull(e.x0, e.w0, e.w1);
    }
    e.lsr(e.x0, e.x0, static_cast<uint32_t>(32));
    e.mov(i.dest, e.w0);
  }
};
struct MUL_HI_I64
    : Sequence<MUL_HI_I64, I<OPCODE_MUL_HI, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    XReg s1 = i.src1.is_constant ? e.x0 : XReg(i.src1.reg().getIdx());
    XReg s2 = i.src2.is_constant ? e.x1 : XReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
    }
    if (i.src2.is_constant) {
      e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));
    }
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.umulh(i.dest, s1, s2);
    } else {
      e.smulh(i.dest, s1, s2);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_HI, MUL_HI_I8, MUL_HI_I16, MUL_HI_I32,
                     MUL_HI_I64);

// ============================================================================
// OPCODE_DIV
// ============================================================================
struct DIV_I32 : Sequence<DIV_I32, I<OPCODE_DIV, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // ARM64 sdiv/udiv returns 0 on divide by zero (no exception).
    WReg s1 = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    WReg s2 = i.src2.is_constant ? e.w1 : WReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
    }
    if (i.src2.is_constant) {
      e.mov(e.w1,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
    }
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.udiv(i.dest, s1, s2);
    } else {
      e.sdiv(i.dest, s1, s2);
    }
  }
};
struct DIV_I64 : Sequence<DIV_I64, I<OPCODE_DIV, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    XReg s1 = i.src1.is_constant ? e.x0 : XReg(i.src1.reg().getIdx());
    XReg s2 = i.src2.is_constant ? e.x1 : XReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
    }
    if (i.src2.is_constant) {
      e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));
    }
    if (i.instr->flags & ARITHMETIC_UNSIGNED) {
      e.udiv(i.dest, s1, s2);
    } else {
      e.sdiv(i.dest, s1, s2);
    }
  }
};
struct DIV_F32 : Sequence<DIV_F32, I<OPCODE_DIV, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    SReg s1 = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    SReg s2 = i.src2.is_constant ? e.s1 : SReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w0);
    }
    EmitFpBinOpWithPpcNan_F32(e, i.dest, s1, s2, FpBinOp::Div);
  }
};
struct DIV_F64 : Sequence<DIV_F64, I<OPCODE_DIV, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    DReg s1 = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    DReg s2 = i.src2.is_constant ? e.d1 : DReg(i.src2.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d1, e.x0);
    }
    EmitFpBinOpWithPpcNan_F64(e, i.dest, s1, s2, FpBinOp::Div);
  }
};
struct DIV_V128 : Sequence<DIV_V128, I<OPCODE_DIV, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitVmxFpBinOp_V128(e, i.dest.reg().getIdx(), i.src1, i.src2,
                        VmxFpBinOp::Div);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DIV, DIV_I32, DIV_I64, DIV_F32, DIV_F64, DIV_V128);

// ============================================================================
// OPCODE_NEG
// ============================================================================
struct NEG_I8 : Sequence<NEG_I8, I<OPCODE_NEG, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint8_t>(-i.src1.constant())));
    } else {
      e.neg(i.dest, i.src1);
    }
  }
};
struct NEG_I16 : Sequence<NEG_I16, I<OPCODE_NEG, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint16_t>(-i.src1.constant())));
    } else {
      e.neg(i.dest, i.src1);
    }
  }
};
struct NEG_I32 : Sequence<NEG_I32, I<OPCODE_NEG, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint32_t>(-i.src1.constant())));
    } else {
      e.neg(i.dest, i.src1);
    }
  }
};
struct NEG_I64 : Sequence<NEG_I64, I<OPCODE_NEG, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(-i.src1.constant()));
    } else {
      e.neg(i.dest, i.src1);
    }
  }
};
struct NEG_F32 : Sequence<NEG_F32, I<OPCODE_NEG, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = -i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(i.dest, e.w0);
    } else {
      e.fneg(i.dest, i.src1);
    }
  }
};
struct NEG_F64 : Sequence<NEG_F64, I<OPCODE_NEG, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = -i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(i.dest, e.x0);
    } else {
      e.fneg(i.dest, i.src1);
    }
  }
};
struct NEG_V128 : Sequence<NEG_V128, I<OPCODE_NEG, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      int s = SrcVReg(e, i.src1, 0);
      e.fneg(VReg(i.dest.reg().getIdx()).s4, VReg(s).s4);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_NEG, NEG_I8, NEG_I16, NEG_I32, NEG_I64, NEG_F32,
                     NEG_F64, NEG_V128);

// ============================================================================
// OPCODE_ABS
// ============================================================================
struct ABS_F32 : Sequence<ABS_F32, I<OPCODE_ABS, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      c.u &= 0x7FFFFFFF;
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(i.dest, e.w0);
    } else {
      e.fabs(i.dest, i.src1);
    }
  }
};
struct ABS_F64 : Sequence<ABS_F64, I<OPCODE_ABS, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      c.u &= 0x7FFFFFFFFFFFFFFFULL;
      e.mov(e.x0, c.u);
      e.fmov(i.dest, e.x0);
    } else {
      e.fabs(i.dest, i.src1);
    }
  }
};
struct ABS_V128 : Sequence<ABS_V128, I<OPCODE_ABS, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      int s = SrcVReg(e, i.src1, 0);
      e.fabs(VReg(i.dest.reg().getIdx()).s4, VReg(s).s4);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ABS, ABS_F32, ABS_F64, ABS_V128);

// ============================================================================
// OPCODE_AND
// ============================================================================
struct AND_I8 : Sequence<AND_I8, I<OPCODE_AND, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() & i.src2.constant()) & 0xFF));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kAnd, "AND_I8", i.dest, i.src1,
                       i.src2.constant() & 0xFF, "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kAnd, "AND_I8", i.dest, i.src2,
                       i.src1.constant() & 0xFF, "src1");
    } else {
      e.and_(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_I16 : Sequence<AND_I16, I<OPCODE_AND, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() & i.src2.constant()) & 0xFFFF));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kAnd, "AND_I16", i.dest, i.src1,
                       i.src2.constant() & 0xFFFF, "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kAnd, "AND_I16", i.dest, i.src2,
                       i.src1.constant() & 0xFFFF, "src1");
    } else {
      e.and_(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_I32 : Sequence<AND_I32, I<OPCODE_AND, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() & i.src2.constant())));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kAnd, "AND_I32", i.dest, i.src1,
                       static_cast<uint32_t>(i.src2.constant()), "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kAnd, "AND_I32", i.dest, i.src2,
                       static_cast<uint32_t>(i.src1.constant()), "src1");
    } else {
      e.and_(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_I64 : Sequence<AND_I64, I<OPCODE_AND, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() & i.src2.constant()));
    } else if (i.src2.is_constant) {
      EmitLogicalImm64(e, LogicalImmOp::kAnd, "AND_I64", i.dest, i.src1,
                       static_cast<uint64_t>(i.src2.constant()), "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm64(e, LogicalImmOp::kAnd, "AND_I64", i.dest, i.src2,
                       static_cast<uint64_t>(i.src1.constant()), "src1");
    } else {
      e.and_(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_V128 : Sequence<AND_V128, I<OPCODE_AND, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int s1 = SrcVReg(e, i.src1, 0);
    int s2 = SrcVReg(e, i.src2, 1);
    e.and_(VReg(i.dest.reg().getIdx()).b16, VReg(s1).b16, VReg(s2).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_AND, AND_I8, AND_I16, AND_I32, AND_I64, AND_V128);

// ============================================================================
// OPCODE_AND_NOT
// ============================================================================
struct AND_NOT_I8 : Sequence<AND_NOT_I8, I<OPCODE_AND_NOT, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 & ~src2 -> bic dest, src1, src2
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() & ~i.src2.constant()) & 0xFF));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kAnd, "AND_NOT_I8", i.dest, i.src1,
                       ~static_cast<uint32_t>(i.src2.constant() & 0xFF),
                       "src2_inverted");
    } else if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
      e.bic(i.dest, e.w0, i.src2);
    } else {
      e.bic(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_NOT_I16
    : Sequence<AND_NOT_I16, I<OPCODE_AND_NOT, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() & ~i.src2.constant()) & 0xFFFF));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kAnd, "AND_NOT_I16", i.dest, i.src1,
                       ~static_cast<uint32_t>(i.src2.constant() & 0xFFFF),
                       "src2_inverted");
    } else if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
      e.bic(i.dest, e.w0, i.src2);
    } else {
      e.bic(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_NOT_I32
    : Sequence<AND_NOT_I32, I<OPCODE_AND_NOT, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() & ~i.src2.constant())));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kAnd, "AND_NOT_I32", i.dest, i.src1,
                       ~static_cast<uint32_t>(i.src2.constant()),
                       "src2_inverted");
    } else if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      e.bic(i.dest, e.w0, i.src2);
    } else {
      e.bic(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_NOT_I64
    : Sequence<AND_NOT_I64, I<OPCODE_AND_NOT, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() & ~i.src2.constant()));
    } else if (i.src2.is_constant) {
      EmitLogicalImm64(e, LogicalImmOp::kAnd, "AND_NOT_I64", i.dest, i.src1,
                       ~static_cast<uint64_t>(i.src2.constant()),
                       "src2_inverted");
    } else if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
      e.bic(i.dest, e.x0, i.src2);
    } else {
      e.bic(i.dest, i.src1, i.src2);
    }
  }
};
struct AND_NOT_V128
    : Sequence<AND_NOT_V128, I<OPCODE_AND_NOT, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // AND_NOT = src1 AND (NOT src2) = BIC(src1, src2)
    int s1 = SrcVReg(e, i.src1, 0);
    int s2 = SrcVReg(e, i.src2, 1);
    e.bic(VReg(i.dest.reg().getIdx()).b16, VReg(s1).b16, VReg(s2).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_AND_NOT, AND_NOT_I8, AND_NOT_I16, AND_NOT_I32,
                     AND_NOT_I64, AND_NOT_V128);

// ============================================================================
// OPCODE_OR
// ============================================================================
struct OR_I8 : Sequence<OR_I8, I<OPCODE_OR, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() | i.src2.constant()) & 0xFF));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kOrr, "OR_I8", i.dest, i.src1,
                       i.src2.constant() & 0xFF, "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kOrr, "OR_I8", i.dest, i.src2,
                       i.src1.constant() & 0xFF, "src1");
    } else {
      e.orr(i.dest, i.src1, i.src2);
    }
  }
};
struct OR_I16 : Sequence<OR_I16, I<OPCODE_OR, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() | i.src2.constant()) & 0xFFFF));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kOrr, "OR_I16", i.dest, i.src1,
                       i.src2.constant() & 0xFFFF, "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kOrr, "OR_I16", i.dest, i.src2,
                       i.src1.constant() & 0xFFFF, "src1");
    } else {
      e.orr(i.dest, i.src1, i.src2);
    }
  }
};
struct OR_I32 : Sequence<OR_I32, I<OPCODE_OR, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() | i.src2.constant())));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kOrr, "OR_I32", i.dest, i.src1,
                       static_cast<uint32_t>(i.src2.constant()), "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kOrr, "OR_I32", i.dest, i.src2,
                       static_cast<uint32_t>(i.src1.constant()), "src1");
    } else {
      e.orr(i.dest, i.src1, i.src2);
    }
  }
};
struct OR_I64 : Sequence<OR_I64, I<OPCODE_OR, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() | i.src2.constant()));
    } else if (i.src2.is_constant) {
      EmitLogicalImm64(e, LogicalImmOp::kOrr, "OR_I64", i.dest, i.src1,
                       static_cast<uint64_t>(i.src2.constant()), "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm64(e, LogicalImmOp::kOrr, "OR_I64", i.dest, i.src2,
                       static_cast<uint64_t>(i.src1.constant()), "src1");
    } else {
      e.orr(i.dest, i.src1, i.src2);
    }
  }
};
struct OR_V128 : Sequence<OR_V128, I<OPCODE_OR, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int s1 = SrcVReg(e, i.src1, 0);
    int s2 = SrcVReg(e, i.src2, 1);
    e.orr(VReg(i.dest.reg().getIdx()).b16, VReg(s1).b16, VReg(s2).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_OR, OR_I8, OR_I16, OR_I32, OR_I64, OR_V128);

// ============================================================================
// OPCODE_XOR
// ============================================================================
struct XOR_I8 : Sequence<XOR_I8, I<OPCODE_XOR, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() ^ i.src2.constant()) & 0xFF));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kEor, "XOR_I8", i.dest, i.src1,
                       i.src2.constant() & 0xFF, "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kEor, "XOR_I8", i.dest, i.src2,
                       i.src1.constant() & 0xFF, "src1");
    } else {
      e.eor(i.dest, i.src1, i.src2);
    }
  }
};
struct XOR_I16 : Sequence<XOR_I16, I<OPCODE_XOR, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(
                        (i.src1.constant() ^ i.src2.constant()) & 0xFFFF));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kEor, "XOR_I16", i.dest, i.src1,
                       i.src2.constant() & 0xFFFF, "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kEor, "XOR_I16", i.dest, i.src2,
                       i.src1.constant() & 0xFFFF, "src1");
    } else {
      e.eor(i.dest, i.src1, i.src2);
    }
  }
};
struct XOR_I32 : Sequence<XOR_I32, I<OPCODE_XOR, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                        i.src1.constant() ^ i.src2.constant())));
    } else if (i.src2.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kEor, "XOR_I32", i.dest, i.src1,
                       static_cast<uint32_t>(i.src2.constant()), "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm32(e, LogicalImmOp::kEor, "XOR_I32", i.dest, i.src2,
                       static_cast<uint32_t>(i.src1.constant()), "src1");
    } else {
      e.eor(i.dest, i.src1, i.src2);
    }
  }
};
struct XOR_I64 : Sequence<XOR_I64, I<OPCODE_XOR, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant && i.src2.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(i.src1.constant() ^ i.src2.constant()));
    } else if (i.src2.is_constant) {
      EmitLogicalImm64(e, LogicalImmOp::kEor, "XOR_I64", i.dest, i.src1,
                       static_cast<uint64_t>(i.src2.constant()), "src2");
    } else if (i.src1.is_constant) {
      EmitLogicalImm64(e, LogicalImmOp::kEor, "XOR_I64", i.dest, i.src2,
                       static_cast<uint64_t>(i.src1.constant()), "src1");
    } else {
      e.eor(i.dest, i.src1, i.src2);
    }
  }
};
struct XOR_V128 : Sequence<XOR_V128, I<OPCODE_XOR, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int s1 = SrcVReg(e, i.src1, 0);
    int s2 = SrcVReg(e, i.src2, 1);
    e.eor(VReg(i.dest.reg().getIdx()).b16, VReg(s1).b16, VReg(s2).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_XOR, XOR_I8, XOR_I16, XOR_I32, XOR_I64, XOR_V128);

// ============================================================================
// OPCODE_NOT
// ============================================================================
struct NOT_I8 : Sequence<NOT_I8, I<OPCODE_NOT, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint8_t>(~i.src1.constant())));
    } else {
      e.mvn(i.dest, i.src1);
    }
  }
};
struct NOT_I16 : Sequence<NOT_I16, I<OPCODE_NOT, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint16_t>(~i.src1.constant())));
    } else {
      e.mvn(i.dest, i.src1);
    }
  }
};
struct NOT_I32 : Sequence<NOT_I32, I<OPCODE_NOT, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest,
            static_cast<uint64_t>(static_cast<uint32_t>(~i.src1.constant())));
    } else {
      e.mvn(i.dest, i.src1);
    }
  }
};
struct NOT_I64 : Sequence<NOT_I64, I<OPCODE_NOT, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(~i.src1.constant()));
    } else {
      e.mvn(i.dest, i.src1);
    }
  }
};
struct NOT_V128 : Sequence<NOT_V128, I<OPCODE_NOT, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int s = SrcVReg(e, i.src1, 0);
    e.not_(VReg(i.dest.reg().getIdx()).b16, VReg(s).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_NOT, NOT_I8, NOT_I16, NOT_I32, NOT_I64, NOT_V128);

// ============================================================================
// OPCODE_SHL
// ============================================================================
struct SHL_I8 : Sequence<SHL_I8, I<OPCODE_SHL, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint8_t>(
                          i.src1.constant() << (i.src2.constant() & 0x7))));
      } else {
        e.lsl(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFF));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsl(i.dest, i.dest, e.w0);
    }
  }
};
struct SHL_I16 : Sequence<SHL_I16, I<OPCODE_SHL, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint16_t>(
                          i.src1.constant() << (i.src2.constant() & 0xF))));
      } else {
        e.lsl(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsl(i.dest, i.dest, e.w0);
    }
  }
};
struct SHL_I32 : Sequence<SHL_I32, I<OPCODE_SHL, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                          i.src1.constant() << (i.src2.constant() & 0x1F))));
      } else {
        e.lsl(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsl(i.dest, i.dest, e.w0);
    }
  }
};
struct SHL_I64 : Sequence<SHL_I64, I<OPCODE_SHL, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()
                                            << (i.src2.constant() & 0x3F)));
      } else {
        e.lsl(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x3F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.x0, XReg(i.src2.reg().getIdx()));
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsl(i.dest, i.dest, e.x0);
    }
  }
};
struct SHL_V128 : Sequence<SHL_V128, I<OPCODE_SHL, V128Op, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // PPC 128-bit SHL by N bits (0-7). The value is stored as 4 word-swapped
    // 32-bit lanes. Carries flow from higher NEON lanes to lower:
    //   lane[i] = (lane[i] << N) | (lane[i+1] >> (32-N))
    int s = SrcVReg(e, i.src1, 0);
    int d = i.dest.reg().getIdx();
    if (i.src2.is_constant) {
      uint8_t sh = i.src2.constant() & 0x7;
      if (sh == 0) {
        if (d != s) e.mov(VReg(d).b16, VReg(s).b16);
        return;
      }
      // Read carry before writing result (handles dest==src aliasing).
      e.ushr(VReg(0).s4, VReg(s).s4, 32 - sh);
      e.shl(VReg(d).s4, VReg(s).s4, sh);
    } else {
      // Variable shift: mask to 0-7, splat, use ushl.
      e.and_(e.w0, WReg(i.src2.reg().getIdx()), 7);
      e.dup(VReg(1).s4, e.w0);
      e.movi(VReg(2).s4, 32);
      e.sub(VReg(2).s4, VReg(2).s4, VReg(1).s4);   // 32-N
      e.neg(VReg(2).s4, VReg(2).s4);               // -(32-N) for right shift
      e.ushl(VReg(0).s4, VReg(s).s4, VReg(2).s4);  // carry: lane >> (32-N)
      e.ushl(VReg(d).s4, VReg(s).s4, VReg(1).s4);  // result: lane << N
    }
    // Shift carries from lane i+1 to lane i; lane 3 gets zero.
    e.movi(VReg(1).s4, 0);
    e.ext(VReg(0).b16, VReg(0).b16, VReg(1).b16, 4);
    e.orr(VReg(d).b16, VReg(d).b16, VReg(0).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHL, SHL_I8, SHL_I16, SHL_I32, SHL_I64, SHL_V128);

// ============================================================================
// OPCODE_SHR (logical shift right)
// ============================================================================
struct SHR_I8 : Sequence<SHR_I8, I<OPCODE_SHR, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint8_t>(
                          static_cast<uint8_t>(i.src1.constant()) >>
                          (i.src2.constant() & 0x7))));
      } else {
        e.lsr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFF));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsr(i.dest, i.dest, e.w0);
    }
  }
};
struct SHR_I16 : Sequence<SHR_I16, I<OPCODE_SHR, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint16_t>(
                          static_cast<uint16_t>(i.src1.constant()) >>
                          (i.src2.constant() & 0xF))));
      } else {
        e.lsr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsr(i.dest, i.dest, e.w0);
    }
  }
};
struct SHR_I32 : Sequence<SHR_I32, I<OPCODE_SHR, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant()) >>
                                    (i.src2.constant() & 0x1F)));
      } else {
        e.lsr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsr(i.dest, i.dest, e.w0);
    }
  }
};
struct SHR_I64 : Sequence<SHR_I64, I<OPCODE_SHR, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()) >>
                          (i.src2.constant() & 0x3F));
      } else {
        e.lsr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x3F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.x0, XReg(i.src2.reg().getIdx()));
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.lsr(i.dest, i.dest, e.x0);
    }
  }
};
struct SHR_V128 : Sequence<SHR_V128, I<OPCODE_SHR, V128Op, V128Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // PPC 128-bit SHR by N bits (0-7). Carries flow from lower NEON lanes
    // to higher:
    //   lane[i] = (lane[i] >> N) | (lane[i-1] << (32-N))
    int s = SrcVReg(e, i.src1, 0);
    int d = i.dest.reg().getIdx();
    if (i.src2.is_constant) {
      uint8_t sh = i.src2.constant() & 0x7;
      if (sh == 0) {
        if (d != s) e.mov(VReg(d).b16, VReg(s).b16);
        return;
      }
      // Read carry before writing result (handles dest==src aliasing).
      e.shl(VReg(0).s4, VReg(s).s4, 32 - sh);
      e.ushr(VReg(d).s4, VReg(s).s4, sh);
    } else {
      // Variable shift: mask to 0-7, splat, use ushl.
      e.and_(e.w0, WReg(i.src2.reg().getIdx()), 7);
      e.dup(VReg(1).s4, e.w0);
      e.movi(VReg(2).s4, 32);
      e.sub(VReg(2).s4, VReg(2).s4, VReg(1).s4);   // 32-N
      e.ushl(VReg(0).s4, VReg(s).s4, VReg(2).s4);  // carry: lane << (32-N)
      e.neg(VReg(1).s4, VReg(1).s4);               // -N for right shift
      e.ushl(VReg(d).s4, VReg(s).s4, VReg(1).s4);  // result: lane >> N
    }
    // Shift carries from lane i-1 to lane i; lane 0 gets zero.
    e.movi(VReg(1).s4, 0);
    e.ext(VReg(0).b16, VReg(1).b16, VReg(0).b16, 12);
    e.orr(VReg(d).b16, VReg(d).b16, VReg(0).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHR, SHR_I8, SHR_I16, SHR_I32, SHR_I64, SHR_V128);

// ============================================================================
// OPCODE_SHA (arithmetic shift right)
// ============================================================================
struct SHA_I8 : Sequence<SHA_I8, I<OPCODE_SHA, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Sign-extend to 32-bit, then ASR.
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    } else {
      e.mov(e.w0, i.src1);
    }
    e.sxtb(e.w0, e.w0);
    if (i.src2.is_constant) {
      e.asr(i.dest, e.w0, static_cast<uint32_t>(i.src2.constant() & 0x1F));
    } else {
      e.asr(i.dest, e.w0, i.src2);
    }
  }
};
struct SHA_I16 : Sequence<SHA_I16, I<OPCODE_SHA, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
    } else {
      e.mov(e.w0, i.src1);
    }
    e.sxth(e.w0, e.w0);
    if (i.src2.is_constant) {
      e.asr(i.dest, e.w0, static_cast<uint32_t>(i.src2.constant() & 0x1F));
    } else {
      e.asr(i.dest, e.w0, i.src2);
    }
  }
};
struct SHA_I32 : Sequence<SHA_I32, I<OPCODE_SHA, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(static_cast<uint32_t>(
                          i.src1.constant() >> (i.src2.constant() & 0x1F))));
      } else {
        e.asr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x1F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.asr(i.dest, i.dest, e.w0);
    }
  }
};
struct SHA_I64 : Sequence<SHA_I64, I<OPCODE_SHA, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant() >>
                                            (i.src2.constant() & 0x3F)));
      } else {
        e.asr(i.dest, i.src1, static_cast<uint32_t>(i.src2.constant() & 0x3F));
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.x0, XReg(i.src2.reg().getIdx()));
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      e.asr(i.dest, i.dest, e.x0);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SHA, SHA_I8, SHA_I16, SHA_I32, SHA_I64);

// ============================================================================
// OPCODE_ROTATE_LEFT
// ============================================================================
struct ROTATE_LEFT_I8
    : Sequence<ROTATE_LEFT_I8, I<OPCODE_ROTATE_LEFT, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // ARM64 has ROR but no ROL. ROL(x, n) = ROR(x, size - n).
    // For 8-bit: duplicate into both halves of a 16-bit val, then shift.
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    } else {
      e.uxtb(e.w0, i.src1);
    }
    // Duplicate byte into bits [15:8] too: w0 = (w0 | (w0 << 8))
    e.orr(e.w0, e.w0, e.w0, Xbyak_aarch64::LSL, 8);
    if (i.src2.is_constant) {
      uint32_t amt = i.src2.constant() & 0x7;
      if (amt) {
        e.lsr(e.w0, e.w0, static_cast<uint32_t>(8 - amt));
      }
    } else {
      // shift = 8 - (src2 & 7)
      e.mov(e.w1, 8);
      e.and_(e.w2, i.src2, 7);
      e.sub(e.w1, e.w1, e.w2);
      e.lsr(e.w0, e.w0, e.w1);
    }
    e.uxtb(i.dest, e.w0);
  }
};
struct ROTATE_LEFT_I16
    : Sequence<ROTATE_LEFT_I16, I<OPCODE_ROTATE_LEFT, I16Op, I16Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
    } else {
      e.uxth(e.w0, i.src1);
    }
    e.orr(e.w0, e.w0, e.w0, Xbyak_aarch64::LSL, 16);
    if (i.src2.is_constant) {
      uint32_t amt = i.src2.constant() & 0xF;
      if (amt) {
        e.lsr(e.w0, e.w0, static_cast<uint32_t>(16 - amt));
      }
    } else {
      e.mov(e.w1, 16);
      e.and_(e.w2, i.src2, 0xF);
      e.sub(e.w1, e.w1, e.w2);
      e.lsr(e.w0, e.w0, e.w1);
    }
    e.uxth(i.dest, e.w0);
  }
};
struct ROTATE_LEFT_I32
    : Sequence<ROTATE_LEFT_I32, I<OPCODE_ROTATE_LEFT, I32Op, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // ROL(x, n) = ROR(x, 32 - n)
    if (i.src2.is_constant) {
      uint32_t amt = i.src2.constant() & 0x1F;
      if (amt == 0) {
        if (i.src1.is_constant) {
          e.mov(i.dest, static_cast<uint64_t>(
                            static_cast<uint32_t>(i.src1.constant())));
        } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
          e.mov(i.dest, i.src1);
        }
      } else {
        if (i.src1.is_constant) {
          e.mov(e.w0, static_cast<uint64_t>(
                          static_cast<uint32_t>(i.src1.constant())));
          e.ror(i.dest, e.w0, static_cast<uint32_t>(32 - amt));
        } else {
          e.ror(i.dest, i.src1, static_cast<uint32_t>(32 - amt));
        }
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.w0, i.src2);
      if (i.src1.is_constant) {
        e.mov(i.dest,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      // ROL(x, n) = ROR(x, -n) since ROR uses amount mod 32
      e.neg(e.w0, e.w0);
      e.ror(i.dest, i.dest, e.w0);
    }
  }
};
struct ROTATE_LEFT_I64
    : Sequence<ROTATE_LEFT_I64, I<OPCODE_ROTATE_LEFT, I64Op, I64Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src2.is_constant) {
      uint32_t amt = i.src2.constant() & 0x3F;
      if (amt == 0) {
        if (i.src1.is_constant) {
          e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
        } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
          e.mov(i.dest, i.src1);
        }
      } else {
        if (i.src1.is_constant) {
          e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
          e.ror(i.dest, e.x0, static_cast<uint32_t>(64 - amt));
        } else {
          e.ror(i.dest, i.src1, static_cast<uint32_t>(64 - amt));
        }
      }
    } else {
      // Read shift amount first — dest may alias src2.
      e.mov(e.x0, XReg(i.src2.reg().getIdx()));
      if (i.src1.is_constant) {
        e.mov(i.dest, static_cast<uint64_t>(i.src1.constant()));
      } else if (i.dest.reg().getIdx() != i.src1.reg().getIdx()) {
        e.mov(i.dest, i.src1);
      }
      // ROL(x, n) = ROR(x, -n) since ROR uses amount mod 64
      e.neg(e.x0, e.x0);
      e.ror(i.dest, i.dest, e.x0);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROTATE_LEFT, ROTATE_LEFT_I8, ROTATE_LEFT_I16,
                     ROTATE_LEFT_I32, ROTATE_LEFT_I64);

// ============================================================================
// OPCODE_BYTE_SWAP
// ============================================================================
struct BYTE_SWAP_I16
    : Sequence<BYTE_SWAP_I16, I<OPCODE_BYTE_SWAP, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      uint16_t v = i.src1.constant();
      v = (v >> 8) | (v << 8);
      e.mov(i.dest, static_cast<uint64_t>(v));
    } else {
      e.rev16(i.dest, i.src1);
    }
  }
};
struct BYTE_SWAP_I32
    : Sequence<BYTE_SWAP_I32, I<OPCODE_BYTE_SWAP, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, static_cast<uint64_t>(xe::byte_swap(
                        static_cast<uint32_t>(i.src1.constant()))));
    } else {
      e.rev(i.dest, i.src1);
    }
  }
};
struct BYTE_SWAP_I64
    : Sequence<BYTE_SWAP_I64, I<OPCODE_BYTE_SWAP, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(i.dest, xe::byte_swap(static_cast<uint64_t>(i.src1.constant())));
    } else {
      e.rev(i.dest, i.src1);
    }
  }
};
struct BYTE_SWAP_V128
    : Sequence<BYTE_SWAP_V128, I<OPCODE_BYTE_SWAP, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int s = SrcVReg(e, i.src1, 0);
    e.rev32(VReg(i.dest.reg().getIdx()).b16, VReg(s).b16);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_BYTE_SWAP, BYTE_SWAP_I16, BYTE_SWAP_I32,
                     BYTE_SWAP_I64, BYTE_SWAP_V128);

// ============================================================================
// OPCODE_CNTLZ
// ============================================================================
struct CNTLZ_I8 : Sequence<CNTLZ_I8, I<OPCODE_CNTLZ, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      uint8_t v = static_cast<uint8_t>(i.src1.constant());
      uint8_t count = 0;
      while (count < 8 && !(v & 0x80)) {
        v <<= 1;
        count++;
      }
      e.mov(i.dest, static_cast<uint64_t>(count));
    } else {
      // clz operates on 32-bit, so shift left 24 to put byte in top.
      // OR a sentinel bit at position 23 so that a zero byte yields 8,
      // not 32.
      e.lsl(e.w0, i.src1, 24);
      e.orr(e.w0, e.w0, 1u << 23);
      e.clz(i.dest, e.w0);
    }
  }
};
struct CNTLZ_I16 : Sequence<CNTLZ_I16, I<OPCODE_CNTLZ, I8Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      uint16_t v = static_cast<uint16_t>(i.src1.constant());
      uint8_t count = 0;
      while (count < 16 && !(v & 0x8000)) {
        v <<= 1;
        count++;
      }
      e.mov(i.dest, static_cast<uint64_t>(count));
    } else {
      // Sentinel bit at position 15 caps the result at 16 for zero input.
      e.lsl(e.w0, i.src1, 16);
      e.orr(e.w0, e.w0, 1u << 15);
      e.clz(i.dest, e.w0);
    }
  }
};
struct CNTLZ_I32 : Sequence<CNTLZ_I32, I<OPCODE_CNTLZ, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      uint32_t v = static_cast<uint32_t>(i.src1.constant());
      e.mov(i.dest, static_cast<uint64_t>(xe::lzcnt(v)));
    } else {
      e.clz(i.dest, i.src1);
    }
  }
};
struct CNTLZ_I64 : Sequence<CNTLZ_I64, I<OPCODE_CNTLZ, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      uint64_t v = static_cast<uint64_t>(i.src1.constant());
      e.mov(i.dest, static_cast<uint64_t>(xe::lzcnt(v)));
    } else {
      // clz on XReg returns into XReg, we need WReg dest.
      e.clz(e.x0, i.src1);
      e.mov(i.dest, e.w0);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CNTLZ, CNTLZ_I8, CNTLZ_I16, CNTLZ_I32, CNTLZ_I64);

// ============================================================================
// Compare helpers
// ============================================================================
// ARM64: cmp src1, src2; cset dest, <cond>
// For I8/I16/I32 the dest is I8Op (WReg).
// For constants, load into scratch first.

#define DEFINE_COMPARE_XX(NAME, COND)                                          \
  struct NAME##_I8 : Sequence<NAME##_I8, I<OPCODE_##NAME, I8Op, I8Op, I8Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (TryEmitUnsignedZeroCompareFold(e, OPCODE_##NAME, i.dest, i.src1,     \
                                         i.src2)) {                            \
        return;                                                                \
      }                                                                        \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));          \
        if (i.src2.is_constant) {                                              \
          e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFF));        \
          e.cmp(e.w0, e.w1);                                                   \
        } else {                                                               \
          e.cmp(e.w0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint32_t imm = static_cast<uint32_t>(i.src2.constant() & 0xFF);        \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, imm);                                                  \
        } else {                                                               \
          e.mov(e.w0, static_cast<uint64_t>(imm));                             \
          e.cmp(i.src1, e.w0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I16                                                            \
      : Sequence<NAME##_I16, I<OPCODE_##NAME, I8Op, I16Op, I16Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (TryEmitUnsignedZeroCompareFold(e, OPCODE_##NAME, i.dest, i.src1,     \
                                         i.src2)) {                            \
        return;                                                                \
      }                                                                        \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));        \
        if (i.src2.is_constant) {                                              \
          e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));      \
          e.cmp(e.w0, e.w1);                                                   \
        } else {                                                               \
          e.cmp(e.w0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint32_t imm = static_cast<uint32_t>(i.src2.constant() & 0xFFFF);      \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, imm);                                                  \
        } else {                                                               \
          e.mov(e.w0, static_cast<uint64_t>(imm));                             \
          e.cmp(i.src1, e.w0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I32                                                            \
      : Sequence<NAME##_I32, I<OPCODE_##NAME, I8Op, I32Op, I32Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (TryEmitUnsignedZeroCompareFold(e, OPCODE_##NAME, i.dest, i.src1,     \
                                         i.src2)) {                            \
        return;                                                                \
      }                                                                        \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(                                     \
                        static_cast<uint32_t>(i.src1.constant())));            \
        if (i.src2.is_constant) {                                              \
          e.mov(e.w1, static_cast<uint64_t>(                                   \
                          static_cast<uint32_t>(i.src2.constant())));          \
          e.cmp(e.w0, e.w1);                                                   \
        } else {                                                               \
          e.cmp(e.w0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint32_t imm = static_cast<uint32_t>(i.src2.constant());               \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, imm);                                                  \
        } else {                                                               \
          e.mov(e.w0, static_cast<uint64_t>(imm));                             \
          e.cmp(i.src1, e.w0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I64                                                            \
      : Sequence<NAME##_I64, I<OPCODE_##NAME, I8Op, I64Op, I64Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (TryEmitUnsignedZeroCompareFold(e, OPCODE_##NAME, i.dest, i.src1,     \
                                         i.src2)) {                            \
        return;                                                                \
      }                                                                        \
      if (i.src1.is_constant) {                                                \
        e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));                 \
        if (i.src2.is_constant) {                                              \
          e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));               \
          e.cmp(e.x0, e.x1);                                                   \
        } else {                                                               \
          e.cmp(e.x0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint64_t imm = static_cast<uint64_t>(i.src2.constant());               \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, static_cast<uint32_t>(imm));                           \
        } else {                                                               \
          e.mov(e.x0, imm);                                                    \
          e.cmp(i.src1, e.x0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct _tag_##NAME {}

DEFINE_COMPARE_XX(COMPARE_EQ, EQ);
DEFINE_COMPARE_XX(COMPARE_NE, NE);
// Signed I8/I16 comparisons need sign-extension to 32-bit because ARM64
// cmp always operates on full 32-bit WRegs. Without sign-extension,
// 0xFF (which is -1 as signed I8) would compare as 255, giving wrong results.
#define DEFINE_SIGNED_COMPARE_XX(NAME, COND)                                   \
  struct NAME##_I8 : Sequence<NAME##_I8, I<OPCODE_##NAME, I8Op, I8Op, I8Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));          \
      } else {                                                                 \
        e.mov(e.w0, i.src1);                                                   \
      }                                                                        \
      e.sxtb(e.w0, e.w0);                                                      \
      if (i.src2.is_constant) {                                                \
        e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFF));          \
        e.sxtb(e.w1, e.w1);                                                    \
        e.cmp(e.w0, e.w1);                                                     \
      } else {                                                                 \
        e.sxtb(e.w1, i.src2);                                                  \
        e.cmp(e.w0, e.w1);                                                     \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I16                                                            \
      : Sequence<NAME##_I16, I<OPCODE_##NAME, I8Op, I16Op, I16Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));        \
      } else {                                                                 \
        e.mov(e.w0, i.src1);                                                   \
      }                                                                        \
      e.sxth(e.w0, e.w0);                                                      \
      if (i.src2.is_constant) {                                                \
        e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));        \
        e.sxth(e.w1, e.w1);                                                    \
        e.cmp(e.w0, e.w1);                                                     \
      } else {                                                                 \
        e.sxth(e.w1, i.src2);                                                  \
        e.cmp(e.w0, e.w1);                                                     \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I32                                                            \
      : Sequence<NAME##_I32, I<OPCODE_##NAME, I8Op, I32Op, I32Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.w0, static_cast<uint64_t>(                                     \
                        static_cast<uint32_t>(i.src1.constant())));            \
        if (i.src2.is_constant) {                                              \
          e.mov(e.w1, static_cast<uint64_t>(                                   \
                          static_cast<uint32_t>(i.src2.constant())));          \
          e.cmp(e.w0, e.w1);                                                   \
        } else {                                                               \
          e.cmp(e.w0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint32_t imm = static_cast<uint32_t>(i.src2.constant());               \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, imm);                                                  \
        } else {                                                               \
          e.mov(e.w0, static_cast<uint64_t>(imm));                             \
          e.cmp(i.src1, e.w0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct NAME##_I64                                                            \
      : Sequence<NAME##_I64, I<OPCODE_##NAME, I8Op, I64Op, I64Op>> {           \
    static void Emit(A64Emitter& e, const EmitArgType& i) {                    \
      if (i.src1.is_constant) {                                                \
        e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));                 \
        if (i.src2.is_constant) {                                              \
          e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));               \
          e.cmp(e.x0, e.x1);                                                   \
        } else {                                                               \
          e.cmp(e.x0, i.src2);                                                 \
        }                                                                      \
      } else if (i.src2.is_constant) {                                         \
        uint64_t imm = static_cast<uint64_t>(i.src2.constant());               \
        if (imm <= 4095) {                                                     \
          e.cmp(i.src1, static_cast<uint32_t>(imm));                           \
        } else {                                                               \
          e.mov(e.x0, imm);                                                    \
          e.cmp(i.src1, e.x0);                                                 \
        }                                                                      \
      } else {                                                                 \
        e.cmp(i.src1, i.src2);                                                 \
      }                                                                        \
      e.cset(i.dest, Xbyak_aarch64::COND);                                     \
    }                                                                          \
  };                                                                           \
  struct _tag_##NAME {}

DEFINE_SIGNED_COMPARE_XX(COMPARE_SLT, LT);
DEFINE_SIGNED_COMPARE_XX(COMPARE_SLE, LE);
DEFINE_SIGNED_COMPARE_XX(COMPARE_SGT, GT);
DEFINE_SIGNED_COMPARE_XX(COMPARE_SGE, GE);
DEFINE_COMPARE_XX(COMPARE_ULT, LO);
DEFINE_COMPARE_XX(COMPARE_ULE, LS);
DEFINE_COMPARE_XX(COMPARE_UGT, HI);
DEFINE_COMPARE_XX(COMPARE_UGE, HS);

#undef DEFINE_COMPARE_XX

// Integer-only compare registrations are deferred until after float
// compare definitions below.

// ============================================================================
// OPCODE_SELECT
// ============================================================================
// dest = src1 ? src2 : src3
struct SELECT_I8
    : Sequence<SELECT_I8, I<OPCODE_SELECT, I8Op, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    bool src2_zero = i.src2.is_constant && ((i.src2.constant() & 0xFF) == 0);
    bool src3_zero = i.src3.is_constant && ((i.src3.constant() & 0xFF) == 0);
    if (i.src2.is_constant && !src2_zero) {
      e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFF));
    }
    if (i.src3.is_constant && !src3_zero) {
      e.mov(e.w2, static_cast<uint64_t>(i.src3.constant() & 0xFF));
    }
    WReg s2 = src2_zero ? e.wzr
                        : (i.src2.is_constant ? e.w1
                                              : WReg(i.src2.reg().getIdx()));
    WReg s3 = src3_zero ? e.wzr
                        : (i.src3.is_constant ? e.w2
                                              : WReg(i.src3.reg().getIdx()));
    e.csel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_I16
    : Sequence<SELECT_I16, I<OPCODE_SELECT, I16Op, I8Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    bool src2_zero = i.src2.is_constant && ((i.src2.constant() & 0xFFFF) == 0);
    bool src3_zero = i.src3.is_constant && ((i.src3.constant() & 0xFFFF) == 0);
    if (i.src2.is_constant && !src2_zero) {
      e.mov(e.w1, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
    }
    if (i.src3.is_constant && !src3_zero) {
      e.mov(e.w2, static_cast<uint64_t>(i.src3.constant() & 0xFFFF));
    }
    WReg s2 = src2_zero ? e.wzr
                        : (i.src2.is_constant ? e.w1
                                              : WReg(i.src2.reg().getIdx()));
    WReg s3 = src3_zero ? e.wzr
                        : (i.src3.is_constant ? e.w2
                                              : WReg(i.src3.reg().getIdx()));
    e.csel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_I32
    : Sequence<SELECT_I32, I<OPCODE_SELECT, I32Op, I8Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    bool src2_zero =
        i.src2.is_constant && (static_cast<uint32_t>(i.src2.constant()) == 0);
    bool src3_zero =
        i.src3.is_constant && (static_cast<uint32_t>(i.src3.constant()) == 0);
    if (i.src2.is_constant && !src2_zero) {
      e.mov(e.w1,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
    }
    if (i.src3.is_constant && !src3_zero) {
      e.mov(e.w2,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src3.constant())));
    }
    WReg s2 = src2_zero ? e.wzr
                        : (i.src2.is_constant ? e.w1
                                              : WReg(i.src2.reg().getIdx()));
    WReg s3 = src3_zero ? e.wzr
                        : (i.src3.is_constant ? e.w2
                                              : WReg(i.src3.reg().getIdx()));
    e.csel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_I64
    : Sequence<SELECT_I64, I<OPCODE_SELECT, I64Op, I8Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    bool src2_zero =
        i.src2.is_constant && (static_cast<uint64_t>(i.src2.constant()) == 0);
    bool src3_zero =
        i.src3.is_constant && (static_cast<uint64_t>(i.src3.constant()) == 0);
    if (i.src2.is_constant && !src2_zero) {
      e.mov(e.x1, static_cast<uint64_t>(i.src2.constant()));
    }
    if (i.src3.is_constant && !src3_zero) {
      e.mov(e.x2, static_cast<uint64_t>(i.src3.constant()));
    }
    XReg s2 = src2_zero ? e.xzr
                        : (i.src2.is_constant ? e.x1
                                              : XReg(i.src2.reg().getIdx()));
    XReg s3 = src3_zero ? e.xzr
                        : (i.src3.is_constant ? e.x2
                                              : XReg(i.src3.reg().getIdx()));
    e.csel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_F32
    : Sequence<SELECT_F32, I<OPCODE_SELECT, F32Op, I8Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w1, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w1);
    }
    if (i.src3.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src3.constant();
      e.mov(e.w1, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w1);
    }
    SReg s2 = i.src2.is_constant ? e.s0 : SReg(i.src2.reg().getIdx());
    SReg s3 = i.src3.is_constant ? e.s1 : SReg(i.src3.reg().getIdx());
    e.fcsel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_F64
    : Sequence<SELECT_F64, I<OPCODE_SELECT, F64Op, I8Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    WReg cond = i.src1.is_constant ? e.w0 : WReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    }
    e.cmp(cond, 0);
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x1, c.u);
      e.fmov(e.d0, e.x1);
    }
    if (i.src3.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src3.constant();
      e.mov(e.x1, c.u);
      e.fmov(e.d1, e.x1);
    }
    DReg s2 = i.src2.is_constant ? e.d0 : DReg(i.src2.reg().getIdx());
    DReg s3 = i.src3.is_constant ? e.d1 : DReg(i.src3.reg().getIdx());
    e.fcsel(i.dest, s2, s3, Xbyak_aarch64::NE);
  }
};
struct SELECT_V128_V128
    : Sequence<SELECT_V128_V128,
               I<OPCODE_SELECT, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    int d = i.dest.reg().getIdx();
    int s1 = SrcVReg(e, i.src1, 0);  // condition mask
    int s2 = SrcVReg(e, i.src2, 1);  // value for mask=0
    int s3 = SrcVReg(e, i.src3, 2);  // value for mask=1
    // PPC vsel / HIR SELECT V128: bit=1 → src3, bit=0 → src2
    // ARM64 BIT: dest = (op1 & mask) | (dest & ~mask) — keeps dest where mask=0
    // ARM64 BIF: dest = (dest & mask) | (op1 & ~mask) — keeps dest where mask=1
    // Use BIT/BIF to avoid clobbering when dest aliases an operand.
    if (d == s1) {
      // dest already holds the mask. BSL is safe here.
      e.bsl(VReg(d).b16, VReg(s3).b16, VReg(s2).b16);
    } else if (d == s3) {
      // dest holds the mask=1 value. BIT inserts mask=1 bits from s3, keeps
      // dest (=s2-candidate) where mask=0... no, dest=s3 not s2.
      // Use: copy s2 to scratch, then BIT(scratch, s3, mask), move to dest.
      // Or: copy mask to scratch v0, copy s2 to dest, BIT(dest, s3_orig, v0).
      // Simplest: use scratch v0 for mask, then BSL.
      e.orr(VReg(0).b16, VReg(s1).b16, VReg(s1).b16);  // v0 = mask
      e.bsl(VReg(0).b16, VReg(s3).b16, VReg(s2).b16);  // v0 = result
      e.orr(VReg(d).b16, VReg(0).b16, VReg(0).b16);    // dest = result
    } else if (d == s2) {
      // dest holds the mask=0 value. BIF inserts ~mask bits from s2,
      // but dest=s2... Use scratch for mask.
      e.orr(VReg(0).b16, VReg(s1).b16, VReg(s1).b16);  // v0 = mask
      e.bsl(VReg(0).b16, VReg(s3).b16, VReg(s2).b16);  // v0 = result
      e.orr(VReg(d).b16, VReg(0).b16, VReg(0).b16);    // dest = result
    } else {
      // No aliasing — copy mask to dest, then BSL.
      e.orr(VReg(d).b16, VReg(s1).b16, VReg(s1).b16);
      e.bsl(VReg(d).b16, VReg(s3).b16, VReg(s2).b16);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SELECT, SELECT_I8, SELECT_I16, SELECT_I32,
                     SELECT_I64, SELECT_F32, SELECT_F64, SELECT_V128_V128);

// ============================================================================
// OPCODE_IS_TRUE / OPCODE_IS_FALSE
// ============================================================================
template <typename T>
static void EmitIsTrueI32(A64Emitter& e, const T& i,
                          Xbyak_aarch64::Cond cond) {
  if (i.src1.is_constant) {
    e.mov(i.dest, i.src1.constant() != 0 ? 1 : 0);
    if (cond == Xbyak_aarch64::EQ) {
      e.eor(i.dest, i.dest, 1);
    }
    return;
  }
  e.cmp(i.src1, 0);
  e.cset(i.dest, cond);
}

template <typename T>
static void EmitIsTrueI64(A64Emitter& e, const T& i,
                          Xbyak_aarch64::Cond cond) {
  if (i.src1.is_constant) {
    e.mov(i.dest, i.src1.constant() != 0 ? 1 : 0);
    if (cond == Xbyak_aarch64::EQ) {
      e.eor(i.dest, i.dest, 1);
    }
    return;
  }
  e.cmp(i.src1, 0);
  e.cset(i.dest, cond);
}

template <typename T>
static void EmitIsTrueF32(A64Emitter& e, const T& i,
                          Xbyak_aarch64::Cond cond) {
  if (i.src1.is_constant) {
    const uint32_t bits = xe::memory::Reinterpret<uint32_t>(i.src1.constant());
    e.mov(i.dest, bits != 0 ? 1 : 0);
    if (cond == Xbyak_aarch64::EQ) {
      e.eor(i.dest, i.dest, 1);
    }
    return;
  }
  e.fmov(e.w0, i.src1);
  e.cmp(e.w0, 0);
  e.cset(i.dest, cond);
}

template <typename T>
static void EmitIsTrueF64(A64Emitter& e, const T& i,
                          Xbyak_aarch64::Cond cond) {
  if (i.src1.is_constant) {
    const uint64_t bits = xe::memory::Reinterpret<uint64_t>(i.src1.constant());
    e.mov(i.dest, bits != 0 ? 1 : 0);
    if (cond == Xbyak_aarch64::EQ) {
      e.eor(i.dest, i.dest, 1);
    }
    return;
  }
  e.fmov(e.x0, i.src1);
  e.cmp(e.x0, 0);
  e.cset(i.dest, cond);
}

template <typename T>
static void EmitIsTrueV128(A64Emitter& e, const T& i,
                           Xbyak_aarch64::Cond cond) {
  if (i.src1.is_constant) {
    const vec128_t& value = i.src1.constant();
    e.mov(i.dest, (value.low | value.high) != 0 ? 1 : 0);
    if (cond == Xbyak_aarch64::EQ) {
      e.eor(i.dest, i.dest, 1);
    }
    return;
  }
  const int src = i.src1.reg().getIdx();
  e.umov(e.x0, VReg(src).d2[0]);
  e.umov(e.x1, VReg(src).d2[1]);
  e.orr(e.x0, e.x0, e.x1);
  e.cmp(e.x0, 0);
  e.cset(i.dest, cond);
}

struct IS_TRUE_I8 : Sequence<IS_TRUE_I8, I<OPCODE_IS_TRUE, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueI32(e, i, Xbyak_aarch64::NE);
  }
};
struct IS_TRUE_I16 : Sequence<IS_TRUE_I16, I<OPCODE_IS_TRUE, I8Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueI32(e, i, Xbyak_aarch64::NE);
  }
};
struct IS_TRUE_I32 : Sequence<IS_TRUE_I32, I<OPCODE_IS_TRUE, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueI32(e, i, Xbyak_aarch64::NE);
  }
};
struct IS_TRUE_I64 : Sequence<IS_TRUE_I64, I<OPCODE_IS_TRUE, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueI64(e, i, Xbyak_aarch64::NE);
  }
};
struct IS_TRUE_F32 : Sequence<IS_TRUE_F32, I<OPCODE_IS_TRUE, I8Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueF32(e, i, Xbyak_aarch64::NE);
  }
};
struct IS_TRUE_F64 : Sequence<IS_TRUE_F64, I<OPCODE_IS_TRUE, I8Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueF64(e, i, Xbyak_aarch64::NE);
  }
};
struct IS_TRUE_V128
    : Sequence<IS_TRUE_V128, I<OPCODE_IS_TRUE, I8Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueV128(e, i, Xbyak_aarch64::NE);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_TRUE, IS_TRUE_I8, IS_TRUE_I16, IS_TRUE_I32,
                     IS_TRUE_I64, IS_TRUE_F32, IS_TRUE_F64, IS_TRUE_V128);

struct IS_FALSE_I8 : Sequence<IS_FALSE_I8, I<OPCODE_IS_FALSE, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueI32(e, i, Xbyak_aarch64::EQ);
  }
};
struct IS_FALSE_I16
    : Sequence<IS_FALSE_I16, I<OPCODE_IS_FALSE, I8Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueI32(e, i, Xbyak_aarch64::EQ);
  }
};
struct IS_FALSE_I32
    : Sequence<IS_FALSE_I32, I<OPCODE_IS_FALSE, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueI32(e, i, Xbyak_aarch64::EQ);
  }
};
struct IS_FALSE_I64
    : Sequence<IS_FALSE_I64, I<OPCODE_IS_FALSE, I8Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueI64(e, i, Xbyak_aarch64::EQ);
  }
};
struct IS_FALSE_F32
    : Sequence<IS_FALSE_F32, I<OPCODE_IS_FALSE, I8Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueF32(e, i, Xbyak_aarch64::EQ);
  }
};
struct IS_FALSE_F64
    : Sequence<IS_FALSE_F64, I<OPCODE_IS_FALSE, I8Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueF64(e, i, Xbyak_aarch64::EQ);
  }
};
struct IS_FALSE_V128
    : Sequence<IS_FALSE_V128, I<OPCODE_IS_FALSE, I8Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitIsTrueV128(e, i, Xbyak_aarch64::EQ);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_FALSE, IS_FALSE_I8, IS_FALSE_I16, IS_FALSE_I32,
                     IS_FALSE_I64, IS_FALSE_F32, IS_FALSE_F64, IS_FALSE_V128);

// ============================================================================
// OPCODE_LOAD_LOCAL
// ============================================================================
// Note: all types are always aligned on the stack.
// For large offsets that don't fit in the unsigned immediate field of
// LDR/STR, compute the effective address in a temp register first.
static inline bool LocalOffsetFitsImm(uint32_t offset, uint32_t scale) {
  return (offset % scale) == 0 && (offset / scale) <= 0xFFF;
}
// Compute base register for local access; returns {base, imm} pair.
// If the offset fits the scaled immediate, returns {sp, offset}.
// Otherwise loads sp+offset into x17 and returns {x17, 0}.
static inline XReg PrepareLocalBase(A64Emitter& e, uint32_t offset,
                                    uint32_t scale) {
  if (LocalOffsetFitsImm(offset, scale)) {
    return e.sp;
  }
  e.mov(e.x17, static_cast<uint64_t>(offset));
  e.add(e.x17, e.sp, e.x17);
  return e.x17;
}
static inline uint32_t PrepareLocalImm(uint32_t offset, uint32_t scale) {
  if (LocalOffsetFitsImm(offset, scale)) {
    return offset;
  }
  return 0;
}

struct LOAD_LOCAL_I8
    : Sequence<LOAD_LOCAL_I8, I<OPCODE_LOAD_LOCAL, I8Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 1);
    e.ldrb(i.dest, ptr(base, PrepareLocalImm(off, 1)));
  }
};
struct LOAD_LOCAL_I16
    : Sequence<LOAD_LOCAL_I16, I<OPCODE_LOAD_LOCAL, I16Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 2);
    e.ldrh(i.dest, ptr(base, PrepareLocalImm(off, 2)));
  }
};
struct LOAD_LOCAL_I32
    : Sequence<LOAD_LOCAL_I32, I<OPCODE_LOAD_LOCAL, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 4);
    e.ldr(i.dest, ptr(base, PrepareLocalImm(off, 4)));
  }
};
struct LOAD_LOCAL_I64
    : Sequence<LOAD_LOCAL_I64, I<OPCODE_LOAD_LOCAL, I64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 8);
    e.ldr(i.dest, ptr(base, PrepareLocalImm(off, 8)));
  }
};
struct LOAD_LOCAL_F32
    : Sequence<LOAD_LOCAL_F32, I<OPCODE_LOAD_LOCAL, F32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 4);
    e.ldr(i.dest, ptr(base, PrepareLocalImm(off, 4)));
  }
};
struct LOAD_LOCAL_F64
    : Sequence<LOAD_LOCAL_F64, I<OPCODE_LOAD_LOCAL, F64Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 8);
    e.ldr(i.dest, ptr(base, PrepareLocalImm(off, 8)));
  }
};
struct LOAD_LOCAL_V128
    : Sequence<LOAD_LOCAL_V128, I<OPCODE_LOAD_LOCAL, V128Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 16);
    e.ldr(i.dest, ptr(base, PrepareLocalImm(off, 16)));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOAD_LOCAL, LOAD_LOCAL_I8, LOAD_LOCAL_I16,
                     LOAD_LOCAL_I32, LOAD_LOCAL_I64, LOAD_LOCAL_F32,
                     LOAD_LOCAL_F64, LOAD_LOCAL_V128);

// ============================================================================
// OPCODE_STORE_LOCAL
// ============================================================================
struct STORE_LOCAL_I8
    : Sequence<STORE_LOCAL_I8, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 1);
    uint32_t imm = PrepareLocalImm(off, 1);
    if (i.src2.is_constant) {
      if ((i.src2.constant() & 0xFF) == 0) {
        e.strb(e.wzr, ptr(base, imm));
      } else {
        e.mov(e.w0, static_cast<uint64_t>(i.src2.constant() & 0xFF));
        e.strb(e.w0, ptr(base, imm));
      }
    } else {
      e.strb(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_I16
    : Sequence<STORE_LOCAL_I16, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 2);
    uint32_t imm = PrepareLocalImm(off, 2);
    if (i.src2.is_constant) {
      if ((i.src2.constant() & 0xFFFF) == 0) {
        e.strh(e.wzr, ptr(base, imm));
      } else {
        e.mov(e.w0, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
        e.strh(e.w0, ptr(base, imm));
      }
    } else {
      e.strh(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_I32
    : Sequence<STORE_LOCAL_I32, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 4);
    uint32_t imm = PrepareLocalImm(off, 4);
    if (i.src2.is_constant) {
      if (i.src2.constant() == 0) {
        e.str(e.wzr, ptr(base, imm));
      } else {
        e.mov(e.w0,
              static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
        e.str(e.w0, ptr(base, imm));
      }
    } else {
      e.str(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_I64
    : Sequence<STORE_LOCAL_I64, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 8);
    uint32_t imm = PrepareLocalImm(off, 8);
    if (i.src2.is_constant) {
      if (i.src2.constant() == 0) {
        e.str(e.xzr, ptr(base, imm));
      } else {
        e.mov(e.x0, static_cast<uint64_t>(i.src2.constant()));
        e.str(e.x0, ptr(base, imm));
      }
    } else {
      e.str(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_F32
    : Sequence<STORE_LOCAL_F32, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 4);
    uint32_t imm = PrepareLocalImm(off, 4);
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.str(e.w0, ptr(base, imm));
    } else {
      e.str(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_F64
    : Sequence<STORE_LOCAL_F64, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 8);
    uint32_t imm = PrepareLocalImm(off, 8);
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.str(e.x0, ptr(base, imm));
    } else {
      e.str(i.src2, ptr(base, imm));
    }
  }
};
struct STORE_LOCAL_V128
    : Sequence<STORE_LOCAL_V128, I<OPCODE_STORE_LOCAL, VoidOp, I32Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    uint32_t off = static_cast<uint32_t>(i.src1.constant());
    auto base = PrepareLocalBase(e, off, 16);
    uint32_t imm = PrepareLocalImm(off, 16);
    if (i.src2.is_constant) {
      LoadV128Const(e, 0, i.src2.constant());
      e.str(QReg(0), ptr(base, imm));
    } else {
      e.str(i.src2, ptr(base, imm));
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_STORE_LOCAL, STORE_LOCAL_I8, STORE_LOCAL_I16,
                     STORE_LOCAL_I32, STORE_LOCAL_I64, STORE_LOCAL_F32,
                     STORE_LOCAL_F64, STORE_LOCAL_V128);

// ============================================================================
// OPCODE_CAST
// ============================================================================
struct CAST_I32_F32 : Sequence<CAST_I32_F32, I<OPCODE_CAST, I32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Bitcast float -> int (not conversion).
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(i.dest, static_cast<uint64_t>(c.u));
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
struct CAST_I64_F64 : Sequence<CAST_I64_F64, I<OPCODE_CAST, I64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(i.dest, c.u);
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
struct CAST_F32_I32 : Sequence<CAST_F32_I32, I<OPCODE_CAST, F32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      e.fmov(i.dest, e.w0);
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
struct CAST_F64_I64 : Sequence<CAST_F64_I64, I<OPCODE_CAST, F64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
      e.fmov(i.dest, e.x0);
    } else {
      e.fmov(i.dest, i.src1);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CAST, CAST_I32_F32, CAST_I64_F64, CAST_F32_I32,
                     CAST_F64_I64);

// ============================================================================
// OPCODE_DID_SATURATE
// ============================================================================
struct DID_SATURATE
    : Sequence<DID_SATURATE, I<OPCODE_DID_SATURATE, I8Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // TODO(has207): Implement saturation tracking. ARM64 NEON saturating
    // ops (sqadd/uqadd/etc.) set FPSR.QC — clear it before the saturating
    // op, then read it here with mrs. Requires coordinating with all
    // ARITHMETIC_SATURATE vector paths. Always returns 0 for now (same as
    // x64 backend).
    e.mov(i.dest, 0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DID_SATURATE, DID_SATURATE);

// ============================================================================
// OPCODE_MAX / OPCODE_MIN (scalar)
// ============================================================================
struct MAX_F32 : Sequence<MAX_F32, I<OPCODE_MAX, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fmax(i.dest, e.s0, i.src2.is_constant ? e.s1 : i.src2);
    } else if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fmax(i.dest, i.src1, e.s0);
    } else {
      e.fmax(i.dest, i.src1, i.src2);
    }
  }
};
struct MAX_F64 : Sequence<MAX_F64, I<OPCODE_MAX, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fmax(i.dest, e.d0, i.src2.is_constant ? e.d1 : i.src2);
    } else if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fmax(i.dest, i.src1, e.d0);
    } else {
      e.fmax(i.dest, i.src1, i.src2);
    }
  }
};
struct MAX_V128 : Sequence<MAX_V128, I<OPCODE_MAX, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      int s1, s2;
      PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
      e.fmax(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);
      // PPC vmaxfp: if either input is NaN, result = src1 (vA).
      FixupVmxMaxMinNan(e);
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 2, 0, 1);
      }
      e.mov(VReg(i.dest.reg().getIdx()).b16, VReg(2).b16);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MAX, MAX_F32, MAX_F64, MAX_V128);

// MIN has signed semantics (HIR builder constant-folds using CompareSLT).
// I8/I16 need sign-extension; all need signed condition code (LT not LO).
struct MIN_I8 : Sequence<MIN_I8, I<OPCODE_MIN, I8Op, I8Op, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFF));
    } else {
      e.mov(e.w0, i.src1);
    }
    e.sxtb(e.w0, e.w0);
    if (i.src2.is_constant) {
      e.mov(e.w17, static_cast<uint64_t>(i.src2.constant() & 0xFF));
      e.sxtb(e.w17, e.w17);
    } else {
      e.sxtb(e.w17, i.src2);
    }
    e.cmp(e.w0, e.w17);
    e.csel(i.dest, e.w0, e.w17, LT);
  }
};
struct MIN_I16 : Sequence<MIN_I16, I<OPCODE_MIN, I16Op, I16Op, I16Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0, static_cast<uint64_t>(i.src1.constant() & 0xFFFF));
    } else {
      e.mov(e.w0, i.src1);
    }
    e.sxth(e.w0, e.w0);
    if (i.src2.is_constant) {
      e.mov(e.w17, static_cast<uint64_t>(i.src2.constant() & 0xFFFF));
      e.sxth(e.w17, e.w17);
    } else {
      e.sxth(e.w17, i.src2);
    }
    e.cmp(e.w0, e.w17);
    e.csel(i.dest, e.w0, e.w17, LT);
  }
};
struct MIN_I32 : Sequence<MIN_I32, I<OPCODE_MIN, I32Op, I32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
    } else {
      e.mov(e.w0, i.src1);
    }
    if (i.src2.is_constant) {
      e.mov(e.w17,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src2.constant())));
    } else {
      e.mov(e.w17, i.src2);
    }
    e.cmp(e.w0, e.w17);
    e.csel(i.dest, e.w0, e.w17, LT);
  }
};
struct MIN_I64 : Sequence<MIN_I64, I<OPCODE_MIN, I64Op, I64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
    } else {
      e.mov(e.x0, i.src1);
    }
    if (i.src2.is_constant) {
      e.mov(e.x17, static_cast<uint64_t>(i.src2.constant()));
    } else {
      e.mov(e.x17, i.src2);
    }
    e.cmp(e.x0, e.x17);
    e.csel(i.dest, e.x0, e.x17, LT);
  }
};
struct MIN_F32 : Sequence<MIN_F32, I<OPCODE_MIN, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fmin(i.dest, e.s0, i.src2.is_constant ? e.s1 : i.src2);
    } else if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fmin(i.dest, i.src1, e.s0);
    } else {
      e.fmin(i.dest, i.src1, i.src2);
    }
  }
};
struct MIN_F64 : Sequence<MIN_F64, I<OPCODE_MIN, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fmin(i.dest, e.d0, i.src2.is_constant ? e.d1 : i.src2);
    } else if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fmin(i.dest, i.src1, e.d0);
    } else {
      e.fmin(i.dest, i.src1, i.src2);
    }
  }
};
struct MIN_V128 : Sequence<MIN_V128, I<OPCODE_MIN, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      int s1, s2;
      PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
      e.fmin(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);
      // PPC vminfp: if either input is NaN, result = src1 (vA).
      FixupVmxMaxMinNan(e);
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 2, 0, 1);
      }
      e.mov(VReg(i.dest.reg().getIdx()).b16, VReg(2).b16);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MIN, MIN_I8, MIN_I16, MIN_I32, MIN_I64, MIN_F32,
                     MIN_F64, MIN_V128);

// ============================================================================
// OPCODE_CONVERT
// ============================================================================
struct CONVERT_I32_F32
    : Sequence<CONVERT_I32_F32, I<OPCODE_CONVERT, I32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    SReg src = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.fcvtzs(i.dest, src);
    } else {
      e.frintx(e.s0, src);
      e.fcvtzs(i.dest, e.s0);
    }
  }
};
struct CONVERT_I32_F64
    : Sequence<CONVERT_I32_F64, I<OPCODE_CONVERT, I32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    DReg src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.fcvtzs(i.dest, src);
    } else {
      // Use current FPCR rounding mode: round first, then truncate.
      e.frintx(e.d0, src);
      e.fcvtzs(i.dest, e.d0);
    }
  }
};
struct CONVERT_I64_F64
    : Sequence<CONVERT_I64_F64, I<OPCODE_CONVERT, I64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    DReg src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    if (i.instr->flags == ROUND_TO_ZERO) {
      e.fcvtzs(i.dest, src);
    } else {
      e.frintx(e.d0, src);
      e.fcvtzs(i.dest, e.d0);
    }
  }
};
struct CONVERT_F32_I32
    : Sequence<CONVERT_F32_I32, I<OPCODE_CONVERT, F32Op, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.w0,
            static_cast<uint64_t>(static_cast<uint32_t>(i.src1.constant())));
      e.scvtf(i.dest, e.w0);
    } else {
      e.scvtf(i.dest, i.src1);
    }
  }
};
struct CONVERT_F64_I64
    : Sequence<CONVERT_F64_I64, I<OPCODE_CONVERT, F64Op, I64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      e.mov(e.x0, static_cast<uint64_t>(i.src1.constant()));
      e.scvtf(i.dest, e.x0);
    } else {
      e.scvtf(i.dest, i.src1);
    }
  }
};
struct CONVERT_F32_F64
    : Sequence<CONVERT_F32_F64, I<OPCODE_CONVERT, F32Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fcvt(i.dest, e.d0);
    } else {
      e.fcvt(i.dest, i.src1);
    }
  }
};
struct CONVERT_F64_F32
    : Sequence<CONVERT_F64_F32, I<OPCODE_CONVERT, F64Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fcvt(i.dest, e.s0);
    } else {
      e.fcvt(i.dest, i.src1);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_CONVERT, CONVERT_I32_F32, CONVERT_I32_F64,
                     CONVERT_I64_F64, CONVERT_F32_I32, CONVERT_F64_I64,
                     CONVERT_F32_F64, CONVERT_F64_F32);

// ============================================================================
// OPCODE_ROUND
// ============================================================================
struct ROUND_F32 : Sequence<ROUND_F32, I<OPCODE_ROUND, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Round mode is in i.instr->flags.
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    auto src = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.frintz(i.dest, src);
        break;
      case ROUND_TO_NEAREST:
        e.frintn(i.dest, src);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.frintm(i.dest, src);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.frintp(i.dest, src);
        break;
      default:
        // ROUND_DYNAMIC - use current rounding mode.
        e.frinti(i.dest, src);
        break;
    }
  }
};
struct ROUND_F64 : Sequence<ROUND_F64, I<OPCODE_ROUND, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    auto src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    switch (i.instr->flags) {
      case ROUND_TO_ZERO:
        e.frintz(i.dest, src);
        break;
      case ROUND_TO_NEAREST:
        e.frintn(i.dest, src);
        break;
      case ROUND_TO_MINUS_INFINITY:
        e.frintm(i.dest, src);
        break;
      case ROUND_TO_POSITIVE_INFINITY:
        e.frintp(i.dest, src);
        break;
      default:
        e.frinti(i.dest, src);
        break;
    }
  }
};
struct ROUND_V128 : Sequence<ROUND_V128, I<OPCODE_ROUND, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      int s = SrcVReg(e, i.src1, 0);
      auto src = VReg(s).s4;
      auto dst = VReg(i.dest.reg().getIdx()).s4;
      switch (i.instr->flags) {
        case ROUND_TO_ZERO:
          e.frintz(dst, src);
          break;
        case ROUND_TO_NEAREST:
          e.frintn(dst, src);
          break;
        case ROUND_TO_MINUS_INFINITY:
          e.frintm(dst, src);
          break;
        case ROUND_TO_POSITIVE_INFINITY:
          e.frintp(dst, src);
          break;
        default:
          // ROUND_DYNAMIC - use current rounding mode.
          e.frinti(dst, src);
          break;
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_ROUND, ROUND_F32, ROUND_F64, ROUND_V128);

// ============================================================================
// OPCODE_SQRT
// ============================================================================
struct SQRT_F32 : Sequence<SQRT_F32, I<OPCODE_SQRT, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fsqrt(i.dest, e.s0);
    } else {
      e.fsqrt(i.dest, i.src1);
    }
  }
};
struct SQRT_F64 : Sequence<SQRT_F64, I<OPCODE_SQRT, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fsqrt(i.dest, e.d0);
    } else {
      e.fsqrt(i.dest, i.src1);
    }
  }
};
struct SQRT_V128 : Sequence<SQRT_V128, I<OPCODE_SQRT, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      int s = SrcVReg(e, i.src1, 0);
      e.fsqrt(VReg(i.dest.reg().getIdx()).s4, VReg(s).s4);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SQRT, SQRT_F32, SQRT_F64, SQRT_V128);

// ============================================================================
// OPCODE_IS_NAN
// ============================================================================
struct IS_NAN_F32 : Sequence<IS_NAN_F32, I<OPCODE_IS_NAN, I8Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fcmp(e.s0, e.s0);
    } else {
      e.fcmp(i.src1, i.src1);
    }
    // VS (overflow) set when either operand is NaN.
    e.cset(i.dest, Xbyak_aarch64::VS);
  }
};
struct IS_NAN_F64 : Sequence<IS_NAN_F64, I<OPCODE_IS_NAN, I8Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fcmp(e.d0, e.d0);
    } else {
      e.fcmp(i.src1, i.src1);
    }
    e.cset(i.dest, Xbyak_aarch64::VS);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_IS_NAN, IS_NAN_F32, IS_NAN_F64);

// ============================================================================
// OPCODE_COMPARE_EQ/NE for float
// ============================================================================
struct COMPARE_EQ_F32
    : Sequence<COMPARE_EQ_F32, I<OPCODE_COMPARE_EQ, I8Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      if (i.src2.is_constant) {
        union {
          float f;
          uint32_t u;
        } c2;
        c2.f = i.src2.constant();
        e.mov(e.w0, static_cast<uint64_t>(c2.u));
        e.fmov(e.s1, e.w0);
        e.fcmp(e.s0, e.s1);
      } else {
        e.fcmp(e.s0, i.src2);
      }
    } else if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fcmp(i.src1, e.s0);
    } else {
      e.fcmp(i.src1, i.src2);
    }
    e.cset(i.dest, Xbyak_aarch64::EQ);
  }
};
struct COMPARE_EQ_F64
    : Sequence<COMPARE_EQ_F64, I<OPCODE_COMPARE_EQ, I8Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      if (i.src2.is_constant) {
        union {
          double d;
          uint64_t u;
        } c2;
        c2.d = i.src2.constant();
        e.mov(e.x0, c2.u);
        e.fmov(e.d1, e.x0);
        e.fcmp(e.d0, e.d1);
      } else {
        e.fcmp(e.d0, i.src2);
      }
    } else if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fcmp(i.src1, e.d0);
    } else {
      e.fcmp(i.src1, i.src2);
    }
    e.cset(i.dest, Xbyak_aarch64::EQ);
  }
};

struct COMPARE_NE_F32
    : Sequence<COMPARE_NE_F32, I<OPCODE_COMPARE_NE, I8Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      if (i.src2.is_constant) {
        union {
          float f;
          uint32_t u;
        } c2;
        c2.f = i.src2.constant();
        e.mov(e.w0, static_cast<uint64_t>(c2.u));
        e.fmov(e.s1, e.w0);
        e.fcmp(e.s0, e.s1);
      } else {
        e.fcmp(e.s0, i.src2);
      }
    } else if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
      e.fcmp(i.src1, e.s0);
    } else {
      e.fcmp(i.src1, i.src2);
    }
    e.cset(i.dest, Xbyak_aarch64::NE);
  }
};
struct COMPARE_NE_F64
    : Sequence<COMPARE_NE_F64, I<OPCODE_COMPARE_NE, I8Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      if (i.src2.is_constant) {
        union {
          double d;
          uint64_t u;
        } c2;
        c2.d = i.src2.constant();
        e.mov(e.x0, c2.u);
        e.fmov(e.d1, e.x0);
        e.fcmp(e.d0, e.d1);
      } else {
        e.fcmp(e.d0, i.src2);
      }
    } else if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
      e.fcmp(i.src1, e.d0);
    } else {
      e.fcmp(i.src1, i.src2);
    }
    e.cset(i.dest, Xbyak_aarch64::NE);
  }
};

// Float compares for SLT/SLE/SGT/SGE (use MI/LS/GT/GE for ordered compares)
#define DEFINE_FLOAT_COMPARE(NAME, COND_S, COND_D)                   \
  struct NAME##_F32                                                  \
      : Sequence<NAME##_F32, I<OPCODE_##NAME, I8Op, F32Op, F32Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {          \
      if (i.src1.is_constant) {                                      \
        union {                                                      \
          float f;                                                   \
          uint32_t u;                                                \
        } c;                                                         \
        c.f = i.src1.constant();                                     \
        e.mov(e.w0, static_cast<uint64_t>(c.u));                     \
        e.fmov(e.s0, e.w0);                                          \
        if (i.src2.is_constant) {                                    \
          union {                                                    \
            float f;                                                 \
            uint32_t u;                                              \
          } c2;                                                      \
          c2.f = i.src2.constant();                                  \
          e.mov(e.w0, static_cast<uint64_t>(c2.u));                  \
          e.fmov(e.s1, e.w0);                                        \
          e.fcmp(e.s0, e.s1);                                        \
        } else {                                                     \
          e.fcmp(e.s0, i.src2);                                      \
        }                                                            \
      } else if (i.src2.is_constant) {                               \
        union {                                                      \
          float f;                                                   \
          uint32_t u;                                                \
        } c;                                                         \
        c.f = i.src2.constant();                                     \
        e.mov(e.w0, static_cast<uint64_t>(c.u));                     \
        e.fmov(e.s0, e.w0);                                          \
        e.fcmp(i.src1, e.s0);                                        \
      } else {                                                       \
        e.fcmp(i.src1, i.src2);                                      \
      }                                                              \
      e.cset(i.dest, Xbyak_aarch64::COND_S);                         \
    }                                                                \
  };                                                                 \
  struct NAME##_F64                                                  \
      : Sequence<NAME##_F64, I<OPCODE_##NAME, I8Op, F64Op, F64Op>> { \
    static void Emit(A64Emitter& e, const EmitArgType& i) {          \
      if (i.src1.is_constant) {                                      \
        union {                                                      \
          double d;                                                  \
          uint64_t u;                                                \
        } c;                                                         \
        c.d = i.src1.constant();                                     \
        e.mov(e.x0, c.u);                                            \
        e.fmov(e.d0, e.x0);                                          \
        if (i.src2.is_constant) {                                    \
          union {                                                    \
            double d;                                                \
            uint64_t u;                                              \
          } c2;                                                      \
          c2.d = i.src2.constant();                                  \
          e.mov(e.x0, c2.u);                                         \
          e.fmov(e.d1, e.x0);                                        \
          e.fcmp(e.d0, e.d1);                                        \
        } else {                                                     \
          e.fcmp(e.d0, i.src2);                                      \
        }                                                            \
      } else if (i.src2.is_constant) {                               \
        union {                                                      \
          double d;                                                  \
          uint64_t u;                                                \
        } c;                                                         \
        c.d = i.src2.constant();                                     \
        e.mov(e.x0, c.u);                                            \
        e.fmov(e.d0, e.x0);                                          \
        e.fcmp(i.src1, e.d0);                                        \
      } else {                                                       \
        e.fcmp(i.src1, i.src2);                                      \
      }                                                              \
      e.cset(i.dest, Xbyak_aarch64::COND_D);                         \
    }                                                                \
  }

DEFINE_FLOAT_COMPARE(COMPARE_SLT, MI, MI);
DEFINE_FLOAT_COMPARE(COMPARE_SLE, LS, LS);
DEFINE_FLOAT_COMPARE(COMPARE_SGT, GT, GT);
DEFINE_FLOAT_COMPARE(COMPARE_SGE, GE, GE);
// For fcmp: LT = N!=V = "less than or unordered" (correct for ULT on floats).
DEFINE_FLOAT_COMPARE(COMPARE_ULT, LT, LT);
// For fcmp: LE = Z=1 or N!=V = "less/equal or unordered" (correct for ULE on
// floats).
DEFINE_FLOAT_COMPARE(COMPARE_ULE, LE, LE);
DEFINE_FLOAT_COMPARE(COMPARE_UGT, HI, HI);
DEFINE_FLOAT_COMPARE(COMPARE_UGE, HS, HS);
#undef DEFINE_FLOAT_COMPARE

// Register all compare opcodes with integer + float variants.
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_EQ, COMPARE_EQ_I8, COMPARE_EQ_I16,
                     COMPARE_EQ_I32, COMPARE_EQ_I64, COMPARE_EQ_F32,
                     COMPARE_EQ_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_NE, COMPARE_NE_I8, COMPARE_NE_I16,
                     COMPARE_NE_I32, COMPARE_NE_I64, COMPARE_NE_F32,
                     COMPARE_NE_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SLT, COMPARE_SLT_I8, COMPARE_SLT_I16,
                     COMPARE_SLT_I32, COMPARE_SLT_I64, COMPARE_SLT_F32,
                     COMPARE_SLT_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SLE, COMPARE_SLE_I8, COMPARE_SLE_I16,
                     COMPARE_SLE_I32, COMPARE_SLE_I64, COMPARE_SLE_F32,
                     COMPARE_SLE_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SGT, COMPARE_SGT_I8, COMPARE_SGT_I16,
                     COMPARE_SGT_I32, COMPARE_SGT_I64, COMPARE_SGT_F32,
                     COMPARE_SGT_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_SGE, COMPARE_SGE_I8, COMPARE_SGE_I16,
                     COMPARE_SGE_I32, COMPARE_SGE_I64, COMPARE_SGE_F32,
                     COMPARE_SGE_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_ULT, COMPARE_ULT_I8, COMPARE_ULT_I16,
                     COMPARE_ULT_I32, COMPARE_ULT_I64, COMPARE_ULT_F32,
                     COMPARE_ULT_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_ULE, COMPARE_ULE_I8, COMPARE_ULE_I16,
                     COMPARE_ULE_I32, COMPARE_ULE_I64, COMPARE_ULE_F32,
                     COMPARE_ULE_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_UGT, COMPARE_UGT_I8, COMPARE_UGT_I16,
                     COMPARE_UGT_I32, COMPARE_UGT_I64, COMPARE_UGT_F32,
                     COMPARE_UGT_F64);
EMITTER_OPCODE_TABLE(OPCODE_COMPARE_UGE, COMPARE_UGE_I8, COMPARE_UGE_I16,
                     COMPARE_UGE_I32, COMPARE_UGE_I64, COMPARE_UGE_F32,
                     COMPARE_UGE_F64);

// ============================================================================
// OPCODE_MUL_ADD (fused multiply-add)
// ============================================================================
struct MUL_ADD_F32
    : Sequence<MUL_ADD_F32, I<OPCODE_MUL_ADD, F32Op, F32Op, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 * src2 + src3
    // ARM64: fmadd dest, src1, src2, src3
    SReg s1 = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    SReg s2 = i.src2.is_constant ? e.s1 : SReg(i.src2.reg().getIdx());
    SReg s3 = i.src3.is_constant ? e.s2 : SReg(i.src3.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    if (i.src2.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src2.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s1, e.w0);
    }
    if (i.src3.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src3.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s2, e.w0);
    }
    EmitFmaWithPpcNan_F32(e, i.dest, s1, s2, s3, /*is_sub=*/false);
  }
};
struct MUL_ADD_F64
    : Sequence<MUL_ADD_F64, I<OPCODE_MUL_ADD, F64Op, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    DReg s1 = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    DReg s2 = i.src2.is_constant ? e.d1 : DReg(i.src2.reg().getIdx());
    DReg s3 = i.src3.is_constant ? e.d2 : DReg(i.src3.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d1, e.x0);
    }
    if (i.src3.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src3.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d2, e.x0);
    }
    EmitFmaWithPpcNan_F64(e, i.dest, s1, s2, s3, /*is_sub=*/false);
  }
};
struct MUL_ADD_V128
    : Sequence<MUL_ADD_V128,
               I<OPCODE_MUL_ADD, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = s1*s2 + s3 with VMX denormal flushing + PPC NaN propagation.
    // Scratch register plan:
    //   1. Flush s3 into v3, save to stack[32].
    //   2. Flush s1/s2 into v0/v1, save to stack[0]/stack[16].
    //   3. Restore s3 into v3, fmla into v2, NaN fixup, flush output.
    const uint32_t guest_pc = i.instr ? i.instr->GuestAddressFor() : 0;
    const bool blue_dragon_target =
        e.current_guest_function() == 0x82282490 &&
        (guest_pc == 0x82282568 || guest_pc == 0x8228256C ||
         guest_pc == 0x82282570);
    const bool audit =
        cvars::arm64_blue_dragon_mul_add_v128_audit && blue_dragon_target;
    const bool fastpath =
        cvars::arm64_blue_dragon_mul_add_v128_fastpath &&
        blue_dragon_target && e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs);

    std::atomic<uint64_t>* pc_counter = nullptr;
    if (audit) {
      pc_counter =
          guest_pc == 0x82282568
              ? e.backend()->blue_dragon_mul_add_v128_audit_pc0_count()
              : guest_pc == 0x8228256C
                    ? e.backend()->blue_dragon_mul_add_v128_audit_pc1_count()
                    : e.backend()->blue_dragon_mul_add_v128_audit_pc2_count();
    }
    auto* total_counter =
        audit ? e.backend()->blue_dragon_mul_add_v128_audit_total_count()
              : nullptr;
    auto* fpcr_switch_counter =
        audit ? e.backend()->blue_dragon_mul_add_v128_audit_fpcr_switch_count()
              : nullptr;
    auto* sw_flush_path_counter =
        audit
            ? e.backend()->blue_dragon_mul_add_v128_audit_sw_flush_path_count()
            : nullptr;
    auto* input_denorm_counter =
        audit
            ? e.backend()->blue_dragon_mul_add_v128_audit_input_denorm_count()
            : nullptr;
    auto* output_denorm_counter =
        audit
            ? e.backend()->blue_dragon_mul_add_v128_audit_output_denorm_count()
            : nullptr;
    auto* nan_entry_counter =
        audit ? e.backend()->blue_dragon_mul_add_v128_audit_nan_entry_count()
              : nullptr;
    auto* nan_lane_counter =
        audit ? e.backend()->blue_dragon_mul_add_v128_audit_nan_lane_count()
              : nullptr;
    auto* src_copy_counter =
        audit ? e.backend()->blue_dragon_mul_add_v128_audit_src_copy_count()
              : nullptr;
    auto* dest_copy_counter =
        audit ? e.backend()->blue_dragon_mul_add_v128_audit_dest_copy_count()
              : nullptr;

    const bool fpcr_switch = e.ChangeFpcrMode(FPCRMode::Vmx);
    e.EmitAtomicIncrement64(total_counter);
    e.EmitAtomicIncrement64(pc_counter);
    if (fpcr_switch) {
      e.EmitAtomicIncrement64(fpcr_switch_counter);
    }
    const bool software_flush =
        !e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs);
    if (software_flush) {
      e.EmitAtomicIncrement64(sw_flush_path_counter);
    }
    if (fastpath) {
      int d = i.dest.reg().getIdx();
      int s1, s2;
      PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
      int s3 = SrcVReg(e, i.src3, 2);
      if (s3 != 2) {
        e.mov(VReg(2).b16, VReg(s3).b16);
      }
      e.fmla(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);
      if (d != 2) {
        e.mov(VReg(d).b16, VReg(2).b16);
      }
      return;
    }

    {
      int d = i.dest.reg().getIdx();

      // Flush s3 → v3, save to stack slot 2.
      int s3 = SrcVReg(e, i.src3, 3);
      if (s3 != 3) {
        e.EmitAtomicIncrement64(src_copy_counter);
        e.mov(VReg(3).b16, VReg(s3).b16);
      }
      AuditV128DenormalIfAny(e, 3, input_denorm_counter, 0, 1);
      if (software_flush) {
        FlushDenormals_V128(e, 3, 0, 1);
      }
      e.str(QReg(3),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 32));

      // Flush s1/s2 → v0/v1, save to stack slots 0/1.
      int s1 = SrcVReg(e, i.src1, 0);
      int s2 = SrcVReg(e, i.src2, 1);
      if (s1 != 0) {
        e.EmitAtomicIncrement64(src_copy_counter);
        e.mov(VReg(0).b16, VReg(s1).b16);
      }
      if (s2 != 1) {
        e.EmitAtomicIncrement64(src_copy_counter);
        e.mov(VReg(1).b16, VReg(s2).b16);
      }
      AuditV128DenormalIfAny(e, 0, input_denorm_counter, 2, 3);
      AuditV128DenormalIfAny(e, 1, input_denorm_counter, 2, 3);
      if (software_flush) {
        FlushDenormals_V128(e, 0);
        FlushDenormals_V128(e, 1);
      }
      e.str(QReg(0), Xbyak_aarch64::ptr(e.sp, static_cast<int32_t>(
                                                  StackLayout::GUEST_SCRATCH)));
      e.str(QReg(1),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 16));

      // Restore flushed s3, compute fmla into v2 via copy.
      e.ldr(QReg(2),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 32));
      e.fmla(VReg(2).s4, VReg(0).s4, VReg(1).s4);

      // PPC NaN fixup (sources on stack at offsets 0/16/32).
      FixupVmxNan_V128_Fma(e, nan_entry_counter, nan_lane_counter);

      // Flush output denormals.
      AuditV128DenormalIfAny(e, 2, output_denorm_counter, 0, 1);
      if (software_flush) {
        FlushDenormals_V128(e, 2, 0, 1);
      }
      e.EmitAtomicIncrement64(dest_copy_counter);
      e.mov(VReg(d).b16, VReg(2).b16);
    }
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_ADD, MUL_ADD_F32, MUL_ADD_F64, MUL_ADD_V128);

// ============================================================================
// OPCODE_MUL_SUB (fused multiply-subtract)
// ============================================================================
struct MUL_SUB_F64
    : Sequence<MUL_SUB_F64, I<OPCODE_MUL_SUB, F64Op, F64Op, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = src1 * src2 - src3
    // ARM64 fnmsub(d,n,m,a) = -a + n*m = n*m - a
    DReg s1 = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    DReg s2 = i.src2.is_constant ? e.d1 : DReg(i.src2.reg().getIdx());
    DReg s3 = i.src3.is_constant ? e.d2 : DReg(i.src3.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    if (i.src2.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src2.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d1, e.x0);
    }
    if (i.src3.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src3.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d2, e.x0);
    }
    EmitFmaWithPpcNan_F64(e, i.dest, s1, s2, s3, /*is_sub=*/true);
  }
};
struct MUL_SUB_V128
    : Sequence<MUL_SUB_V128,
               I<OPCODE_MUL_SUB, V128Op, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // dest = s1*s2 - s3 with VMX denormal flushing + PPC NaN propagation.
    // Same as MUL_ADD but negate s3 before the fmla.
    EmitWithVmxFpcr(e, [&] {
      int d = i.dest.reg().getIdx();

      // Flush s3 → v3, save un-negated for NaN fixup.
      int s3 = SrcVReg(e, i.src3, 3);
      if (s3 != 3) e.mov(VReg(3).b16, VReg(s3).b16);
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 3, 0, 1);
      }
      e.str(QReg(3),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 32));

      // Flush s1/s2 → v0/v1, save for NaN fixup.
      int s1, s2;
      PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
      e.str(QReg(0), Xbyak_aarch64::ptr(e.sp, static_cast<int32_t>(
                                                  StackLayout::GUEST_SCRATCH)));
      e.str(QReg(1),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 16));

      // Reload flushed s3, negate into v2, fmla: v2 = -s3 + s1*s2 = s1*s2 - s3.
      e.ldr(QReg(2),
            Xbyak_aarch64::ptr(
                e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 32));
      e.fneg(VReg(2).s4, VReg(2).s4);
      e.fmla(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);

      // PPC NaN fixup (sources on stack at offsets 0/16/32).
      FixupVmxNan_V128_Fma(e);

      // Flush output denormals.
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 2, 0, 1);
      }
      e.mov(VReg(d).b16, VReg(2).b16);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_MUL_SUB, MUL_SUB_F64, MUL_SUB_V128);

// ============================================================================
// POW2 / LOG2 / DOT_PRODUCT C helper functions (called via CallNativeSafe)
// ============================================================================

// POW2 (vexptefp): 2^x for each of 4 float lanes.
// Args: x0=PPCContext* (unused), x1=pointer to vec128_t (in-place).
static void EmulatePow2(void* /*ctx*/, void* vdata) {
  auto* data = reinterpret_cast<vec128_t*>(vdata);
  for (int i = 0; i < 4; i++) {
    data->f32[i] = std::exp2(data->f32[i]);
  }
}

// LOG2 (vlogefp): log2(x) for each of 4 float lanes.
// Args: x0=PPCContext* (unused), x1=pointer to vec128_t (in-place).
static void EmulateLog2(void* /*ctx*/, void* vdata) {
  auto* data = reinterpret_cast<vec128_t*>(vdata);
  for (int i = 0; i < 4; i++) {
    data->f32[i] = std::log2(data->f32[i]);
  }
}

// DOT_PRODUCT_3 (vmsum3fp): dot product of first 3 elements.
// Uses double-precision intermediates; overflow -> QNaN.
// Args: x0=PPCContext* (unused), x1=pointer to 2 consecutive vec128_t
//       (src1 at offset 0, src2 at offset 16). Result stored in src1.
static void EmulateDotProduct3(void* /*ctx*/, void* vdata) {
  auto* data = reinterpret_cast<vec128_t*>(vdata);
  vec128_t& src1 = data[0];
  vec128_t& src2 = data[1];
  double d0 = (double)src1.f32[0] * (double)src2.f32[0];
  double d1 = (double)src1.f32[1] * (double)src2.f32[1];
  double d2 = (double)src1.f32[2] * (double)src2.f32[2];
  double sum = d0 + d1 + d2;
  float result = (float)sum;
  if (std::isinf(result)) {
    uint32_t qnan = 0x7FC00000u;
    memcpy(&result, &qnan, sizeof(result));
  }
  src1.f32[0] = src1.f32[1] = src1.f32[2] = src1.f32[3] = result;
}

// DOT_PRODUCT_4 (vmsum4fp): dot product of all 4 elements.
// Uses double-precision intermediates; overflow -> QNaN.
// Args: x0=PPCContext* (unused), x1=pointer to 2 consecutive vec128_t.
static void EmulateDotProduct4(void* /*ctx*/, void* vdata) {
  auto* data = reinterpret_cast<vec128_t*>(vdata);
  vec128_t& src1 = data[0];
  vec128_t& src2 = data[1];
  double d0 = (double)src1.f32[0] * (double)src2.f32[0];
  double d1 = (double)src1.f32[1] * (double)src2.f32[1];
  double d2 = (double)src1.f32[2] * (double)src2.f32[2];
  double d3 = (double)src1.f32[3] * (double)src2.f32[3];
  double sum = d0 + d1 + d2 + d3;
  float result = (float)sum;
  if (std::isinf(result)) {
    uint32_t qnan = 0x7FC00000u;
    memcpy(&result, &qnan, sizeof(result));
  }
  src1.f32[0] = src1.f32[1] = src1.f32[2] = src1.f32[3] = result;
}

// ============================================================================
// OPCODE_POW2
// ============================================================================
struct POW2_F32 : Sequence<POW2_F32, I<OPCODE_POW2, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_always("POW2_F32 should not be emitted");
  }
};
struct POW2_F64 : Sequence<POW2_F64, I<OPCODE_POW2, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_always("POW2_F64 should not be emitted");
  }
};
struct POW2_V128 : Sequence<POW2_V128, I<OPCODE_POW2, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // No hardware FP emitted — the C++ helper does all math.
    // GuestToHostThunk restores FPCR after the native call.
    int s = SrcVReg(e, i.src1, 0);
    int d = i.dest.reg().getIdx();
    e.str(QReg(s),
          ptr(e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH)));
    e.add(e.x1, e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulatePow2));
    e.ldr(QReg(d),
          ptr(e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH)));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_POW2, POW2_F32, POW2_F64, POW2_V128);

// ============================================================================
// OPCODE_LOG2
// ============================================================================
struct LOG2_F32 : Sequence<LOG2_F32, I<OPCODE_LOG2, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_always("LOG2_F32 should not be emitted");
  }
};
struct LOG2_F64 : Sequence<LOG2_F64, I<OPCODE_LOG2, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    assert_always("LOG2_F64 should not be emitted");
  }
};
struct LOG2_V128 : Sequence<LOG2_V128, I<OPCODE_LOG2, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // No hardware FP emitted — the C++ helper does all math.
    // GuestToHostThunk restores FPCR after the native call.
    int s = SrcVReg(e, i.src1, 0);
    int d = i.dest.reg().getIdx();
    e.str(QReg(s),
          ptr(e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH)));
    e.add(e.x1, e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH));
    e.CallNativeSafe(reinterpret_cast<void*>(EmulateLog2));
    e.ldr(QReg(d),
          ptr(e.sp, static_cast<uint32_t>(StackLayout::GUEST_SCRATCH)));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_LOG2, LOG2_F32, LOG2_F64, LOG2_V128);

// ============================================================================
// OPCODE_DOT_PRODUCT_3
// ============================================================================
template <bool IncludeFourthLane>
static void EmitDotProductF32Fastpath(A64Emitter& e, SReg dest,
                                      const VReg& src1, const VReg& src2) {
  e.fmul(VReg(2).s4, src1.s4, src2.s4);
  e.dup(VReg(3).s4, VReg(2).s4[1]);
  e.fadd(SReg(2), SReg(2), SReg(3));
  e.dup(VReg(3).s4, VReg(2).s4[2]);
  e.fadd(SReg(2), SReg(2), SReg(3));
  if constexpr (IncludeFourthLane) {
    e.dup(VReg(3).s4, VReg(2).s4[3]);
    e.fadd(SReg(2), SReg(2), SReg(3));
  }

  e.fabs(SReg(3), SReg(2));
  e.mov(e.w17, 0x7F800000u);
  e.fmov(SReg(0), e.w17);
  e.fcmp(SReg(3), SReg(0));
  auto& not_inf = e.NewCachedLabel();
  e.b(Xbyak_aarch64::NE, not_inf);
  e.mov(e.w17, 0x7FC00000u);
  e.fmov(SReg(2), e.w17);
  e.L(not_inf);

  e.fmov(dest, SReg(2));
}

struct DOT_PRODUCT_3_F32
    : Sequence<DOT_PRODUCT_3_F32,
               I<OPCODE_DOT_PRODUCT_3, F32Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      if (cvars::arm64_vmx_dot_f32_fastpath) {
        int s1, s2;
        PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
        EmitDotProductF32Fastpath<false>(e, i.dest, VReg(s1), VReg(s2));
        return;
      }

      int s1 = SrcVReg(e, i.src1, 0);
      int s2 = SrcVReg(e, i.src2, 1);

      e.fcvtl(VReg(0).d2, VReg(s1).s2);
      e.fcvtl(VReg(1).d2, VReg(s2).s2);
      e.fmul(VReg(0).d2, VReg(0).d2, VReg(1).d2);
      e.fcvtl2(VReg(2).d2, VReg(s1).s4);
      e.fcvtl2(VReg(3).d2, VReg(s2).s4);
      e.fmul(VReg(2).d2, VReg(2).d2, VReg(3).d2);

      e.faddp(DReg(1), VReg(0).d2);
      e.fadd(DReg(1), DReg(1), DReg(2));
      e.fcvt(i.dest, DReg(1));

      e.fabs(SReg(1), i.dest);
      e.mov(e.w17, 0x7F800000u);
      e.fmov(SReg(2), e.w17);
      e.fcmp(SReg(1), SReg(2));
      auto& not_inf = e.NewCachedLabel();
      e.b(Xbyak_aarch64::NE, not_inf);
      e.mov(e.w17, 0x7FC00000u);
      e.fmov(i.dest, e.w17);
      e.L(not_inf);
    });
  }
};

struct DOT_PRODUCT_3_V128
    : Sequence<DOT_PRODUCT_3_V128,
               I<OPCODE_DOT_PRODUCT_3, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      if (cvars::arm64_vmx_dot_f32_fastpath) {
        int s1, s2;
        PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
        int d = i.dest.reg().getIdx();
        EmitDotProductF32Fastpath<false>(e, SReg(2), VReg(s1), VReg(s2));
        e.dup(VReg(d).s4, VReg(2).s4[0]);
        return;
      }

      // Inline NEON: multiply in double precision, sum 3 elements, convert
      // back. Uses v0-v3 as scratch.
      int s1 = SrcVReg(e, i.src1, 0);
      int s2 = SrcVReg(e, i.src2, 1);
      int d = i.dest.reg().getIdx();
      // Widen low 2 floats of each source to double.
      e.fcvtl(VReg(0).d2, VReg(s1).s2);            // v0 = {s1[0], s1[1]} as f64
      e.fcvtl(VReg(1).d2, VReg(s2).s2);            // v1 = {s2[0], s2[1]} as f64
      e.fmul(VReg(0).d2, VReg(0).d2, VReg(1).d2);  // v0 = {a0*b0, a1*b1}
      // Widen high 2 floats (elements 2,3) to double.
      e.fcvtl2(VReg(2).d2, VReg(s1).s4);           // v2 = {s1[2], s1[3]} as f64
      e.fcvtl2(VReg(3).d2, VReg(s2).s4);           // v3 = {s2[2], s2[3]} as f64
      e.fmul(VReg(2).d2, VReg(2).d2, VReg(3).d2);  // v2 = {a2*b2, a3*b3}
      // Sum: d0 = v0[0] + v0[1] + v2[0] (skip v2[1] = element 3).
      e.faddp(DReg(1), VReg(0).d2);
      e.fadd(DReg(1), DReg(1), DReg(2));
      // Convert back to float.
      e.fcvt(SReg(0), DReg(1));
      // Check for infinity → QNaN.
      e.fabs(SReg(1), SReg(0));
      e.mov(e.w17, 0x7F800000u);  // +inf
      e.fmov(SReg(2), e.w17);
      e.fcmp(SReg(1), SReg(2));
      auto& not_inf = e.NewCachedLabel();
      e.b(Xbyak_aarch64::NE, not_inf);
      e.mov(e.w17, 0x7FC00000u);  // QNaN
      e.fmov(SReg(0), e.w17);
      e.L(not_inf);
      // Splat result to all 4 lanes.
      e.dup(VReg(d).s4, VReg(0).s4[0]);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_3, DOT_PRODUCT_3_F32,
                     DOT_PRODUCT_3_V128);

// ============================================================================
// OPCODE_DOT_PRODUCT_4
// ============================================================================
struct DOT_PRODUCT_4_F32
    : Sequence<DOT_PRODUCT_4_F32,
               I<OPCODE_DOT_PRODUCT_4, F32Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      if (cvars::arm64_vmx_dot_f32_fastpath) {
        int s1, s2;
        PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
        EmitDotProductF32Fastpath<true>(e, i.dest, VReg(s1), VReg(s2));
        return;
      }

      int s1 = SrcVReg(e, i.src1, 0);
      int s2 = SrcVReg(e, i.src2, 1);

      e.fcvtl(VReg(0).d2, VReg(s1).s2);
      e.fcvtl(VReg(1).d2, VReg(s2).s2);
      e.fmul(VReg(0).d2, VReg(0).d2, VReg(1).d2);
      e.fcvtl2(VReg(2).d2, VReg(s1).s4);
      e.fcvtl2(VReg(3).d2, VReg(s2).s4);
      e.fmul(VReg(2).d2, VReg(2).d2, VReg(3).d2);

      e.fadd(VReg(0).d2, VReg(0).d2, VReg(2).d2);
      e.faddp(DReg(1), VReg(0).d2);
      e.fcvt(i.dest, DReg(1));

      e.fabs(SReg(1), i.dest);
      e.mov(e.w17, 0x7F800000u);
      e.fmov(SReg(2), e.w17);
      e.fcmp(SReg(1), SReg(2));
      auto& not_inf = e.NewCachedLabel();
      e.b(Xbyak_aarch64::NE, not_inf);
      e.mov(e.w17, 0x7FC00000u);
      e.fmov(i.dest, e.w17);
      e.L(not_inf);
    });
  }
};

struct DOT_PRODUCT_4_V128
    : Sequence<DOT_PRODUCT_4_V128,
               I<OPCODE_DOT_PRODUCT_4, V128Op, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      if (cvars::arm64_vmx_dot_f32_fastpath) {
        int s1, s2;
        PrepareVmxFpSources(e, i.src1, i.src2, s1, s2);
        int d = i.dest.reg().getIdx();
        EmitDotProductF32Fastpath<true>(e, SReg(2), VReg(s1), VReg(s2));
        e.dup(VReg(d).s4, VReg(2).s4[0]);
        return;
      }

      // Inline NEON: multiply in double precision, sum all 4 elements.
      int s1 = SrcVReg(e, i.src1, 0);
      int s2 = SrcVReg(e, i.src2, 1);
      int d = i.dest.reg().getIdx();
      // Widen low 2 floats to double, multiply.
      e.fcvtl(VReg(0).d2, VReg(s1).s2);
      e.fcvtl(VReg(1).d2, VReg(s2).s2);
      e.fmul(VReg(0).d2, VReg(0).d2, VReg(1).d2);
      // Widen high 2 floats to double, multiply.
      e.fcvtl2(VReg(2).d2, VReg(s1).s4);
      e.fcvtl2(VReg(3).d2, VReg(s2).s4);
      e.fmul(VReg(2).d2, VReg(2).d2, VReg(3).d2);
      // Sum all 4 products: v0 = {a0*b0+a2*b2, a1*b1+a3*b3}
      e.fadd(VReg(0).d2, VReg(0).d2, VReg(2).d2);
      e.faddp(DReg(1), VReg(0).d2);
      // Convert back to float.
      e.fcvt(SReg(0), DReg(1));
      // Check for infinity → QNaN.
      e.fabs(SReg(1), SReg(0));
      e.mov(e.w17, 0x7F800000u);
      e.fmov(SReg(2), e.w17);
      e.fcmp(SReg(1), SReg(2));
      auto& not_inf = e.NewCachedLabel();
      e.b(Xbyak_aarch64::NE, not_inf);
      e.mov(e.w17, 0x7FC00000u);
      e.fmov(SReg(0), e.w17);
      e.L(not_inf);
      // Splat result to all 4 lanes.
      e.dup(VReg(d).s4, VReg(0).s4[0]);
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_DOT_PRODUCT_4, DOT_PRODUCT_4_F32,
                     DOT_PRODUCT_4_V128);

// ============================================================================
// OPCODE_SET_ROUNDING_MODE
// ============================================================================
// PPC rounding mode (input bits 0-2) to ARM64 FPCR value table.
// Bits 0-1: PPC RN (rounding mode), Bit 2: PPC NI (non-IEEE / flush-to-zero).
//   PPC RN=0 (nearest) -> ARM64 RMode=00, PPC RN=1 (toward zero) -> RMode=11,
//   PPC RN=2 (toward +inf) -> RMode=01, PPC RN=3 (toward -inf) -> RMode=10.
// ARM64 FPCR RMode is bits 23:22, FZ is bit 24.
// Index 0-3: NI=0 (IEEE), Index 4-7: NI=1 (non-IEEE, FZ set).
static constexpr uint32_t fpcr_table[8] = {
    (0b00 << 22),              // PPC 0: nearest, IEEE
    (0b11 << 22),              // PPC 1: toward zero, IEEE
    (0b01 << 22),              // PPC 2: toward +inf, IEEE
    (0b10 << 22),              // PPC 3: toward -inf, IEEE
    (0b00 << 22) | (1 << 24),  // PPC 4: nearest, flush-to-zero
    (0b11 << 22) | (1 << 24),  // PPC 5: toward zero, flush-to-zero
    (0b01 << 22) | (1 << 24),  // PPC 6: toward +inf, flush-to-zero
    (0b10 << 22) | (1 << 24),  // PPC 7: toward -inf, flush-to-zero
};
struct SET_ROUNDING_MODE
    : Sequence<SET_ROUNDING_MODE, I<OPCODE_SET_ROUNDING_MODE, VoidOp, I32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Input is PPC FPSCR bits (already masked to 0-7 by the frontend).
    // We set FPCR RMode + FZ bits and cache the value in the backend context.
    auto bctx = e.GetBackendCtxReg();

    if (i.src1.is_constant) {
      uint32_t fpcr_val = fpcr_table[i.src1.constant() & 7];
      e.mov(e.x0, static_cast<uint64_t>(fpcr_val));
      e.msr(3, 3, 4, 4, 0, e.x0);  // msr FPCR, x0
      // Cache in backend context.
      e.str(e.w0, ptr(bctx, static_cast<uint32_t>(
                                offsetof(A64BackendContext, fpcr_fpu))));
      // Update NonIEEE flag.
      e.ldr(
          e.w0,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
      if (i.src1.constant() & 4) {
        e.orr(e.w0, e.w0, 1u << kA64BackendNonIEEEMode);
      } else {
        // Clear bit kA64BackendNonIEEEMode using BIC (avoids bitmask encoding).
        e.mov(e.w1, 1u << kA64BackendNonIEEEMode);
        e.bic(e.w0, e.w0, e.w1);
      }
      e.str(
          e.w0,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
    } else {
      // Dynamic: look up FPCR value from table.
      e.mov(e.x0, reinterpret_cast<uint64_t>(fpcr_table));
      e.and_(e.w1, i.src1, 7);
      e.ldr(e.w0, Xbyak_aarch64::ptr(e.x0, e.x1, Xbyak_aarch64::LSL, 2));
      // Write FPCR.
      e.msr(3, 3, 4, 4, 0, e.x0);
      // Cache in backend context.
      e.str(e.w0, ptr(bctx, static_cast<uint32_t>(
                                offsetof(A64BackendContext, fpcr_fpu))));
      // Update NonIEEE flag based on bit 2 of input.
      e.ldr(
          e.w0,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
      // Clear bit kA64BackendNonIEEEMode using BIC (avoids bitmask encoding).
      e.mov(e.w1, 1u << kA64BackendNonIEEEMode);
      e.bic(e.w0, e.w0, e.w1);
      // Conditionally set it back if input bit 2 is set.
      e.tst(i.src1, 4);
      e.csel(e.w1, e.w1, e.wzr, Xbyak_aarch64::Cond::NE);
      e.orr(e.w0, e.w0, e.w1);
      e.str(
          e.w0,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
    }
    e.ChangeFpcrMode(FPCRMode::Fpu, /*already_set=*/true);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SET_ROUNDING_MODE, SET_ROUNDING_MODE);

// PPC frsqrte lookup table implementation (PowerISA Table E-5).
// Matches the x64 backend's EmitFrsqrteHelper.
static uint64_t PpcFrsqrte(uint64_t bits) {
  uint32_t sign = (uint32_t)(bits >> 63);
  uint32_t exp = (uint32_t)((bits >> 52) & 0x7FF);
  uint64_t mantissa = bits & 0x000FFFFFFFFFFFFFULL;

  // NaN → QNaN (quiet it, preserve sign and payload)
  if (exp == 0x7FF && mantissa != 0) {
    return bits | (1ULL << 51);
  }
  // ±0 → ±inf
  if (exp == 0 && mantissa == 0) {
    return sign ? 0xFFF0000000000000ULL : 0x7FF0000000000000ULL;
  }
  // +inf → +0
  if (exp == 0x7FF && !sign) {
    return 0;
  }
  // -inf or negative → QNaN
  if (sign) {
    return 0x7FF8000000000000ULL;
  }

  // Denormal: normalize (matching x64 EmitFrsqrteHelper L25).
  int32_t effective_exp = (int32_t)exp;
  uint64_t norm_mantissa = mantissa;
  if (exp == 0) {
    int lz = (int)xe::lzcnt(mantissa);  // leading zeros in 64-bit
    norm_mantissa = mantissa << (lz - 11);
    effective_exp = 12 - lz;
  }

  // PPC frsqrte lookup table (16 entries, 8 bits each).
  static constexpr uint8_t table[] = {241, 216, 192, 168, 152, 136, 128, 112,
                                      96,  76,  60,  48,  32,  24,  16,  8};

  // Index: bit 3 = !(exp & 1), bits 2:0 = top 3 mantissa bits.
  // For denormals, norm_mantissa has implicit 1 at bit 52; & 7 masks it out.
  uint32_t top3 = (uint32_t)(norm_mantissa >> 49) & 7;
  uint32_t index = (((uint32_t)effective_exp & 1) << 3) | top3;
  index ^= 8;

  // Result exponent = 1022 - floor((effective_exp - 1023) / 2).
  int32_t unbiased = effective_exp - 1023;
  int32_t half = unbiased >> 1;  // arithmetic shift = floor division
  uint32_t result_exp = (uint32_t)(1022 - half);

  // Construct result: exponent in bits 62:52, table value in bits 51:44.
  uint64_t result =
      ((uint64_t)result_exp << 52) | ((uint64_t)table[index] << 44);
  return result;
}

// ============================================================================
// OPCODE_RSQRT
// ============================================================================
struct RSQRT_F32 : Sequence<RSQRT_F32, I<OPCODE_RSQRT, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    SReg src = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    e.fsqrt(e.s1, src);
    e.mov(e.w0, static_cast<uint64_t>(0x3F800000u));
    e.fmov(e.s2, e.w0);
    e.fdiv(i.dest, e.s2, e.s1);
  }
};
struct RSQRT_F64 : Sequence<RSQRT_F64, I<OPCODE_RSQRT, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // PPC frsqrte uses a specific lookup table, not a high-precision estimate.
    // Call PpcFrsqrte directly (pure integer math, no FPCR impact).
    DReg src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    e.fmov(e.x0, src);
    e.mov(e.x9, reinterpret_cast<uint64_t>(PpcFrsqrte));
    e.blr(e.x9);
    e.fmov(i.dest, e.x0);
  }
};
// PPC vrsqrtefp per-lane implementation.
// Uses the same 32-entry lookup table + interpolation as x64's
// EmitScalarVRsqrteHelper.
static uint32_t PpcVrsqrtefpLane(uint32_t bits) {
  static constexpr uint32_t table[32] = {
      0x0568B4FD, 0x04F3AF97, 0x048DAAA5, 0x0435A618, 0x03E7A1E4, 0x03A29DFE,
      0x03659A5C, 0x032E96F8, 0x02FC93CA, 0x02D090CE, 0x02A88DFE, 0x02838B57,
      0x026188D4, 0x02438673, 0x02268431, 0x020B820B, 0x03D27FFA, 0x03807C29,
      0x033878AA, 0x02F97572, 0x02C27279, 0x02926FB7, 0x02666D26, 0x023F6AC0,
      0x021D6881, 0x01FD6665, 0x01E16468, 0x01C76287, 0x01AF60C1, 0x01995F12,
      0x01855D79, 0x01735BF4,
  };

  uint32_t sign = bits >> 31;
  uint32_t biased_exp = (bits >> 23) & 0xFF;
  uint32_t mantissa = bits & 0x007FFFFF;

  // -Inf → QNaN
  if (bits == 0xFF800000u) return 0x7FC00000u;

  // Denormal or zero (exp == 0)
  if (biased_exp == 0) {
    // ±0 or denormal with NJM on → flush to ±0 → ±Inf
    return sign ? 0xFF800000u : 0x7F800000u;
  }

  // NaN/Inf (exp == 255)
  if (biased_exp == 255) {
    if (mantissa == 0) {
      // +Inf → +0 (-Inf already handled above)
      return 0;
    }
    // NaN: quiet it (set bit 22), preserve sign and payload
    return bits | 0x00400000u;
  }

  // Negative normal → QNaN
  if (sign) return 0x7FC00000u;

  // Normal positive: table lookup + interpolation
  int32_t unbiased_exp = (int32_t)biased_exp - 127;

  // Table index: exp parity selects half, top 4 mantissa bits select entry
  uint32_t exp_parity = ((uint32_t)(unbiased_exp << 4)) & 16;
  uint32_t top4 = mantissa >> 19;
  uint32_t index = (exp_parity | top4) ^ 16;

  // 10-bit interpolation factor from mantissa
  uint32_t interp = (mantissa >> 9) & 1023;

  // Result exponent (arithmetic shift)
  int32_t result_exp = (127 - (int32_t)biased_exp) >> 1;

  // Lookup + linear interpolation
  uint32_t entry = table[index];
  uint32_t slope = entry >> 16;
  uint32_t base = (entry << 10) & 0x3FFFC00u;
  int32_t raw = (int32_t)base - (int32_t)(interp * slope);

  // Normalize if bit 25 not set
  if (!(raw & (1 << 25))) {
    uint32_t val = (uint32_t)raw & 0x1FFFFFF;
    uint32_t lz = (uint32_t)xe::lzcnt(val);
    int32_t shift = (int32_t)lz - 6;
    result_exp += 6;
    result_exp -= (int32_t)lz;
    raw <<= shift;
  }

  // Rounding
  if ((raw & 5) && (raw & 2)) raw += 4;

  // Assemble result
  uint32_t res_exp = (uint32_t)((result_exp << 23) + 0x3F800000);
  uint32_t res_man = ((uint32_t)raw >> 2) & 0x7FFFFF;
  uint32_t result = res_exp | res_man;

  // DAZ: flush denormal output to +0
  if (((result >> 23) & 0xFF) == 0 && (result & 0x7FFFFF)) {
    result = 0;
  }

  return result;
}

struct RSQRT_V128 : Sequence<RSQRT_V128, I<OPCODE_RSQRT, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // Call PpcVrsqrtefpLane directly per lane (pure integer math).
    // Save source to stack scratch, accumulate results there, load at end.
    int src_idx = SrcVReg(e, i.src1, 0);
    e.str(QReg(src_idx),
          Xbyak_aarch64::ptr(e.sp,
                             static_cast<int32_t>(StackLayout::GUEST_SCRATCH)));
    for (int lane = 0; lane < 4; lane++) {
      e.ldr(e.w0, Xbyak_aarch64::ptr(
                      e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) +
                                lane * 4));
      e.mov(e.x9, reinterpret_cast<uint64_t>(PpcVrsqrtefpLane));
      e.blr(e.x9);
      e.str(e.w0, Xbyak_aarch64::ptr(
                      e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) +
                                lane * 4));
    }
    e.ldr(QReg(i.dest.reg().getIdx()),
          Xbyak_aarch64::ptr(e.sp,
                             static_cast<int32_t>(StackLayout::GUEST_SCRATCH)));
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RSQRT, RSQRT_F32, RSQRT_F64, RSQRT_V128);

// ============================================================================
// OPCODE_RECIP
// ============================================================================
struct RECIP_F32 : Sequence<RECIP_F32, I<OPCODE_RECIP, F32Op, F32Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    SReg src = i.src1.is_constant ? e.s0 : SReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        float f;
        uint32_t u;
      } c;
      c.f = i.src1.constant();
      e.mov(e.w0, static_cast<uint64_t>(c.u));
      e.fmov(e.s0, e.w0);
    }
    e.fmov(e.s2, 1.0f);
    e.fdiv(i.dest, e.s2, src);
  }
};
struct RECIP_F64 : Sequence<RECIP_F64, I<OPCODE_RECIP, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    DReg src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    e.fmov(e.d2, 1.0);
    e.fdiv(i.dest, e.d2, src);
  }
};
struct RECIP_V128 : Sequence<RECIP_V128, I<OPCODE_RECIP, V128Op, V128Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    EmitWithVmxFpcr(e, [&] {
      if (i.src1.is_constant) {
        LoadV128Const(e, 1, i.src1.constant());
      } else {
        e.mov(VReg(1).b16, VReg(i.src1.reg().getIdx()).b16);
      }
      // Flush input denormals.
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, 1);  // scratch v2, v3
      }
      auto d = VReg(i.dest.reg().getIdx()).s4;
      // Load 1.0f vector.
      e.fmov(VReg(0).s4, 1.0f);
      e.fdiv(d, VReg(0).s4, VReg(1).s4);
      // Flush output denormals.
      if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
        FlushDenormals_V128(e, i.dest.reg().getIdx(), 0, 1);
      }
    });
  }
};
EMITTER_OPCODE_TABLE(OPCODE_RECIP, RECIP_F32, RECIP_F64, RECIP_V128);

// ============================================================================
// OPCODE_TO_SINGLE
// ============================================================================
struct TOSINGLE : Sequence<TOSINGLE, I<OPCODE_TO_SINGLE, F64Op, F64Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    e.ChangeFpcrMode(FPCRMode::Fpu);
    DReg src = i.src1.is_constant ? e.d0 : DReg(i.src1.reg().getIdx());
    if (i.src1.is_constant) {
      union {
        double d;
        uint64_t u;
      } c;
      c.d = i.src1.constant();
      e.mov(e.x0, c.u);
      e.fmov(e.d0, e.x0);
    }
    // Round double->single->double.
    // NaN sign is already correct from upstream arithmetic (EmitFmaWithPpcNan
    // etc.) or fneg.  fcvt with DN=0 preserves NaN sign, so no fixup needed.
    e.fcvt(e.s0, src);
    e.fcvt(i.dest, e.s0);
  }
};
EMITTER_OPCODE_TABLE(OPCODE_TO_SINGLE, TOSINGLE);

// ============================================================================
// OPCODE_SET_NJM
// ============================================================================
struct SET_NJM : Sequence<SET_NJM, I<OPCODE_SET_NJM, VoidOp, I8Op>> {
  static void Emit(A64Emitter& e, const EmitArgType& i) {
    // NJM (Non-Java Mode) is a VMX/AltiVec feature (VSCR bit 16) that
    // controls flush-to-zero for vector operations.  It does NOT affect
    // scalar FPU behaviour.  On ARM64 this maps to FPCR.FZ (bit 24) in
    // the cached fpcr_vmx value, which EmitWithVmxFpcr loads before
    // each vector FP operation.
    auto bctx = e.GetBackendCtxReg();

    // Toggle FZ bit in cached fpcr_vmx.
    e.ldr(e.w0, ptr(bctx, static_cast<uint32_t>(
                              offsetof(A64BackendContext, fpcr_vmx))));
    if (i.src1.is_constant) {
      if (i.src1.constant()) {
        e.orr(e.w0, e.w0, (1u << 24));  // NJM=1: set FZ
      } else {
        e.and_(e.w0, e.w0, ~(1u << 24));  // NJM=0: clear FZ
      }
    } else {
      auto& set_fz = e.NewCachedLabel();
      auto& done = e.NewCachedLabel();
      e.cbnz(i.src1, set_fz);
      e.and_(e.w0, e.w0, ~(1u << 24));  // NJM=0: clear FZ
      e.b(done);
      e.L(set_fz);
      e.orr(e.w0, e.w0, (1u << 24));  // NJM=1: set FZ
      e.L(done);
    }
    e.str(e.w0, ptr(bctx, static_cast<uint32_t>(
                              offsetof(A64BackendContext, fpcr_vmx))));

    // Update kA64BackendNJMOn flag.
    e.ldr(e.w0,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));
    if (i.src1.is_constant) {
      if (i.src1.constant()) {
        e.orr(e.w0, e.w0, 1u << kA64BackendNJMOn);
      } else {
        e.mov(e.w1, 1u << kA64BackendNJMOn);
        e.bic(e.w0, e.w0, e.w1);
      }
    } else {
      e.mov(e.w1, 1u << kA64BackendNJMOn);
      e.bic(e.w0, e.w0, e.w1);
      e.tst(i.src1, 0xFF);
      e.csel(e.w1, e.w1, e.wzr, Xbyak_aarch64::Cond::NE);
      e.orr(e.w0, e.w0, e.w1);
    }
    e.str(e.w0,
          ptr(bctx, static_cast<uint32_t>(offsetof(A64BackendContext, flags))));

    e.ForgetFpcrMode();
  }
};
EMITTER_OPCODE_TABLE(OPCODE_SET_NJM, SET_NJM);

// Force-link the split sequence files so their static initializers run.
extern volatile int anchor_control;
static int anchor_control_dest = anchor_control;

extern volatile int anchor_memory;
static int anchor_memory_dest = anchor_memory;

extern volatile int anchor_vector;
static int anchor_vector_dest = anchor_vector;

namespace {

static bool IsIntegerZeroValue(const hir::Value* value) {
  if (!value || !(value->flags & hir::VALUE_IS_CONSTANT)) {
    return false;
  }
  switch (value->type) {
    case hir::INT8_TYPE:
    case hir::INT16_TYPE:
    case hir::INT32_TYPE:
    case hir::INT64_TYPE:
      return value->constant.i64 == 0;
    default:
      return false;
  }
}

static bool IsUnsignedZeroCompareAlwaysFalse(const hir::Instr* instr) {
  switch (instr->GetOpcodeNum()) {
    case hir::OPCODE_COMPARE_ULT:
      return IsIntegerZeroValue(instr->src2.value);
    case hir::OPCODE_COMPARE_UGT:
      return IsIntegerZeroValue(instr->src1.value);
    default:
      return false;
  }
}

static bool ValueHasOnlyUse(const hir::Value* value, const hir::Instr* instr) {
  return value && value->use_head && value->use_head->instr == instr &&
         value->use_head->next == nullptr;
}

static bool IsIntegerType(hir::TypeName type) {
  switch (type) {
    case hir::INT8_TYPE:
    case hir::INT16_TYPE:
    case hir::INT32_TYPE:
    case hir::INT64_TYPE:
      return true;
    default:
      return false;
  }
}

static uint64_t IntegerValueBits(const hir::Value* value) {
  switch (value->type) {
    case hir::INT8_TYPE:
      return static_cast<uint8_t>(value->constant.i8);
    case hir::INT16_TYPE:
      return static_cast<uint16_t>(value->constant.i16);
    case hir::INT32_TYPE:
      return static_cast<uint32_t>(value->constant.i32);
    case hir::INT64_TYPE:
      return static_cast<uint64_t>(value->constant.i64);
    default:
      return 0;
  }
}

static bool ValuesMatchForCompare(const hir::Value* a, const hir::Value* b) {
  if (a == b) {
    return true;
  }
  if (!a || !b || a->type != b->type || !IsIntegerType(a->type)) {
    return false;
  }
  if (a->IsConstant() && b->IsConstant()) {
    return IntegerValueBits(a) == IntegerValueBits(b);
  }
  return false;
}

static bool CompareOperandsMatch(const hir::Instr* a, const hir::Instr* b) {
  return a && b && ValuesMatchForCompare(a->src1.value, b->src1.value) &&
         ValuesMatchForCompare(a->src2.value, b->src2.value);
}

static bool ValueUsesOnly(const hir::Value* value, const hir::Instr* allowed_a,
                          const hir::Instr* allowed_b = nullptr) {
  if (!value || !value->use_head) {
    return false;
  }
  for (auto use = value->use_head; use; use = use->next) {
    if (use->instr == allowed_a || use->instr == allowed_b) {
      continue;
    }
    return false;
  }
  return true;
}

static bool IsStoreContextOfValue(const hir::Instr* instr,
                                  const hir::Value* value,
                                  uint32_t* out_offset) {
  if (!instr || instr->GetOpcodeNum() != hir::OPCODE_STORE_CONTEXT ||
      instr->src2.value != value) {
    return false;
  }
  *out_offset = static_cast<uint32_t>(instr->src1.offset);
  return true;
}

static bool IsPpcCrGtEqStorePair(uint32_t gt_offset, uint32_t eq_offset) {
  const uint32_t cr_base = static_cast<uint32_t>(offsetof(ppc::PPCContext, cr0));
  const uint32_t cr_end = cr_base + 8 * 4;
  return gt_offset + 1 == eq_offset && gt_offset >= cr_base + 1 &&
         gt_offset < cr_end && ((gt_offset - cr_base) & 3) == 1;
}

static bool IsPpcCrLtGtEqStoreTriplet(uint32_t lt_offset, uint32_t gt_offset,
                                      uint32_t eq_offset) {
  const uint32_t cr_base = static_cast<uint32_t>(offsetof(ppc::PPCContext, cr0));
  const uint32_t cr_end = cr_base + 8 * 4;
  return lt_offset + 1 == gt_offset && gt_offset + 1 == eq_offset &&
         lt_offset >= cr_base && eq_offset < cr_end &&
         ((lt_offset - cr_base) & 3) == 0;
}

static const hir::Instr* BranchOnCompareValue(const hir::Instr* instr,
                                              const hir::Value* a,
                                              Cond a_cond,
                                              const hir::Value* b,
                                              Cond b_cond,
                                              const hir::Value* c,
                                              Cond c_cond,
                                              Cond* out_true_cond) {
  const bool broaden_branch_match =
      cvars::arm64_cr_compare_branch_across_context_barrier;
  if (broaden_branch_match && instr &&
      instr->GetOpcodeNum() == hir::OPCODE_CONTEXT_BARRIER) {
    instr = instr->next;
  }
  if (!instr) {
    return nullptr;
  }
  switch (instr->GetOpcodeNum()) {
    case hir::OPCODE_BRANCH_TRUE:
    case hir::OPCODE_BRANCH_FALSE:
      break;
    default:
      return nullptr;
  }
  const hir::Value* branch_value = instr->src1.value;
  if (!broaden_branch_match) {
    if (branch_value == c) {
      *out_true_cond = c_cond;
      return instr;
    }
    return nullptr;
  }
  if (branch_value == a) {
    *out_true_cond = a_cond;
    return instr;
  }
  if (branch_value == b) {
    *out_true_cond = b_cond;
    return instr;
  }
  if (branch_value == c) {
    *out_true_cond = c_cond;
    return instr;
  }
  return nullptr;
}

static void EmitBranchOnCompareValue(A64Emitter* e, const hir::Instr* branch,
                                     Cond true_cond) {
  auto false_cond =
      static_cast<Cond>((static_cast<uint32_t>(true_cond) ^ 1u) & 0xFu);
  auto& target = e->GetLabel(branch->src2.label->id);
  e->b(branch->GetOpcodeNum() == hir::OPCODE_BRANCH_TRUE ? true_cond
                                                         : false_cond,
       target);
}

static bool ShouldElideCrStoresForFusedBranch(const A64Emitter& e,
                                              const hir::Instr* branch) {
  if (!branch || !cvars::arm64_cr_store_elide_for_fused_branch) {
    return false;
  }
  uint32_t function_filter =
      cvars::arm64_cr_store_elide_for_fused_branch_function;
  return !function_filter || function_filter == e.current_guest_function();
}

static bool EmitIntegerCompareFlags(A64Emitter& e, const hir::Instr* instr) {
  const hir::Value* src1 = instr->src1.value;
  const hir::Value* src2 = instr->src2.value;
  if (!src1 || !src2 || src1->type != src2->type ||
      !IsIntegerType(src1->type)) {
    return false;
  }

  switch (src1->type) {
    case hir::INT8_TYPE: {
      if (src1->IsConstant()) {
        e.mov(e.w0, IntegerValueBits(src1));
        if (src2->IsConstant()) {
          e.mov(e.w1, IntegerValueBits(src2));
          e.cmp(e.w0, e.w1);
        } else {
          WReg src2_reg(0);
          A64Emitter::SetupReg(src2, src2_reg);
          e.cmp(e.w0, src2_reg);
        }
      } else if (src2->IsConstant()) {
        WReg src1_reg(0);
        A64Emitter::SetupReg(src1, src1_reg);
        e.cmp(src1_reg, static_cast<uint32_t>(IntegerValueBits(src2) & 0xFF));
      } else {
        WReg src1_reg(0);
        WReg src2_reg(0);
        A64Emitter::SetupReg(src1, src1_reg);
        A64Emitter::SetupReg(src2, src2_reg);
        e.cmp(src1_reg, src2_reg);
      }
      return true;
    }
    case hir::INT16_TYPE: {
      if (src1->IsConstant()) {
        e.mov(e.w0, IntegerValueBits(src1));
        if (src2->IsConstant()) {
          e.mov(e.w1, IntegerValueBits(src2));
          e.cmp(e.w0, e.w1);
        } else {
          WReg src2_reg(0);
          A64Emitter::SetupReg(src2, src2_reg);
          e.cmp(e.w0, src2_reg);
        }
      } else if (src2->IsConstant()) {
        WReg src1_reg(0);
        A64Emitter::SetupReg(src1, src1_reg);
        uint32_t imm = static_cast<uint32_t>(IntegerValueBits(src2) & 0xFFFF);
        if (imm <= 4095) {
          e.cmp(src1_reg, imm);
        } else {
          e.mov(e.w0, imm);
          e.cmp(src1_reg, e.w0);
        }
      } else {
        WReg src1_reg(0);
        WReg src2_reg(0);
        A64Emitter::SetupReg(src1, src1_reg);
        A64Emitter::SetupReg(src2, src2_reg);
        e.cmp(src1_reg, src2_reg);
      }
      return true;
    }
    case hir::INT32_TYPE: {
      if (src1->IsConstant()) {
        e.mov(e.w0, IntegerValueBits(src1));
        if (src2->IsConstant()) {
          e.mov(e.w1, IntegerValueBits(src2));
          e.cmp(e.w0, e.w1);
        } else {
          WReg src2_reg(0);
          A64Emitter::SetupReg(src2, src2_reg);
          e.cmp(e.w0, src2_reg);
        }
      } else if (src2->IsConstant()) {
        WReg src1_reg(0);
        A64Emitter::SetupReg(src1, src1_reg);
        uint32_t imm = static_cast<uint32_t>(IntegerValueBits(src2));
        if (imm <= 4095) {
          e.cmp(src1_reg, imm);
        } else {
          e.mov(e.w0, imm);
          e.cmp(src1_reg, e.w0);
        }
      } else {
        WReg src1_reg(0);
        WReg src2_reg(0);
        A64Emitter::SetupReg(src1, src1_reg);
        A64Emitter::SetupReg(src2, src2_reg);
        e.cmp(src1_reg, src2_reg);
      }
      return true;
    }
    case hir::INT64_TYPE: {
      if (src1->IsConstant()) {
        e.mov(e.x0, IntegerValueBits(src1));
        if (src2->IsConstant()) {
          e.mov(e.x1, IntegerValueBits(src2));
          e.cmp(e.x0, e.x1);
        } else {
          XReg src2_reg(0);
          A64Emitter::SetupReg(src2, src2_reg);
          e.cmp(e.x0, src2_reg);
        }
      } else if (src2->IsConstant()) {
        XReg src1_reg(0);
        A64Emitter::SetupReg(src1, src1_reg);
        uint64_t imm = IntegerValueBits(src2);
        if (imm <= 4095) {
          e.cmp(src1_reg, static_cast<uint32_t>(imm));
        } else {
          e.mov(e.x0, imm);
          e.cmp(src1_reg, e.x0);
        }
      } else {
        XReg src1_reg(0);
        XReg src2_reg(0);
        A64Emitter::SetupReg(src1, src1_reg);
        A64Emitter::SetupReg(src2, src2_reg);
        e.cmp(src1_reg, src2_reg);
      }
      return true;
    }
    default:
      return false;
  }
}

static bool TrySelectIntegerCrTripletCompareStores(A64Emitter* e,
                                                   const hir::Instr* instr,
                                                   const hir::Instr** new_tail) {
  if (!instr || !instr->dest || instr->dest->type != hir::INT8_TYPE) {
    return false;
  }

  bool is_signed = false;
  if (instr->GetOpcodeNum() == hir::OPCODE_COMPARE_SLT) {
    is_signed = true;
  } else if (instr->GetOpcodeNum() == hir::OPCODE_COMPARE_ULT) {
    is_signed = false;
  } else {
    return false;
  }

  const hir::Instr* lt_store = instr->next;
  uint32_t lt_offset = 0;
  if (!IsStoreContextOfValue(lt_store, instr->dest, &lt_offset)) {
    return false;
  }

  const hir::Instr* gt_compare = lt_store->next;
  hir::Opcode expected_gt =
      is_signed ? hir::OPCODE_COMPARE_SGT : hir::OPCODE_COMPARE_UGT;
  if (!gt_compare || gt_compare->GetOpcodeNum() != expected_gt ||
      !gt_compare->dest || gt_compare->dest->type != hir::INT8_TYPE ||
      !CompareOperandsMatch(instr, gt_compare)) {
    return false;
  }

  const hir::Instr* gt_store = gt_compare->next;
  uint32_t gt_offset = 0;
  if (!IsStoreContextOfValue(gt_store, gt_compare->dest, &gt_offset)) {
    return false;
  }

  const hir::Instr* eq_compare = gt_store->next;
  if (!eq_compare || eq_compare->GetOpcodeNum() != hir::OPCODE_COMPARE_EQ ||
      !eq_compare->dest || eq_compare->dest->type != hir::INT8_TYPE ||
      !CompareOperandsMatch(instr, eq_compare)) {
    return false;
  }

  const hir::Instr* eq_store = eq_compare->next;
  uint32_t eq_offset = 0;
  if (!IsStoreContextOfValue(eq_store, eq_compare->dest, &eq_offset) ||
      !IsPpcCrLtGtEqStoreTriplet(lt_offset, gt_offset, eq_offset)) {
    return false;
  }

  Cond branch_true_cond = Xbyak_aarch64::EQ;
  const hir::Instr* branch = BranchOnCompareValue(
      eq_store->next, instr->dest,
      is_signed ? Xbyak_aarch64::LT : Xbyak_aarch64::LO, gt_compare->dest,
      is_signed ? Xbyak_aarch64::GT : Xbyak_aarch64::HI, eq_compare->dest,
      Xbyak_aarch64::EQ, &branch_true_cond);
  if (!EmitIntegerCompareFlags(*e, instr)) {
    return false;
  }
  const bool elide_cr_stores = ShouldElideCrStoresForFusedBranch(*e, branch);

  // Later HIR can still read these compare values. Emit the same cset results
  // into their assigned value registers, then skip only the redundant compare
  // and CR-store instructions.
  WReg lt_reg(0);
  WReg gt_reg(0);
  WReg eq_reg(0);
  A64Emitter::SetupReg(instr->dest, lt_reg);
  A64Emitter::SetupReg(gt_compare->dest, gt_reg);
  A64Emitter::SetupReg(eq_compare->dest, eq_reg);
  if (elide_cr_stores) {
    e->cset(lt_reg, is_signed ? Xbyak_aarch64::LT : Xbyak_aarch64::LO);
    e->cset(gt_reg, is_signed ? Xbyak_aarch64::GT : Xbyak_aarch64::HI);
    e->cset(eq_reg, Xbyak_aarch64::EQ);
  } else {
    e->cset(lt_reg, is_signed ? Xbyak_aarch64::LT : Xbyak_aarch64::LO);
    e->strb(lt_reg, ptr(e->GetContextReg(), lt_offset));
    e->cset(gt_reg, is_signed ? Xbyak_aarch64::GT : Xbyak_aarch64::HI);
    e->strb(gt_reg, ptr(e->GetContextReg(), gt_offset));
    e->cset(eq_reg, Xbyak_aarch64::EQ);
    e->strb(eq_reg, ptr(e->GetContextReg(), eq_offset));
  }
  if (branch) {
    EmitBranchOnCompareValue(e, branch, branch_true_cond);
    *new_tail = branch->next;
  } else {
    *new_tail = eq_store->next;
  }
  return true;
}

static bool TrySelectUnsignedGtEqCompareStores(A64Emitter* e,
                                               const hir::Instr* instr,
                                               const hir::Instr** new_tail) {
  if (!instr || instr->GetOpcodeNum() != hir::OPCODE_COMPARE_UGT ||
      !instr->dest || instr->dest->type != hir::INT8_TYPE) {
    return false;
  }

  const hir::Instr* gt_store = instr->next;
  uint32_t gt_offset = 0;
  if (!IsStoreContextOfValue(gt_store, instr->dest, &gt_offset)) {
    return false;
  }

  const hir::Instr* eq_compare = gt_store->next;
  if (!eq_compare || eq_compare->GetOpcodeNum() != hir::OPCODE_COMPARE_EQ ||
      !eq_compare->dest || eq_compare->dest->type != hir::INT8_TYPE ||
      !CompareOperandsMatch(instr, eq_compare)) {
    return false;
  }

  const hir::Instr* eq_store = eq_compare->next;
  uint32_t eq_offset = 0;
  if (!IsStoreContextOfValue(eq_store, eq_compare->dest, &eq_offset) ||
      !IsPpcCrGtEqStorePair(gt_offset, eq_offset)) {
    return false;
  }

  Cond branch_true_cond = Xbyak_aarch64::EQ;
  const hir::Instr* branch =
      BranchOnCompareValue(eq_store->next, nullptr, Xbyak_aarch64::EQ,
                           instr->dest, Xbyak_aarch64::HI, eq_compare->dest,
                           Xbyak_aarch64::EQ, &branch_true_cond);
  if (!EmitIntegerCompareFlags(*e, instr)) {
    return false;
  }
  const bool elide_cr_stores = ShouldElideCrStoresForFusedBranch(*e, branch);

  // Preserve the materialized compare values for any later users while
  // collapsing the adjacent CR stores to one flags-producing compare.
  WReg gt_reg(0);
  WReg eq_reg(0);
  A64Emitter::SetupReg(instr->dest, gt_reg);
  A64Emitter::SetupReg(eq_compare->dest, eq_reg);
  if (elide_cr_stores) {
    e->cset(gt_reg, Xbyak_aarch64::HI);
    e->cset(eq_reg, Xbyak_aarch64::EQ);
  } else {
    e->cset(gt_reg, Xbyak_aarch64::HI);
    e->strb(gt_reg, ptr(e->GetContextReg(), gt_offset));
    e->cset(eq_reg, Xbyak_aarch64::EQ);
    e->strb(eq_reg, ptr(e->GetContextReg(), eq_offset));
  }
  if (branch) {
    EmitBranchOnCompareValue(e, branch, branch_true_cond);
    *new_tail = branch->next;
  } else {
    *new_tail = eq_store->next;
  }
  return true;
}

static bool TrySelectUnsignedZeroCompareStoreContext(A64Emitter* e,
                                                     const hir::Instr* instr,
                                                     const hir::Instr** new_tail) {
  if (!IsUnsignedZeroCompareAlwaysFalse(instr) || !instr->dest ||
      instr->dest->type != hir::INT8_TYPE) {
    return false;
  }

  const hir::Instr* store = instr->next;
  if (!store || store->GetOpcodeNum() != hir::OPCODE_STORE_CONTEXT ||
      store->src2.value != instr->dest ||
      !ValueHasOnlyUse(instr->dest, store)) {
    return false;
  }

  const uint32_t offset = static_cast<uint32_t>(store->src1.offset);
  switch (store->src2.value->type) {
    case hir::INT8_TYPE:
      e->strb(e->wzr, ptr(e->GetContextReg(), offset));
      *new_tail = store->next;
      return true;
    case hir::INT16_TYPE:
      e->strh(e->wzr, ptr(e->GetContextReg(), offset));
      *new_tail = store->next;
      return true;
    case hir::INT32_TYPE:
      e->str(e->wzr, ptr(e->GetContextReg(), offset));
      *new_tail = store->next;
      return true;
    case hir::INT64_TYPE:
      e->str(e->xzr, ptr(e->GetContextReg(), offset));
      *new_tail = store->next;
      return true;
    default:
      return false;
  }
}

}  // namespace

// ============================================================================
// SelectSequence — dispatch an instruction to its sequence handler
// ============================================================================
bool SelectSequence(A64Emitter* e, const hir::Instr* i,
                    const hir::Instr** new_tail) {
  if (TrySelectUnsignedZeroCompareStoreContext(e, i, new_tail)) {
    return true;
  }
  if (TrySelectIntegerCrTripletCompareStores(e, i, new_tail)) {
    return true;
  }
  if (TrySelectUnsignedGtEqCompareStores(e, i, new_tail)) {
    return true;
  }

  const InstrKey key(i);
  auto& sequence_table = SequenceTable();
  auto it = sequence_table.find(key);
  if (it != sequence_table.end()) {
    if (it->second(*e, i, InstrKeyValue(key))) {
      *new_tail = i->next;
      return true;
    }
  }
  XELOGE("A64: No sequence match for opcode: {} ({})",
         hir::GetOpcodeName(i->GetOpcodeInfo()),
         static_cast<int>(i->GetOpcodeInfo()->num));
  fprintf(stderr, "A64: No sequence match for opcode: %s (%d)\n",
          hir::GetOpcodeName(i->GetOpcodeInfo()),
          static_cast<int>(i->GetOpcodeInfo()->num));
  return false;
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
