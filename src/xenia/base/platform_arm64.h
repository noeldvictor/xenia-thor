/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_PLATFORM_ARM64_H_
#define XENIA_BASE_PLATFORM_ARM64_H_
#include <cstdint>

namespace xe {
namespace arm64 {
enum A64FeatureFlags : uint64_t {
  kA64EmitLSE = 1 << 0,
  kA64FZFlushesInputs = 1 << 1,
  kA64EmitFlagM = 1 << 2,
  // Detection-only (no codegen consumer YET) - broadened 2026-06-20 to unblock
  // the LDAPR barrier-weakening lever + expose fcma/dotprod for future heuristic
  // units (see docs/research/20260620-ppc-arm64-nextgen-techniques.md). All
  // present on the AYN Thor per /proc/cpuinfo. Inert until a lowering reads them.
  kA64EmitLRCPC = 1 << 3,    // FEAT_LRCPC   (LDAPR weak-acquire load)
  kA64EmitLRCPC2 = 1 << 4,   // FEAT_LRCPC2  (ldapur/stlur; ilrcpc)
  kA64EmitLSE2 = 1 << 5,     // FEAT_LSE2    (single-op unaligned atomics)
  kA64EmitJSCVT = 1 << 6,    // FEAT_JSCVT   (FJCVTZS)
  kA64EmitFCMA = 1 << 7,     // FEAT_FCMA    (FCADD/FCMLA complex)
  kA64EmitDotProd = 1 << 8,  // FEAT_DotProd (SDOT/UDOT int8; heuristics ONLY)
  // FEAT_SHA3 also brings the three-input BITWISE ops EOR3/BCAX (and
  // RAX1/XAR) - useful well outside crypto: they fuse two NEON bitwise
  // instructions into one, which is what VMX bitwise chains lower to.
  kA64EmitSHA3 = 1 << 9,     // FEAT_SHA3    (EOR3/BCAX/RAX1/XAR)
};

XE_NOALIAS
uint64_t GetFeatureFlags();
XE_COLD
void InitFeatureFlags();

}  // namespace arm64
}  // namespace xe

#endif  // XENIA_BASE_PLATFORM_ARM64_H_
