/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/mutex.h"

#include <atomic>

#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/threading.h"

// The global critical region is xenia's single global lock, acquired on a very
// hot path (kernel HLE, object-table lookups, etc.). NoteOwner() records the
// last owner (thread ids + a QueryHostUptimeMillis timestamp + source) on EVERY
// acquire, purely so the a64 idle-thread-snapshot watchdog can log who holds the
// lock when it detects a stall. Device profiling (BD, 2026-06-23) pinned that
// per-acquire bookkeeping at ~7-10% of CPU (QueryHostUptimeMillis ~7% alone) -
// pure debug instrumentation on the hottest lock in the emulator. Gate it off by
// default; enable it only when actually diagnosing a deadlock/stall.
DEFINE_int32(
    thor_debug_cool, 0,
    "DIAGNOSTIC (device debugging without overheating the Thor): N > 0 caps "
    "the host frame rate at N fps (15 halves the CPU and GPU work of a 30 fps "
    "title; the per-frame GPU time and CPU cost per draw stay valid), makes "
    "guest Sleep(0) streaks sleep 200 us (unless thor_sleep0_backoff_us is "
    "set), keeps the guest and GPU-command threads off the X3 prime core, and "
    "limits the load-time precompile to 2 workers. 0 = off. A Gears launch "
    "took the case from 34 C to 44.8 C in 47 s without it (2026-09-29).",
    "Thor");
DEFINE_bool(global_lock_owner_tracking, false,
            "Record the last global-critical-region owner (thread ids, host "
            "uptime, source) on every acquire so the deadlock/stall watchdog can "
            "report who holds the global lock. Off by default to keep the hot "
            "lock path lean (the bookkeeping was ~7-10% of CPU on device); turn "
            "on only when diagnosing a hang.",
            "CPU");

DEFINE_uint32(
    global_lock_spin, 0,
    "Tries of the global critical region's mutex before a waiter parks in the "
    "kernel (a CPU yield hint between tries). The guest's interrupt-disabled "
    "sections and the kernel exports take this lock; on the device a contended "
    "acquire went straight to a futex sleep. 0 = park at once (unchanged). "
    "Read on every acquire (live).",
    "CPU");

namespace xe {

void global_critical_region::LockSpin(std::recursive_mutex& m) {
  const uint32_t spins = cvars::global_lock_spin;
  for (uint32_t i = 0; i < spins; ++i) {
    if (m.try_lock()) {
      return;
    }
#if XE_ARCH_ARM64
    __asm__ __volatile__("yield" ::: "memory");
#elif XE_ARCH_AMD64 && (defined(__GNUC__) || defined(__clang__))
    __builtin_ia32_pause();
#endif
  }
  m.lock();
}

namespace {

std::atomic<uint32_t> last_global_critical_owner_system_thread_id{0};
std::atomic<uint32_t> last_global_critical_owner_thread_id{0};
std::atomic<uint64_t> last_global_critical_owner_sequence{0};
std::atomic<uint64_t> last_global_critical_owner_host_uptime_ms{0};
std::atomic<const char*> last_global_critical_owner_source{""};

}  // namespace

std::recursive_mutex& global_critical_region::mutex() {
  static std::recursive_mutex global_mutex;
  return global_mutex;
}

void global_critical_region::NoteOwner(const char* source) {
  // Debug-only ownership bookkeeping; skip the per-acquire clock read +
  // thread-id queries + atomic stores on the hot global-lock path unless a
  // hang is actively being diagnosed. The stored owner fields stay at their
  // last value (read only by the stall watchdog's diagnostic log).
  if (!cvars::global_lock_owner_tracking) {
    return;
  }
  last_global_critical_owner_thread_id.store(xe::threading::current_thread_id(),
                                             std::memory_order_relaxed);
  last_global_critical_owner_system_thread_id.store(
      xe::threading::current_thread_system_id(), std::memory_order_relaxed);
  last_global_critical_owner_host_uptime_ms.store(
      xe::Clock::QueryHostUptimeMillis(), std::memory_order_relaxed);
  last_global_critical_owner_source.store(source ? source : "",
                                          std::memory_order_relaxed);
  last_global_critical_owner_sequence.fetch_add(1, std::memory_order_release);
}

uint32_t global_critical_region::last_owner_system_thread_id() {
  return last_global_critical_owner_system_thread_id.load(
      std::memory_order_relaxed);
}

uint32_t global_critical_region::last_owner_thread_id() {
  return last_global_critical_owner_thread_id.load(std::memory_order_relaxed);
}

global_critical_region::DebugSnapshot
global_critical_region::QueryDebugSnapshot() {
  DebugSnapshot snapshot;
  snapshot.last_owner_system_thread_id =
      last_global_critical_owner_system_thread_id.load(
          std::memory_order_relaxed);
  snapshot.last_owner_thread_id =
      last_global_critical_owner_thread_id.load(std::memory_order_relaxed);
  snapshot.last_owner_sequence =
      last_global_critical_owner_sequence.load(std::memory_order_acquire);
  snapshot.last_owner_host_uptime_ms =
      last_global_critical_owner_host_uptime_ms.load(
          std::memory_order_relaxed);
  snapshot.last_owner_source =
      last_global_critical_owner_source.load(std::memory_order_relaxed);
  return snapshot;
}

}  // namespace xe
