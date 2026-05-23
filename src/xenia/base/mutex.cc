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

#include "xenia/base/threading.h"

namespace xe {

namespace {

std::atomic<uint32_t> last_global_critical_owner_system_thread_id{0};
std::atomic<uint32_t> last_global_critical_owner_thread_id{0};

}  // namespace

std::recursive_mutex& global_critical_region::mutex() {
  static std::recursive_mutex global_mutex;
  return global_mutex;
}

void global_critical_region::NoteOwner() {
  last_global_critical_owner_thread_id.store(xe::threading::current_thread_id(),
                                             std::memory_order_relaxed);
  last_global_critical_owner_system_thread_id.store(
      xe::threading::current_thread_system_id(), std::memory_order_relaxed);
}

uint32_t global_critical_region::last_owner_system_thread_id() {
  return last_global_critical_owner_system_thread_id.load(
      std::memory_order_relaxed);
}

uint32_t global_critical_region::last_owner_thread_id() {
  return last_global_critical_owner_thread_id.load(std::memory_order_relaxed);
}

}  // namespace xe
