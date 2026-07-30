/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <memory>
#include <utility>

#include "xenia/base/assert.h"
#include "xenia/base/platform_win.h"
#include "xenia/base/threading.h"

namespace xe {
namespace threading {

// Windows implementation of xe::threading::Fiber on the native fiber API
// (ConvertThreadToFiberEx / CreateFiberEx / SwitchToFiber). Functionally
// equivalent to xenia-edge's Boost.Context implementation for the cooperative
// guest scheduler's purposes; chosen so the desktop build needs no
// third-party assembly. The Android/POSIX build uses the fcontext-based
// implementation instead (threading_fiber_posix.cc, staged separately).

class WinFiber;

// The fiber currently executing on this host thread (set on every switch and
// on thread adoption). nullptr on threads that never hosted fibers.
thread_local WinFiber* current_fiber_ = nullptr;

class WinFiber : public Fiber {
 public:
  // Suspended fiber with its own stack running |start_routine| on first
  // switch.
  WinFiber(size_t stack_size, std::function<void()> start_routine)
      : start_routine_(std::move(start_routine)) {
    fiber_handle_ = CreateFiberEx(
        stack_size, stack_size, FIBER_FLAG_FLOAT_SWITCH, FiberEntryThunk, this);
    assert_not_null(fiber_handle_);
  }

  // Adoption of the calling host thread's stack as a fiber.
  explicit WinFiber(void* converted_thread_fiber)
      : fiber_handle_(converted_thread_fiber), is_thread_fiber_(true) {}

  ~WinFiber() override {
    if (is_thread_fiber_) {
      // Un-adopt the host thread (only valid on the owning thread; the guest
      // scheduler destroys its dispatch-thread fiber on that thread during
      // teardown).
      if (current_fiber_ == this) {
        ConvertFiberToThread();
        current_fiber_ = nullptr;
      }
    } else if (fiber_handle_) {
      // Deleting the currently-running fiber is ExitThread per the API
      // contract - the scheduler must only destroy parked or terminated
      // fibers from another fiber's context.
      assert_true(current_fiber_ != this);
      DeleteFiber(fiber_handle_);
    }
  }

  void SwitchTo() override {
    assert_not_null(fiber_handle_);
    assert_false(terminated_);
    WinFiber* previous = current_fiber_;
    // The target starts (or resumes) with current_fiber_ already naming it -
    // GetCurrentFiber() is valid immediately inside the entry thunk.
    current_fiber_ = this;
    SwitchToFiber(fiber_handle_);
    // Control returned to |previous|'s context.
    current_fiber_ = previous;
  }

  void SetTerminated() override { terminated_ = true; }

 private:
  static void CALLBACK FiberEntryThunk(void* parameter) {
    WinFiber* self = static_cast<WinFiber*>(parameter);
    self->start_routine_();
    // A fiber's entry routine must never return (returning exits the host
    // thread per the Windows fiber contract). The start routine is expected
    // to SetTerminated() + switch away as its last act (the guest scheduler's
    // fiber trampoline does); reaching here is a scheduler bug.
    assert_always(
        "Fiber start routine returned instead of switching away after "
        "SetTerminated()");
    ExitThread(1);
  }

  void* fiber_handle_ = nullptr;
  std::function<void()> start_routine_;
  bool is_thread_fiber_ = false;
  bool terminated_ = false;

  friend class Fiber;
};

std::unique_ptr<Fiber> Fiber::Create(CreationParameters params,
                                     std::function<void()> start_routine) {
  auto fiber =
      std::make_unique<WinFiber>(params.stack_size, std::move(start_routine));
  return fiber;
}

std::unique_ptr<Fiber> Fiber::CreateFromThread() {
  void* thread_fiber = ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
  if (!thread_fiber) {
    // Already a fiber (ERROR_ALREADY_FIBER) - adopt the current fiber handle.
    thread_fiber = ::GetCurrentFiber();
    assert_not_null(thread_fiber);
  }
  auto fiber = std::make_unique<WinFiber>(thread_fiber);
  current_fiber_ = fiber.get();
  return fiber;
}

Fiber* Fiber::GetCurrentFiber() { return current_fiber_; }

bool Fiber::IsStackOverflowFault(const void* address) {
  // The native fiber API owns the stacks; their guard pages are not visible
  // here. Windows raises a distinct stack-overflow exception anyway, so the
  // crash handler does not need this signal on this platform.
  return false;
}

}  // namespace threading
}  // namespace xe
