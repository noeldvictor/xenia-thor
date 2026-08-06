/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_A64_A64_SEQ_UTIL_H_
#define XENIA_CPU_BACKEND_A64_A64_SEQ_UTIL_H_

#include <atomic>

#include "xenia/base/cvar.h"
#include "xenia/base/memory.h"
#include "xenia/base/vec128.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_emitter.h"
#include "xenia/cpu/backend/a64/a64_op.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"

#include "xbyak_aarch64.h"

#if XE_COMPILER_MSVC
#include <intrin.h>
constexpr uint32_t DCZID_EL0 = ARM64_SYSREG(0b11, 0b011, 0b0000, 0b0000, 0b111);
#define xe_cpu_mrs(reg) _ReadStatusReg(reg)
#elif XE_COMPILER_CLANG || XE_COMPILER_GNUC
#include <arm_acle.h>
#define xe_cpu_mrs(reg) __arm_rsr64(#reg)
#else
#error "No MRS wrapper available for current compiler implemented."
#endif

// R3 (flat membase): see a64_seq_memory.cc for the definition / rationale.
DECLARE_bool(a64_v128_const_pool);
DECLARE_bool(a64_vmx_fp_no_operand_copy);
DECLARE_bool(arm64_use_flat_membase);

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using Xbyak_aarch64::QReg;
using Xbyak_aarch64::VReg;
using Xbyak_aarch64::WReg;
using Xbyak_aarch64::XReg;

template <typename Fn>
inline void EmitWithVmxFpcr(A64Emitter& e, Fn&& emit_op) {
  // Enter VMX FPCR mode using tracked lazy switching.  If the emitter
  // is already in VMX mode (e.g. consecutive VMX ops in the same basic
  // block) this is a no-op — no system register access at all.
  // FPU mode is restored at block boundaries and calls via ForgetFpcrMode,
  // or on demand by scalar FP sequences via ChangeFpcrMode(Fpu).
  e.ChangeFpcrMode(FPCRMode::Vmx);
  emit_op();
}

// True iff the 64-bit value is encodable as a MOVI Dn, #imm8 immediate.
// MOVI replicates the 8-bit immediate "a:b:c:d:e:f:g:h" into the bytes
// "aaaaaaaabbbbbbbbccccccccddddddddeeeeeeeeffffffffgggggggghhhhhhhh", so
// every byte of the input must be 0x00 or 0xFF. (oaknut's movi(VReg.2D, u64)
// takes the FULL 64-bit value and encodes it itself - passing a pre-compressed
// imm8 crashed on encodable splat constants. Ported from xenia-edge 0a18453bc.)
inline bool IsMovi64Imm(uint64_t value) {
  if (value == 0 || value == ~uint64_t(0)) {
    return true;
  }
  for (int shift = 0; shift < 8; ++shift) {
    const uint8_t shift_u8 = static_cast<uint8_t>(value >> (shift * 8));
    if (shift_u8 != 0 && shift_u8 != 0xFF) {
      return false;
    }
  }
  return true;
}

// Try to see if the provided double value can be compressed into an 8-bit value
// for the fmov instruction. Returns false if the value cannot be represented
// abcdefgh
//    V
// aBbbbbbc defgh000 00000000 00000000
// B = NOT(b)
inline bool IsFmov32Imm(float f32) {
  const uint32_t u32 = xe::memory::Reinterpret<uint32_t>(f32);
  const uint32_t sign = (u32 >> 31) & 1;
  int32_t exp = ((u32 >> 23) & 0xff) - 127;
  int64_t mantissa = u32 & 0x7fffff;

  // Too many mantissa bits
  if (mantissa & 0x7ffff) {
    return false;
  }
  // Too many exp bits
  if (exp < -3 || exp > 4) {
    return false;
  }

  // mantissa = (16 + e:f:g:h) / 16.
  mantissa >>= 19;
  if ((mantissa & 0b1111) != mantissa) {
    return false;
  }

  return true;
}

// Try to see if the provided double value can be compressed into an 8-bit value
// for the fmov instruction. Returns false if the value cannot be represented
// abcdefgh
//    V
// aBbbbbbb bbcdefgh 00000000 00000000 00000000 00000000 00000000 00000000
// B = NOT(b)
inline bool IsFmov64Imm(double f64) {
  const uint64_t u64 = xe::memory::Reinterpret<uint64_t>(f64);
  int32_t exp = ((u64 >> 52) & 0x7ff) - 1023;
  int64_t mantissa = u64 & 0xfffffffffffffULL;

  // Too many mantissa bits
  if (mantissa & 0xffffffffffffULL) {
    return false;
  }
  // Too many exp bits
  if (exp < -3 || exp > 4) {
    return false;
  }

  // mantissa = (16 + e:f:g:h) / 16.
  mantissa >>= 48;
  if ((mantissa & 0b1111) != mantissa) {
    return false;
  }
  return true;
}

