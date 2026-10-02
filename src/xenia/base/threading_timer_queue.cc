/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <condition_variable>
#include <forward_list>
#include <mutex>

#include "third_party/disruptorplus/include/disruptorplus/multi_threaded_claim_strategy.hpp"
#include "third_party/disruptorplus/include/disruptorplus/ring_buffer.hpp"
#include "third_party/disruptorplus/include/disruptorplus/sequence_barrier.hpp"
#include "third_party/disruptorplus/include/disruptorplus/spin_wait.hpp"
#include "third_party/disruptorplus/include/disruptorplus/spin_wait_strategy.hpp"

#include "xenia/base/assert.h"
#include "xenia/base/cvar.h"
#include "xenia/base/threading.h"
#include "xenia/base/threading_timer_queue.h"

DEFINE_bool(
    timer_queue_sleep_idle, XE_ANDROID_DEFAULT(true, false),
    "Thor CPU: make the timer-dispatch thread SLEEP until the next timer "
    "deadline instead of busy-spinning. The disruptor spin_wait_strategy polls "
    "the clock continuously between timer events (device-profiled as the top "
    "__kernel_clock_gettime cost on Blue Dragon - a core burned for nothing, "
    "the same spinning-worker pathology as the XMA decoder). Existing timers "
    "still fire on time (the thread sleeps only until the soonest armed due "
    "time); a timer queued while it sleeps wakes it at once (2026-10-01: an "
    "event wait - the earlier 1 ms poll spun the disruptor's 4092 pauses "
    "on every wake, a full PC core in the Gears profile and 0.47% of the "
    "Blue Dragon frame on the Thor). Device-validated on the BD heavy field (matched "
    "A/B vs the lock-fix baseline): renders+runs correctly, TimerThreadMain "
    "2.63%->1.79% (-0.84pp of the frame; the spin's wait_until_published "
    "2.49%->1.48%). Default-OFF (it shifts newly-queued-timer pickup by up to "
    "the cap, a timing change; flip on after multi-title validation).",
    "CPU");
DEFINE_int32(
    timer_queue_idle_sleep_us, 100000,
    "When timer_queue_sleep_idle is on and no timer is armed, the timer thread "
    "sleeps at most this many microseconds before it checks again. A newly "
    "queued timer wakes it at once and an armed timer at its due time, so this "
    "is only a safety bound.",
    "CPU");

namespace dp = disruptorplus;

namespace xe {
namespace threading {

using WaitItem = TimerQueueWaitItem;

class TimerQueue {
 public:
  using clock = WaitItem::clock;
  static_assert(clock::is_steady);

 public:
  TimerQueue()
      : buffer_(kWaitCount),
        wait_strategy_(),
        claim_strategy_(kWaitCount, wait_strategy_),
        consumed_(wait_strategy_),
        shutdown_(false) {
    claim_strategy_.add_claim_barrier(consumed_);
    dispatch_thread_ = std::thread(&TimerQueue::TimerThreadMain, this);
  }

  ~TimerQueue() {
    shutdown_.store(true, std::memory_order_release);

    // Kick dispatch thread to check shutdown flag
    auto wait_item = std::make_shared<WaitItem>(nullptr, nullptr, this,
                                                clock::time_point::min(),
                                                clock::duration::zero());
    wait_item->Disarm();
    QueueTimer(std::move(wait_item));

    dispatch_thread_.join();
  }

