/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PRECOMPILE_STATUS_H_
#define XENIA_CPU_PRECOMPILE_STATUS_H_

#include <atomic>
#include <cstdint>

namespace xe {
namespace cpu {

// Process-wide state of the load-window AOT precompile, written by the
// XexModule precompile workers and read by the app UI through JNI. This
// replaces parsing the "AOT precompile progress" log line out of logcat, which
// the app cannot do reliably. Every field is a relaxed atomic; the UI polls.
struct PrecompileStatus {
  enum State : uint32_t { kIdle = 0, kRunning = 1, kDone = 2 };
  std::atomic<uint32_t> state{kIdle};
  // Functions compiled in the current pass and the current frontier estimate.
  std::atomic<uint32_t> done{0};
  std::atomic<uint32_t> frontier{0};
  std::atomic<uint32_t> workers{0};
  // Number of passes started in this process. A title loads several XEX
  // modules and each runs its own pass.
  std::atomic<uint32_t> pass{0};
  // Milliseconds spent in the current or last pass.
  std::atomic<uint64_t> elapsed_ms{0};
  // Functions compiled by all passes so far.
  std::atomic<uint64_t> total_done{0};
  // Thermal governor (2026-09-20): workers allowed to run right now, the
  // hottest CPU or GPU thermal zone in whole degrees C, and 1 while the
  // governor holds workers back. The app shows these on the compile overlay.
  std::atomic<uint32_t> workers_active{0};
  std::atomic<int32_t> temp_c{-1};
  std::atomic<int32_t> case_c{-1};
  std::atomic<uint32_t> throttled{0};
};

inline PrecompileStatus& GetPrecompileStatus() {
  static PrecompileStatus status;
  return status;
}

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PRECOMPILE_STATUS_H_
