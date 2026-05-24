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
#include "xenia/base/threading.h"

namespace xe {

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