  void TimerThreadMain() {
    dp::sequence_t next_sequence = 0;
    const auto comp = [](const std::shared_ptr<WaitItem>& left,
                         const std::shared_ptr<WaitItem>& right) {
      return left->due_ < right->due_;
    };

    xe::threading::set_name("xe::threading::TimerQueue");

    while (!shutdown_.load(std::memory_order_relaxed)) {
      {
        // Consume new wait items and add them to sorted wait queue.
        // When timer_queue_sleep_idle is on, drain non-blocking (timeout=now)
        // and sleep at the loop tail instead of letting the spin_wait_strategy
        // busy-poll the clock until the deadline.
        // The disruptor's timed wait spins 4092 pauses before it reads the
        // clock, so with timer_queue_sleep_idle it runs only when QueueTimer
        // counted a new item (queued_).
        dp::sequence_t available = next_sequence - 1;
        if (!cvars::timer_queue_sleep_idle) {
          available = claim_strategy_.wait_until_published(
              next_sequence, next_sequence - 1,
              wait_queue_.empty() ? clock::time_point::max()
                                  : wait_queue_.front()->due_);
        } else if (queued_.load(std::memory_order_acquire) > consumed_count_) {
          available = claim_strategy_.wait_until_published(
              next_sequence, next_sequence - 1, clock::now());
        }

        // Check for timeout
        if (available != next_sequence - 1) {
          std::forward_list<std::shared_ptr<WaitItem>> wait_items;
          do {
            wait_items.push_front(std::move(buffer_[next_sequence]));
            ++consumed_count_;
          } while (next_sequence++ != available);

          consumed_.publish(available);

          wait_items.sort(comp);
          wait_queue_.merge(wait_items, comp);
        }
      }

      {
        // Check wait queue, invoke callbacks and reschedule
        std::forward_list<std::shared_ptr<WaitItem>> wait_items;
        while (!wait_queue_.empty() &&
               wait_queue_.front()->due_ <= clock::now()) {
          auto wait_item = std::move(wait_queue_.front());
          wait_queue_.pop_front();

          // Ensure that it isn't disarmed
          auto state = WaitItem::State::kIdle;
          if (wait_item->state_.compare_exchange_strong(
                  state, WaitItem::State::kInCallback,
                  std::memory_order_acq_rel)) {
            // Possibility to dispatch to a thread pool here
            assert_not_null(wait_item->callback_);
            wait_item->callback_(wait_item->userdata_);

            if (wait_item->interval_ != clock::duration::zero() &&
                wait_item->state_.load(std::memory_order_acquire) !=
                    WaitItem::State::kInCallbackSelfDisarmed) {
              // Item is recurring and didn't self-disarm during callback:
              wait_item->due_ += wait_item->interval_;
              wait_item->state_.store(WaitItem::State::kIdle,
                                      std::memory_order_release);
              wait_items.push_front(std::move(wait_item));
            } else {
              wait_item->state_.store(WaitItem::State::kDisarmed,
                                      std::memory_order_release);
            }
          } else {
            // Specifically, kInCallback is illegal here
            assert_true(WaitItem::State::kDisarmed == state);
          }
        }
        wait_items.sort(comp);
        wait_queue_.merge(wait_items, comp);
      }

      // Sleep instead of busy-spinning the dispatch thread (cvar-gated) until
      // the soonest armed timer is due or QueueTimer adds one (wake_cv_), so
      // armed timers fire on time and a new one is picked up at once. The
      // earlier version polled every 1 ms, and each wake ran the disruptor's
      // 4092-pause spin: a full core on the PC, where a sleep under 1 ms is a
      // yield (Gears 2026-10-01).
      if (cvars::timer_queue_sleep_idle &&
          !shutdown_.load(std::memory_order_relaxed)) {
        const auto now = clock::now();
        const auto cap = std::chrono::microseconds(
            std::max(1, cvars::timer_queue_idle_sleep_us));
        const clock::time_point sleep_until =
            wait_queue_.empty()
                ? now + cap
                : (std::min)(wait_queue_.front()->due_, now + cap);
        if (sleep_until > now) {
          std::unique_lock<std::mutex> lock(wake_mutex_);
          wake_cv_.wait_until(lock, sleep_until, [this] {
            return queued_.load(std::memory_order_acquire) > consumed_count_ ||
                   shutdown_.load(std::memory_order_relaxed);
          });
        }
      }
    }
  }

