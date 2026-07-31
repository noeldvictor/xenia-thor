/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xiocompletion.h"

#include "xenia/base/clock.h"
#include "xenia/kernel/guest_scheduler.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xthread.h"

namespace xe {
namespace kernel {

XIOCompletion::XIOCompletion(KernelState* kernel_state)
    : XObject(kernel_state, kObjectType) {
  notification_semaphore_ = threading::Semaphore::Create(0, kMaxNotifications);
  assert_not_null(notification_semaphore_);
}

XIOCompletion::~XIOCompletion() = default;

void XIOCompletion::QueueNotification(IONotification& notification) {
  {
    std::unique_lock<std::mutex> lock(notification_lock_);
    notifications_.push(notification);
    notification_semaphore_->Release(1, nullptr);
  }
  // A fiber parked in WaitForNotification's poll loop only repolls at the
  // scheduler backstop; wake it now so completions deliver promptly.
  if (GuestScheduler::enabled()) {
    auto* scheduler = kernel_state()->guest_scheduler();
    if (scheduler) {
      scheduler->WakeAll();
    }
  }
}

bool XIOCompletion::WaitForNotification(uint64_t wait_ticks,
                                        IONotification* notify) {
  auto ms = std::chrono::milliseconds(TimeoutTicksToMs(wait_ticks));
  if (XThread::GetCurrentFiberThread()) {
    // Fiber-backed guest thread (guest scheduler): a raw blocking wait here
    // would park the whole dispatch host thread and freeze every fiber
    // multiplexed onto it. Poll + cooperative yield instead (same pattern as
    // the XMA waits).
    // Wall-clock deadline: SpinYield on a fiber parks until the next scheduler
    // wake, NOT for 1ms - counting iterations would expire an N-ms timeout
    // after N wakes (a few real ms during IO storms; code-review finding F5).
    const uint64_t deadline_ms =
        Clock::QueryHostUptimeMillis() + uint64_t(ms.count());
    for (;;) {
      auto res = threading::Wait(notification_semaphore_.get(), false,
                                 std::chrono::milliseconds(0));
      if (res == threading::WaitResult::kSuccess) {
        std::unique_lock<std::mutex> lock(notification_lock_);
        assert_false(notifications_.empty());
        std::memcpy(notify, &notifications_.front(), sizeof(IONotification));
        notifications_.pop();
        return true;
      }
      if (Clock::QueryHostUptimeMillis() >= deadline_ms) {
        return false;
      }
      GuestScheduler::SpinYield(std::chrono::milliseconds(1));
    }
  }
  auto res = threading::Wait(notification_semaphore_.get(), false, ms);
  if (res == threading::WaitResult::kSuccess) {
    std::unique_lock<std::mutex> lock(notification_lock_);
    assert_false(notifications_.empty());

    std::memcpy(notify, &notifications_.front(), sizeof(IONotification));
    notifications_.pop();

    return true;
  }

  return false;
}

}  // namespace kernel
}  // namespace xe
