/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_THOR_TOPOLOGY_H_
#define XENIA_BASE_THOR_TOPOLOGY_H_

#include <cstdint>

#include "xenia/base/platform.h"

namespace xe {

// AYN Thor (Snapdragon 8 Gen 2 / QCS8550 "kalama") CPU core topology - the
// single source of truth so the GPU-command-thread pin (thor_gpu_thread_affinity_cpu),
// the guest-thread mask (thor_guest_thread_affinity_mask), and the parallel-JIT
// worker pool all agree on which host cores are big/little:
//   cpu7    = Cortex-X3 prime @3.19GHz
//   cpu3-6  = 2x A715 + 2x A710 @2.8GHz (the mid / "worker" cluster)
//   cpu0-2  = 3x A510 @2.0GHz (little)
struct ThorTopology {
  static constexpr int kPrimeCore = 7;            // Cortex-X3
  static constexpr uint64_t kBigMask = 0xF8;      // cpu3-7 (X3 + A715x2 + A710x2)
  static constexpr uint64_t kWorkerMask = 0x78;   // cpu3-6 (leave X3 for the guest hot path)
  static constexpr uint64_t kLittleMask = 0x07;   // cpu0-2 (A510)

  // True on the Thor's build target (Android ARM64). The topology constants are
  // only ever applied when a thor_* affinity cvar is explicitly enabled, so this
  // is a build-target guard rather than a strict per-SoC probe.
  static constexpr bool IsThorBuild() {
#if XE_ARCH_ARM64 && XE_PLATFORM_ANDROID
    return true;
#else
    return false;
#endif
  }
  static constexpr int PrimeCore() { return kPrimeCore; }
  static constexpr uint64_t BigCoreMask() { return kBigMask; }
  static constexpr uint64_t WorkerCoreMask() { return kWorkerMask; }
  static constexpr uint64_t LittleCoreMask() { return kLittleMask; }
};

}  // namespace xe

#endif  // XENIA_BASE_THOR_TOPOLOGY_H_