  std::weak_ptr<WaitItem> QueueTimer(std::shared_ptr<WaitItem> wait_item) {
    auto wait_item_weak = std::weak_ptr<WaitItem>(wait_item);

    // Mitigate callback flooding
    wait_item->due_ =
        std::max(clock::now() - wait_item->interval_, wait_item->due_);

    auto sequence = claim_strategy_.claim_one();
    buffer_[sequence] = std::move(wait_item);
    claim_strategy_.publish(sequence);

    // Wake the dispatch thread if it sleeps (timer_queue_sleep_idle). Taking
    // the mutex orders this with its predicate check, so the wake is not lost.
    queued_.fetch_add(1, std::memory_order_release);
    if (cvars::timer_queue_sleep_idle) {
      { std::lock_guard<std::mutex> lock(wake_mutex_); }
      wake_cv_.notify_one();
    }

    return wait_item_weak;
  }

  const std::thread& dispatch_thread() const { return dispatch_thread_; }

 private:
  // This ring buffer will be used to introduce timers queued by the public API
  static constexpr size_t kWaitCount = 512;
  dp::ring_buffer<std::shared_ptr<WaitItem>> buffer_;
  dp::spin_wait_strategy wait_strategy_;
  dp::multi_threaded_claim_strategy<dp::spin_wait_strategy> claim_strategy_;
  dp::sequence_barrier<dp::spin_wait_strategy> consumed_;

  // This is a _sorted_ (ascending due_) list of active timers managed by a
  // dedicated thread
  std::forward_list<std::shared_ptr<WaitItem>> wait_queue_;
  // Items QueueTimer published and items the dispatch thread consumed: with
  // timer_queue_sleep_idle the dispatch thread checks the ring only when
  // they differ, and sleeps on wake_cv_ otherwise.
  std::atomic<uint64_t> queued_{0};
  uint64_t consumed_count_ = 0;
  std::mutex wake_mutex_;
  std::condition_variable wake_cv_;
  std::atomic_bool shutdown_;
  std::thread dispatch_thread_;
};

xe::threading::TimerQueue timer_queue_;

void TimerQueueWaitItem::Disarm() {
  State state;

  // Special case for calling from a callback itself
  if (std::this_thread::get_id() == parent_queue_->dispatch_thread().get_id()) {
    state = State::kInCallback;
    if (state_.compare_exchange_strong(state, State::kInCallbackSelfDisarmed,
                                       std::memory_order_acq_rel)) {
      // If we are self disarming from the callback set this special state and
      // exit
      return;
    }
    // Normal case can handle the rest
  }

  dp::spin_wait spinner;
  state = State::kIdle;
  // Classes which hold WaitItems will often call Disarm() to cancel them during
  // destruction. This may lead to race conditions when the dispatch thread
  // executes a callback which accesses memory that is freed simultaneously due
  // to this. Therefore, we need to guarantee that no callbacks will be running
  // once Disarm() has returned.
  while (!state_.compare_exchange_weak(state, State::kDisarmed,
                                       std::memory_order_acq_rel)) {
    if (state == State::kDisarmed) {
      // Do not break for kInCallbackSelfDisarmed and keep spinning in order to
      // meet guarantees
      break;
    }
    state = State::kIdle;
    spinner.spin_once();
  }
}

std::weak_ptr<WaitItem> QueueTimerOnce(std::function<void(void*)> callback,
                                       void* userdata,
                                       WaitItem::clock::time_point due) {
  return timer_queue_.QueueTimer(
      std::make_shared<WaitItem>(std::move(callback), userdata, &timer_queue_,
                                 due, WaitItem::clock::duration::zero()));
}

std::weak_ptr<WaitItem> QueueTimerRecurring(
    std::function<void(void*)> callback, void* userdata,
    WaitItem::clock::time_point due, WaitItem::clock::duration interval) {
  return timer_queue_.QueueTimer(std::make_shared<WaitItem>(
      std::move(callback), userdata, &timer_queue_, due, interval));
}

}  // namespace threading
}  // namespace xe