// Load a compile-time vec128_t constant into a NEON register.
// May clobber the provided GPR scratch-register
inline void LoadV128Const(A64Emitter& e, int vreg_idx, const vec128_t& val,
                          int gpr_scratch_idx = 0) {
  // Fast common cases
  if (!val.low && !val.high) {
    // 0000...
    e.movi(VReg2D(vreg_idx), 0);
    return;
  } else if (val.low == ~uint64_t(0) && val.high == ~uint64_t(0)) {
    // 1111...
    e.movi(VReg2D(vreg_idx), ~0ULL);
    return;
  }

  // Element splats
  bool all_equal_u8 = true;
  const uint8_t splat_u8 = val.u8[0];
  for (unsigned i = 1; i < 16; ++i) {
    if (val.u8[i] != splat_u8) {
      all_equal_u8 = false;
      break;
    }
  }
  if (all_equal_u8) {
    e.movi(VReg(vreg_idx).b16, static_cast<uint8_t>(splat_u8));
    return;
  }

  bool all_equal_u16 = true;
  const uint16_t splat_u16 = val.u16[0];
  for (unsigned i = 1; i < 8; ++i) {
    if (val.u16[i] != splat_u16) {
      all_equal_u16 = false;
      break;
    }
  }
  if (all_equal_u16) {
    if ((splat_u16 & 0xFF'00) == 0) {
      e.movi(VReg(vreg_idx).h8, static_cast<uint8_t>(splat_u16 >> 0), LSL, 0);
    } else if ((splat_u16 & 0x00'FF) == 0) {
      e.movi(VReg(vreg_idx).h8, static_cast<uint8_t>(splat_u16 >> 8), LSL, 8);
    } else if ((splat_u16 & 0xFF'00) == 0xFF'00) {
      e.mvni(VReg(vreg_idx).h8, ~static_cast<uint8_t>(splat_u16 >> 0) & 0xFF,
             LSL, 0);
    } else if ((splat_u16 & 0x00'FF) == 0x00'FF) {
      e.mvni(VReg(vreg_idx).h8, ~static_cast<uint8_t>(splat_u16 >> 8) & 0xFF,
             LSL, 8);
    } else {
      e.movz(WReg(gpr_scratch_idx), splat_u16, 0);
      e.dup(VReg(vreg_idx).h8, WReg(gpr_scratch_idx));
    }
    return;
  }

  bool all_equal_u32 = true;
  const uint32_t splat_u32 = val.u32[0];
  const float splat_f32 = val.f32[0];
  for (unsigned i = 1; i < 4; ++i) {
    if (val.u32[i] != splat_u32) {
      all_equal_u32 = false;
      break;
    }
  }
  if (all_equal_u32) {
    if ((splat_u32 & 0xFF'FF'FF'00) == 0) {
      e.movi(VReg(vreg_idx).s4, static_cast<uint8_t>(splat_u32 >> 0), LSL, 0);
    } else if ((splat_u32 & 0xFF'FF'00'FF) == 0) {
      e.movi(VReg(vreg_idx).s4, static_cast<uint8_t>(splat_u32 >> 8), LSL, 8);
    } else if ((splat_u32 & 0xFF'00'FF'FF) == 0) {
      e.movi(VReg(vreg_idx).s4, static_cast<uint8_t>(splat_u32 >> 16), LSL, 16);
    } else if ((splat_u32 & 0x00'FF'FF'FF) == 0) {
      e.movi(VReg(vreg_idx).s4, static_cast<uint8_t>(splat_u32 >> 24), LSL, 24);
    } else if ((splat_u32 & 0xFF'FF'FF'00) == 0xFF'FF'FF'00) {
      e.mvni(VReg(vreg_idx).s4, ~static_cast<uint8_t>(splat_u32 >> 0) & 0xFF,
             LSL, 0);
    } else if ((splat_u32 & 0xFF'FF'00'FF) == 0xFF'FF'00'FF) {
      e.mvni(VReg(vreg_idx).s4, ~static_cast<uint8_t>(splat_u32 >> 8) & 0xFF,
             LSL, 8);
    } else if ((splat_u32 & 0xFF'00'FF'FF) == 0xFF'00'FF'FF) {
      e.mvni(VReg(vreg_idx).s4, ~static_cast<uint8_t>(splat_u32 >> 16) & 0xFF,
             LSL, 16);
    } else if ((splat_u32 & 0x00'FF'FF'FF) == 0x00'FF'FF'FF) {
      e.mvni(VReg(vreg_idx).s4, ~static_cast<uint8_t>(splat_u32 >> 24) & 0xFF,
             LSL, 24);
    } else if (IsFmov32Imm(splat_f32)) {
      e.fmov(VReg(vreg_idx).s4, splat_f32);
    } else {
      e.mov(WReg(gpr_scratch_idx), splat_u32);
      e.dup(VReg(vreg_idx).s4, WReg(gpr_scratch_idx));
    }
    return;
  }

  const bool all_equal_u64 = val.low == val.high;
  const uint64_t splat_u64 = val.u64[0];
  const double splat_f64 = val.f64[0];
  if (all_equal_u64) {
    if (IsMovi64Imm(val.low)) {
      e.movi(VReg2D(vreg_idx), val.low);
    } else if (IsFmov64Imm(splat_f64)) {
      e.fmov(VReg(vreg_idx).d2, splat_f64);
    } else {
      e.mov(XReg(gpr_scratch_idx), splat_u64);
      e.dup(VReg(vreg_idx).d2, XReg(gpr_scratch_idx));
    }
    return;
  }

  // Fallback: an arbitrary 128-bit constant.
  //
  // The MOV/FMOV/MOV/INS chain below is up to TEN instructions - each MOV is a
  // MOVZ plus up to three MOVK, all on the ARITHMETIC ports and each dependent
  // on the last - because x86 can encode a 64-bit immediate inline and ARM64
  // cannot. ARM64's answer is a PC-relative literal: ONE LDR on the LOAD ports,
  // which are the abundant resource here (three 128-bit load ports against two
  // arithmetic on the A715/A710 mid-cores). It also leaves the GPR scratch free.
  if (cvars::a64_v128_const_pool) {
    e.ldr(QReg(vreg_idx), e.GetV128ConstLabel(val));
    return;
  }
  e.mov(XReg(gpr_scratch_idx), val.low);
  e.fmov(DReg(vreg_idx), XReg(gpr_scratch_idx));
  e.mov(XReg(gpr_scratch_idx), val.high);
  e.ins(VReg(vreg_idx).d2[1], XReg(gpr_scratch_idx));
}

// Resolve a V128 operand to a register index, loading constants into
// scratch_idx if needed.
template <typename T>
inline int SrcVReg(A64Emitter& e, const T& op, int scratch_idx) {
  if (op.is_constant) {
    LoadV128Const(e, scratch_idx, op.constant());
    return scratch_idx;
  }
  return op.reg().getIdx();
}

// Compute a guest memory address, returning the XReg for [x21, xN] addressing.
// For constants, loads the address into x0 (scratch).
inline XReg ComputeMemoryAddress(A64Emitter& e, const I64Op& guest) {
  using namespace Xbyak_aarch64;
  if (guest.is_constant) {
    uint32_t address = static_cast<uint32_t>(guest.constant());
    if (address >= 0xE0000000 &&
        xe::memory::allocation_granularity() > 0x1000) {
      address += 0x1000;
    }
    e.mov(e.x0, static_cast<uint64_t>(address));
    return e.x0;
  } else {
    auto src = guest.reg();
    // Guest addresses are always 32-bit. Clear any stale upper bits before
    // applying the host membase so guest pointers can't escape above 4 GB.
    e.mov(e.w0, WReg(src.getIdx()));
    if (xe::memory::allocation_granularity() > 0x1000) {
      e.mov(e.w17, 0xE0000000u);
      e.cmp(e.w0, e.w17);
      auto& skip = e.NewCachedLabel();
      e.b(LO, skip);
      e.add(e.w0, e.w0, 1, 12);  // add 0x1000 via LSL #12
      e.L(skip);
    }
    return e.x0;
  }
}

// R3 (arm64_use_flat_membase): true when a guest memory access can fold its
// 32-bit guest address straight into the load/store's [membase, Wn, UXTW]
// indexed addressing mode - eliding the `mov w0, wGuest` zero-extend and freeing
// x0 on every access. Only valid for a non-constant guest register on a platform
// that needs NO large-page +0x1000 fixup (Android: allocation_granularity() ==
// page size). On Windows (64K granularity) the fixup is required, so this returns
// false and ComputeMemoryAddress (which emits the fixup) is used - keeping codegen
// byte-identical when the cvar is off OR a fixup is needed.
inline bool CanFoldFlatGuestAddress(const I64Op& guest) {
  return cvars::arm64_use_flat_membase && !guest.is_constant &&
         xe::memory::allocation_granularity() <= 0x1000;
}

// Invoke `emit(mem)` with the address operand for a guest memory access: the
// folded [membase, Wn, UXTW] form when CanFoldFlatGuestAddress(), else the
// classic [membase, x0] form after ComputeMemoryAddress. `emit` is a generic
// lambda and MUST accept both Xbyak_aarch64::AdrExt (folded) and
// Xbyak_aarch64::AdrReg (classic) - i.e. take `auto&& mem`. Use for guest loads
// and watch-free guest stores.
template <typename EmitFn>
inline void WithGuestMemAddress(A64Emitter& e, const I64Op& guest,
                                EmitFn&& emit) {
  using namespace Xbyak_aarch64;
  if (CanFoldFlatGuestAddress(guest)) {
    emit(ptr(e.GetMembaseReg(), WReg(guest.reg().getIdx()), UXTW));
  } else {
    XReg addr = ComputeMemoryAddress(e, guest);
    emit(ptr(e.GetMembaseReg(), addr));
  }
}

// Like WithGuestMemAddress but also passes the guest address as an XReg for
// EmitGuestStoreWatch (which, when the store-watch list is non-empty, needs a
// register holding the runtime guest address). In the folded path that is the
// guest source register itself - its W view holds the 32-bit guest address; in
// the classic path it is the computed x0. `emit` takes
// `(auto&& mem, Xbyak_aarch64::XReg watch_addr)`.
template <typename EmitFn>
inline void WithGuestStoreAddress(A64Emitter& e, const I64Op& guest,
                                  EmitFn&& emit) {
  using namespace Xbyak_aarch64;
  if (CanFoldFlatGuestAddress(guest)) {
    emit(ptr(e.GetMembaseReg(), WReg(guest.reg().getIdx()), UXTW),
         XReg(guest.reg().getIdx()));
  } else {
    XReg addr = ComputeMemoryAddress(e, guest);
    emit(ptr(e.GetMembaseReg(), addr), addr);
  }
}

template <typename OffsetOp>
inline XReg AddGuestMemoryOffset(A64Emitter& e, const XReg& base,
                                 const OffsetOp& offset) {
  // Guest address arithmetic wraps at 32 bits before the host membase is
  // applied. Keep the add in W registers so stale high bits can't escape into
  // the final host pointer.
  if (base.getIdx() != e.x0.getIdx()) {
    e.mov(e.w0, WReg(base.getIdx()));
  }
  if (offset.is_constant) {
    uint32_t value = static_cast<uint32_t>(offset.constant());
    if (value == 0) {
      return e.x0;
    }
    if (value <= 4095) {
      e.add(e.w0, e.w0, value);
    } else if ((value & 0xFFFu) == 0 && (value >> 12) <= 4095) {
      e.add(e.w0, e.w0, value >> 12, 12);
    } else {
      e.mov(e.w17, static_cast<uint64_t>(value));
      e.add(e.w0, e.w0, e.w17);
    }
  } else {
    e.add(e.w0, e.w0, WReg(offset.reg().getIdx()));
  }
  return e.x0;
}

template <typename OffsetOp>
inline XReg ComputeMemoryAddressOffset(A64Emitter& e, const I64Op& guest,
                                       const OffsetOp& offset) {
  // Keep the existing two-step path when large-page physical mapping
  // compensation is required; this helper only folds 4 KB mapping cases.
  if (!offset.is_constant || xe::memory::allocation_granularity() > 0x1000) {
    return AddGuestMemoryOffset(e, ComputeMemoryAddress(e, guest), offset);
  }

  uint32_t value = static_cast<uint32_t>(offset.constant());
  if (guest.is_constant) {
    uint32_t address = static_cast<uint32_t>(guest.constant()) + value;
    e.mov(e.x0, static_cast<uint64_t>(address));
    return e.x0;
  }

  WReg source(guest.reg().getIdx());
  if (value == 0) {
    e.mov(e.w0, source);
  } else if (value <= 4095) {
    e.add(e.w0, source, value);
  } else if ((value & 0xFFFu) == 0 && (value >> 12) <= 4095) {
    e.add(e.w0, source, value >> 12, 12);
  } else {
    e.mov(e.w0, source);
    e.mov(e.w17, static_cast<uint64_t>(value));
    e.add(e.w0, e.w0, e.w17);
  }
  return e.x0;
}

// Flush denormal float32 lanes to zero in a NEON register (in-place).
// A float32 is denormal when 0 < abs(val) < 0x00800000.
// vreg must not equal sa or sb.
// This is needed because FPCR.FZ may not flush denormal inputs on all ARM64
// implementations (the ARM spec says input flushing is implementation-defined).
inline void FlushDenormals_V128(A64Emitter& e, int vreg, int sa = 2,
                                int sb = 3) {
  // val<<1 removes the sign bit and doubles the value.
  // Denormals become [0x00000002, 0x00FFFFFE]; zeros become 0x00000000.
  // (val<<1) - 1: wraps 0→0xFFFFFFFF (excluded),
  // denorms→[0x00000001,0x00FFFFFD]. Denormal iff ((val<<1) - 1) < 0x00FFFFFF
  // (unsigned).
  e.shl(VReg(sa).s4, VReg(vreg).s4, 1);
  e.movi(VReg(sb).s4, 1u);
  e.sub(VReg(sa).s4, VReg(sa).s4, VReg(sb).s4);
  e.mvni(VReg(sb).s4, 0xFFu, LSL, 24);  // 0x00FFFFFF
  e.cmhi(VReg(sb).s4, VReg(sb).s4,
         VReg(sa).s4);  // mask: all-1s for denormal lanes
  // Clear only bits 30:0 (preserve sign bit 31) so -denormal → -0, +denormal →
  // +0.
  e.ushr(VReg(sa).s4, VReg(sb).s4, 1);  // sa = mask with bit 31 cleared
  e.bic(VReg(vreg).b16, VReg(vreg).b16, VReg(sa).b16);
}

inline void AuditV128DenormalIfAny(A64Emitter& e, int vreg,
                                   std::atomic<uint64_t>* counter, int sa = 2,
                                   int sb = 3) {
  if (!counter) {
    return;
  }

  // Same denormal predicate as FlushDenormals_V128, but only records whether
  // any lane would be flushed. Clobbers sa, sb, and w0.
  auto& denormal_found = e.NewCachedLabel();
  auto& done = e.NewCachedLabel();
  e.shl(VReg(sa).s4, VReg(vreg).s4, 1);
  e.movi(VReg(sb).s4, 1u);
  e.sub(VReg(sa).s4, VReg(sa).s4, VReg(sb).s4);
  e.mvni(VReg(sb).s4, 0xFFu, LSL, 24);
  e.cmhi(VReg(sb).s4, VReg(sb).s4, VReg(sa).s4);
  for (int lane = 0; lane < 4; ++lane) {
    e.umov(e.w0, VReg(sb).s4[lane]);
    e.cbnz(e.w0, denormal_found);
  }
  e.b(done);
  e.L(denormal_found);
  e.EmitAtomicIncrement64(counter);
  e.L(done);
}

// Fixup for vmaxfp/vminfp when BOTH inputs are NaN.
// ARM64 fmax/fmin with DN=0 correctly propagates NaN when only one input is
// NaN, but when both are NaN it may quiet an SNaN differently than x64.
// x64 uses maxps(a,b)|maxps(b,a) which effectively gives src1|src2 for NaN
// lanes. We replicate that: use src1|src2 only for lanes where BOTH are NaN.
// Expects: v0=flushed src1, v1=flushed src2, v2=hardware fmax/fmin result.
// Modifies v2 in place. Clobbers v0, v1, v3.
inline void FixupVmxMaxMinNan(A64Emitter& e, int s1 = 0, int s2 = 1) {
  // s1/s2 are the source registers. They default to the v0/v1 scratch pair for
  // callers that still stage their operands there; when the no-copy path in
  // PrepareVmxFpSources is active they are the ALLOCATED registers instead.
  // That is safe because the allocator only hands out v4-v31 (a64_backend.cc:
  // "v0-v3 scratch"), so writing v0/v1/v3 below can never clobber a source.
  // Compute OR fallback first (before clobbering the scratch).
  e.orr(VReg(3).b16, VReg(s1).b16, VReg(s2).b16);  // v3 = src1 | src2
  // Build "at least one not NaN" mask.
  e.fcmeq(VReg(0).s4, VReg(s1).s4, VReg(s1).s4);   // v0 = non-NaN mask for src1
  e.fcmeq(VReg(1).s4, VReg(s2).s4, VReg(s2).s4);   // v1 = non-NaN mask for src2
  e.orr(VReg(0).b16, VReg(0).b16, VReg(1).b16);  // v0 = 1 where at least one ok
  // BSL: mask=1 → v2 (fmax result), mask=0 → v3 (src1|src2 for both-NaN)
  e.bsl(VReg(0).b16, VReg(2).b16, VReg(3).b16);
  e.mov(VReg(2).b16, VReg(0).b16);
}

// Prepare two V128 operands for a VMX FP operation: copy to scratch v0/v1
// and flush denormals. Returns the flushed register indices (always 0 and 1).
template <typename T1, typename T2>
inline void PrepareVmxFpSources(A64Emitter& e, const T1& op1, const T2& op2,
                                int& out_s1, int& out_s2) {
  int s1 = SrcVReg(e, op1, 0);
  int s2 = SrcVReg(e, op2, 1);
  // The copies below exist ONLY to protect live registers from the DESTRUCTIVE
  // FlushDenormals_V128 calls that follow. That is an x86 habit: SSE is
  // two-operand destructive, so the x64 backend must stage operands. NEON is
  // three-operand non-destructive - fadd v2, vA, vB leaves vA/vB untouched - so
  // when no flush is needed the op can read the allocated registers directly
  // and both MOVs are pure waste. On this SoC FPCR.FZ flushes denormal inputs
  // in hardware (kA64FZFlushesInputs, probed at startup), so that is the
  // COMMON case, not a corner.
  //
  // Only taken when both operands are already in allocated registers: a
  // constant operand has been materialised into v0/v1 by SrcVReg, and the
  // downstream fixups use v0/v1/v3 as scratch, which would clobber it. The
  // allocator only hands out v4-v31, so an allocated source can never alias
  // that scratch.
  const bool flush_needed =
      !e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs);
  if (cvars::a64_vmx_fp_no_operand_copy && !flush_needed && s1 >= 4 &&
      s2 >= 4) {
    out_s1 = s1;
    out_s2 = s2;
    return;
  }
  // Copy to scratch v0/v1 so we don't modify live allocated registers.
  if (s1 != 0) e.mov(VReg(0).b16, VReg(s1).b16);
  if (s2 != 1) e.mov(VReg(1).b16, VReg(s2).b16);
  // Flush denormal inputs in software only if FPCR.FZ doesn't handle it.
  if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
    FlushDenormals_V128(e, 0);
    FlushDenormals_V128(e, 1);
  }
  out_s1 = 0;
  out_s2 = 1;
}

// Fix PPC NaN propagation for V128 float32 lanes after a NEON FP operation.
// Expects: v0=flushed src1, v1=flushed src2, v2=hardware FP result.
// Modifies v2 in place. Clobbers v0, v1, v3, w0, w16, w17.
// PPC rule: first NaN by operand position wins; SNaN is quieted (bit 22 set).
// If neither input was NaN but the op generated NaN (e.g., inf-inf),
// use the PPC default NaN (0xFFC00000).
inline void FixupVmxNan_V128(A64Emitter& e, int s1 = 0, int s2 = 1) {
  using namespace Xbyak_aarch64;
  auto& done = e.NewCachedLabel();

  // Fast path: if no result lane is NaN, skip entirely.
  e.fcmeq(VReg(3).s4, VReg(2).s4, VReg(2).s4);  // all-1s for non-NaN
  e.uminv(SReg(3), VReg(3).s4);                 // min across lanes
  e.fmov(e.w0, SReg(3));
  e.cbnz(e.w0, done);  // all non-NaN → skip

  // Save s1/s2 to stack for scalar lane extraction.
  e.str(QReg(s1), ptr(e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH)));
  e.str(QReg(s2),
        ptr(e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 16));

  // NaN threshold: (val<<1) > 0xFF000000 means val is NaN.
  e.mov(e.w16, 0xFF000000u);

  for (int lane = 0; lane < 4; lane++) {
    auto& lane_ok = e.NewCachedLabel();
    auto& s1_not_nan = e.NewCachedLabel();
    auto& use_default = e.NewCachedLabel();

    // Check if result[lane] is NaN.
    e.umov(e.w0, VReg(2).s4[lane]);
    e.lsl(e.w17, e.w0, 1);
    e.cmp(e.w17, e.w16);
    e.b(LS, lane_ok);

    // Result is NaN. Check s1[lane].
    e.ldr(e.w0, ptr(e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) +
                              lane * 4));
    e.lsl(e.w17, e.w0, 1);
    e.cmp(e.w17, e.w16);
    e.b(LS, s1_not_nan);

    // s1 is NaN: quiet it and insert.
    e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
    e.ins(VReg(2).s4[lane], e.w0);
    e.b(lane_ok);

    e.L(s1_not_nan);
    // Check s2[lane].
    e.ldr(e.w0, ptr(e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) +
                              16 + lane * 4));
    e.lsl(e.w17, e.w0, 1);
    e.cmp(e.w17, e.w16);
    e.b(LS, use_default);

    // s2 is NaN: quiet it and insert.
    e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
    e.ins(VReg(2).s4[lane], e.w0);
    e.b(lane_ok);

    e.L(use_default);
    // Generated NaN (neither input was NaN): use PPC default NaN.
    e.mov(e.w0, 0xFFC00000u);
    e.ins(VReg(2).s4[lane], e.w0);

    e.L(lane_ok);
  }

  e.L(done);
}

// Fix PPC NaN propagation for V128 FMA result (3 source operands).
// Expects: result in v2, flushed sources saved on stack at:
//   GUEST_SCRATCH + 0  = src1 (16 bytes)
//   GUEST_SCRATCH + 16 = src2 (16 bytes)
//   GUEST_SCRATCH + 32 = src3 (16 bytes)
// PPC rule: first NaN by operand position (src1 > src2 > src3) wins.
// Clobbers v0, v1, v3, w0, w16, w17.
inline void FixupVmxNan_V128_Fma(
    A64Emitter& e, std::atomic<uint64_t>* nan_entry_counter = nullptr,
    std::atomic<uint64_t>* nan_lane_counter = nullptr) {
  using namespace Xbyak_aarch64;
  auto& done = e.NewCachedLabel();

  // Fast path: if no result lane is NaN, skip entirely.
  e.fcmeq(VReg(3).s4, VReg(2).s4, VReg(2).s4);
  e.uminv(SReg(3), VReg(3).s4);
  e.fmov(e.w0, SReg(3));
  e.cbnz(e.w0, done);
  e.EmitAtomicIncrement64(nan_entry_counter);

  // NaN threshold constant.
  e.mov(e.w16, 0xFF000000u);

  for (int lane = 0; lane < 4; lane++) {
    auto& lane_ok = e.NewCachedLabel();
    auto& s1_not_nan = e.NewCachedLabel();
    auto& s2_not_nan = e.NewCachedLabel();
    auto& use_default = e.NewCachedLabel();

    // Check if result[lane] is NaN.
    e.umov(e.w0, VReg(2).s4[lane]);
    e.lsl(e.w17, e.w0, 1);
    e.cmp(e.w17, e.w16);
    e.b(LS, lane_ok);
    e.EmitAtomicIncrement64(nan_lane_counter);

    // Result is NaN. Check src1[lane].
    e.ldr(e.w0, ptr(e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) +
                              lane * 4));
    e.lsl(e.w17, e.w0, 1);
    e.cmp(e.w17, e.w16);
    e.b(LS, s1_not_nan);
    e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
    e.ins(VReg(2).s4[lane], e.w0);
    e.b(lane_ok);

    e.L(s1_not_nan);
    // Check src2[lane].
    e.ldr(e.w0, ptr(e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) +
                              16 + lane * 4));
    e.lsl(e.w17, e.w0, 1);
    e.cmp(e.w17, e.w16);
    e.b(LS, s2_not_nan);
    e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
    e.ins(VReg(2).s4[lane], e.w0);
    e.b(lane_ok);

    e.L(s2_not_nan);
    // Check src3[lane].
    e.ldr(e.w0, ptr(e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) +
                              32 + lane * 4));
    e.lsl(e.w17, e.w0, 1);
    e.cmp(e.w17, e.w16);
    e.b(LS, use_default);
    e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
    e.ins(VReg(2).s4[lane], e.w0);
    e.b(lane_ok);

    e.L(use_default);
    e.mov(e.w0, 0xFFC00000u);
    e.ins(VReg(2).s4[lane], e.w0);

    e.L(lane_ok);
  }

  e.L(done);
}

// Lazy-spill variant of FixupVmxNan_V128_Fma for the generalized FMA fast path.
// Sources s1/s2 stay REGISTER-resident in s1_vreg/s2_vreg (read via umov); s3
// was spilled to GUEST_SCRATCH+32 and is loaded into v3 ONLY on the NaN path.
// Result in v2 (modified in place). The cheap front-gate skips everything when
// no result lane is NaN (the common case, so no source spill is read). Same PPC
// NaN-priority semantics as FixupVmxNan_V128_Fma (src1>src2>src3, SNaN quieted,
// default 0xFFC00000). Clobbers v3, w0, w16, w17.
inline void FixupVmxNan_V128_Fma_LazySpill(A64Emitter& e, int s1_vreg,
                                           int s2_vreg) {
  using namespace Xbyak_aarch64;
  auto& done = e.NewCachedLabel();

  e.fcmeq(VReg(3).s4, VReg(2).s4, VReg(2).s4);
  e.uminv(SReg(3), VReg(3).s4);
  e.fmov(e.w0, SReg(3));
  e.cbnz(e.w0, done);

  // NaN present: bring s3 in from the stack (front-gate clobbered v3).
  e.ldr(QReg(3),
        Xbyak_aarch64::ptr(
            e.sp, static_cast<int32_t>(StackLayout::GUEST_SCRATCH) + 32));
  e.mov(e.w16, 0xFF000000u);

  for (int lane = 0; lane < 4; lane++) {
    auto& lane_ok = e.NewCachedLabel();
    auto& s1_not_nan = e.NewCachedLabel();
    auto& s2_not_nan = e.NewCachedLabel();
    auto& use_default = e.NewCachedLabel();

    e.umov(e.w0, VReg(2).s4[lane]);
    e.lsl(e.w17, e.w0, 1);
    e.cmp(e.w17, e.w16);
    e.b(LS, lane_ok);

    // src1 (register-resident).
    e.umov(e.w0, VReg(s1_vreg).s4[lane]);
    e.lsl(e.w17, e.w0, 1);
    e.cmp(e.w17, e.w16);
    e.b(LS, s1_not_nan);
    e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
    e.ins(VReg(2).s4[lane], e.w0);
    e.b(lane_ok);

    e.L(s1_not_nan);
    // src2 (register-resident).
    e.umov(e.w0, VReg(s2_vreg).s4[lane]);
    e.lsl(e.w17, e.w0, 1);
    e.cmp(e.w17, e.w16);
    e.b(LS, s2_not_nan);
    e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
    e.ins(VReg(2).s4[lane], e.w0);
    e.b(lane_ok);

    e.L(s2_not_nan);
    // src3 (loaded into v3 from stack).
    e.umov(e.w0, VReg(3).s4[lane]);
    e.lsl(e.w17, e.w0, 1);
    e.cmp(e.w17, e.w16);
    e.b(LS, use_default);
    e.orr(e.w0, e.w0, static_cast<uint64_t>(1u << 22));
    e.ins(VReg(2).s4[lane], e.w0);
    e.b(lane_ok);

    e.L(use_default);
    e.mov(e.w0, 0xFFC00000u);
    e.ins(VReg(2).s4[lane], e.w0);

    e.L(lane_ok);
  }

  e.L(done);
}

// VMX float32x4 binary operations with full PPC semantics.
enum class VmxFpBinOp { Add, Sub, Mul, Div };

// Execute a VMX float32x4 binary operation with denormal flushing and PPC NaN
// propagation.  Result goes into dest_idx.
// Clobbers v0-v3, w0, w16, w17.
template <typename T1, typename T2>
inline void EmitVmxFpBinOp_V128(A64Emitter& e, int dest_idx, const T1& src1,
                                const T2& src2, VmxFpBinOp op) {
  EmitWithVmxFpcr(e, [&] {
    // Flush input denormals → v0=s1, v1=s2.
    int s1, s2;
    PrepareVmxFpSources(e, src1, src2, s1, s2);

    // Hardware FP op → v2.
    switch (op) {
      case VmxFpBinOp::Add:
        e.fadd(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);
        break;
      case VmxFpBinOp::Sub:
        e.fsub(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);
        break;
      case VmxFpBinOp::Mul:
        e.fmul(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);
        break;
      case VmxFpBinOp::Div:
        e.fdiv(VReg(2).s4, VReg(s1).s4, VReg(s2).s4);
        break;
    }

    // PPC NaN propagation fixup (fast-path skip when no NaN).
    FixupVmxNan_V128(e, s1, s2);

    // Flush output denormals. FPCR.FZ guarantees output flushing per the
    // ARM spec, so skip when FZ is known to also handle inputs (implying
    // the core fully supports FZ denormal handling).
    if (!e.IsFeatureEnabled(xe::arm64::kA64FZFlushesInputs)) {
      FlushDenormals_V128(e, 2, 0, 1);
    }

    // Move to dest.
    e.mov(VReg(dest_idx).b16, VReg(2).b16);
  });
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_A64_A64_SEQ_UTIL_H_
