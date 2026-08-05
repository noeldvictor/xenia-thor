/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/cvar.h"
#include "xenia/base/platform.h"
#include "xenia/base/logging.h"
#include "xenia/base/platform_arm64.h"

#include <cstdio>
#include <fstream>
#include <string>

#if XE_ARCH_ARM64
#include <cfenv>
#include <cmath>
#include <cstring>

#define XBYAK_NO_OP_NAMES
#include "third_party/xbyak_aarch64/xbyak_aarch64/xbyak_aarch64.h"
#include "third_party/xbyak_aarch64/xbyak_aarch64/xbyak_aarch64_util.h"

#if defined(__linux__)
#include <sys/auxv.h>
#endif
#endif

DEFINE_int32(a64_extension_mask, -1,
             "Allow the detection and utilization of specific instruction set "
             "features.\n"
             "    0 = armv8.0\n"
             "    1 = Large System Extensions(LSE) atomic operations\n"
             "    2 = FPCR.FZ flushes denormal inputs (skip software flush)\n"
             "    4 = FEAT_FlagM (rmif+adc carry fastpath)\n"
             "   -1 = Detect and utilize all possible processor features\n",
             "a64");
namespace xe {
namespace arm64 {
void InitFeatureFlags();

static uint64_t g_feature_flags = 0U;
static bool g_did_initialize_feature_flags = false;
uint64_t GetFeatureFlags() {
  if (!g_did_initialize_feature_flags) {
    InitFeatureFlags();
  }
  return g_feature_flags;
}
XE_COLD
XE_NOINLINE
void InitFeatureFlags() {
#if XE_ARCH_ARM64
  uint64_t feature_flags_ = 0U;
  {
    Xbyak_aarch64::util::Cpu cpu_;
#define TEST_EMIT_FEATURE(emit, ext)                \
  if ((cvars::a64_extension_mask & emit) == emit) { \
    feature_flags_ |= (cpu_.has(ext) ? emit : 0);   \
  }
    TEST_EMIT_FEATURE(kA64EmitLSE,
                      Xbyak_aarch64::util::XBYAK_AARCH64_HWCAP_ATOMIC);
#undef TEST_EMIT_FEATURE
  }

  // Detect FEAT_FlagM (RMIF/SETF/CFINV). The vendored xbyak_aarch64 util has no
  // FlagM HWCAP bit, so probe the kernel auxv directly. Linux/Android only; on
  // other ARM64 hosts FlagM stays off and the a64 backend uses the equivalent
  // non-FlagM path.
  if ((cvars::a64_extension_mask & kA64EmitFlagM) == kA64EmitFlagM) {
#if defined(__linux__)
#ifndef HWCAP_FLAGM
#define HWCAP_FLAGM (1 << 23)
#endif
    if (getauxval(AT_HWCAP) & HWCAP_FLAGM) {
      feature_flags_ |= kA64EmitFlagM;
    }
#endif
  }

  // Detection-only broadening (no codegen consumer yet; inert until a lowering
  // reads these). Correct arm64 HWCAP bits per asm/hwcap.h. Each gated by the
  // a64_extension_mask so it can be force-disabled. Unblocks the LDAPR lever +
  // exposes fcma/dotprod for future heuristic units.
#if defined(__linux__)
  {
    const unsigned long hwcap = getauxval(AT_HWCAP);
    const struct {
      uint64_t flag;
      unsigned long bit;
    } probes[] = {
        {kA64EmitJSCVT, 1UL << 13},    // HWCAP_JSCVT
        {kA64EmitFCMA, 1UL << 14},     // HWCAP_FCMA
        {kA64EmitLRCPC, 1UL << 15},    // HWCAP_LRCPC
        {kA64EmitSHA3, 1UL << 17},     // HWCAP_SHA3 (EOR3/BCAX)
        {kA64EmitDotProd, 1UL << 20},  // HWCAP_ASIMDDP
        {kA64EmitLSE2, 1UL << 25},     // HWCAP_USCAT (FEAT_LSE2)
        {kA64EmitLRCPC2, 1UL << 26},   // HWCAP_ILRCPC
    };
    for (const auto& p : probes) {
      if ((uint64_t(cvars::a64_extension_mask) & p.flag) == p.flag &&
          (hwcap & p.bit)) {
        feature_flags_ |= p.flag;
      }
    }
  }
#endif

  // Detect whether FPCR.FZ flushes denormal float32 inputs to zero.
  // The ARM spec says input flushing is implementation-defined.
  // Modern cores (Cortex-A76+, Apple M1+) flush inputs; older ones may not.
  if ((cvars::a64_extension_mask & kA64FZFlushesInputs) ==
      kA64FZFlushesInputs) {
    // Build a denormal float32: smallest positive denormal = 0x00000001.
    uint32_t denorm_bits = 1;
    float denorm;
    std::memcpy(&denorm, &denorm_bits, 4);

    // Save FPCR, enable FZ, add two denormals, check result.
    uint64_t saved_fpcr;
#if XE_COMPILER_MSVC
    saved_fpcr = _ReadStatusReg(ARM64_FPCR);
    _WriteStatusReg(ARM64_FPCR, saved_fpcr | (1ULL << 24));
#else
    asm volatile("mrs %0, fpcr" : "=r"(saved_fpcr));
    uint64_t fz_fpcr = saved_fpcr | (1ULL << 24);
    asm volatile("msr fpcr, %0" ::"r"(fz_fpcr));
#endif

    volatile float a = denorm;
    volatile float b = denorm;
    volatile float result = a + b;

#if XE_COMPILER_MSVC
    _WriteStatusReg(ARM64_FPCR, saved_fpcr);
#else
    asm volatile("msr fpcr, %0" ::"r"(saved_fpcr));
#endif

    if (result == 0.0f) {
      feature_flags_ |= kA64FZFlushesInputs;
    }
  }

  g_feature_flags = feature_flags_;
#else
  g_feature_flags = 0U;
#endif
  g_did_initialize_feature_flags = true;
}

const CoreClasses& GetCoreClasses() {
  static const CoreClasses classes = []() -> CoreClasses {
    CoreClasses out;
#if XE_ARCH_ARM64 && defined(__linux__)
    for (uint32_t cpu = 0; cpu < 64; ++cpu) {
      char path[128];
      std::snprintf(path, sizeof(path),
                    "/sys/devices/system/cpu/cpu%u/regs/identification/midr_el1",
                    cpu);
      std::ifstream file(path);
      if (!file.is_open()) {
        continue;
      }
      std::string text;
      if (!std::getline(file, text) || text.empty()) {
        continue;
      }
      uint64_t midr = 0;
      try {
        midr = std::stoull(text, nullptr, 16);
      } catch (...) {
        continue;
      }
      // MIDR_EL1: implementer in [31:24], part number in [15:4].
      const uint64_t implementer = (midr >> 24) & 0xFF;
      const uint64_t part = (midr >> 4) & 0xFFF;
      if (implementer != 0x41) {  // 0x41 = ARM Ltd; only ARM parts are classified
        continue;
      }
      const uint64_t bit = uint64_t(1) << cpu;
      switch (part) {
        case 0xD4E:  // Cortex-X3 (the Thor's prime, cpu7)
        case 0xD44:  // Cortex-X1
          out.prime |= bit;
          break;
        case 0xD4D:  // Cortex-A715 (the Thor's cpu3-4 - the STRONGER mid pair)
          out.perf |= bit;
          break;
        case 0xD47:  // Cortex-A710 (the Thor's cpu5-6)
        case 0xD0B:  // Cortex-A76
          out.legacy |= bit;
          break;
        case 0xD46:  // Cortex-A510 (the Thor's cpu0-2)
        case 0xD03:  // Cortex-A53
          out.little |= bit;
          break;
        default:
          continue;
      }
      ++out.detected;
    }
    if (out.detected) {
      XELOGI(
          "ARM64 core classes from MIDR_EL1: prime=0x{:X} perf=0x{:X} "
          "legacy=0x{:X} little=0x{:X} ({} cores identified)",
          out.prime, out.perf, out.legacy, out.little, out.detected);
    }
#endif
    return out;
  }();
  return classes;
}

}  // namespace arm64
}  // namespace xe
