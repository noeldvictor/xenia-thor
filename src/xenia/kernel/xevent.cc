/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xevent.h"

#include "xenia/base/byte_stream.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/guest_scheduler.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/memory.h"

namespace xe {
namespace kernel {

XEvent::XEvent(KernelState* kernel_state)
    : XObject(kernel_state, kObjectType) {}

XEvent::~XEvent() = default;

void XEvent::Initialize(bool manual_reset, bool initial_state) {
  assert_false(event_);

  manual_reset_ = manual_reset;
  CreateNative<X_KEVENT>();
  auto* kevent = memory()->TranslateVirtual<X_KEVENT*>(guest_object());
  // Don't touch header.wait_list: SetNativePointer stashes the handle there.
  kevent->header.type = manual_reset
                            ? X_DISPATCHER_FLAGS::DISPATCHER_MANUAL_RESET_EVENT
                            : X_DISPATCHER_FLAGS::DISPATCHER_AUTO_RESET_EVENT;
  kevent->header.signal_state = initial_state ? 1 : 0;

  if (manual_reset) {
    event_ = xe::threading::Event::CreateManualResetEvent(initial_state);
  } else {
    event_ = xe::threading::Event::CreateAutoResetEvent(initial_state);
  }
  assert_not_null(event_);
}

void XEvent::InitializeNative(void* native_ptr,
                              const X_DISPATCH_HEADER* header) {
  assert_false(event_);

  switch (header->type) {
    case X_DISPATCHER_FLAGS::DISPATCHER_MANUAL_RESET_EVENT:
      manual_reset_ = true;
      break;
    case X_DISPATCHER_FLAGS::DISPATCHER_AUTO_RESET_EVENT:
      manual_reset_ = false;
      break;
    default:
      assert_always();
      return;
  }

  bool initial_state = header->signal_state ? true : false;
  if (manual_reset_) {
    event_ = xe::threading::Event::CreateManualResetEvent(initial_state);
  } else {
    event_ = xe::threading::Event::CreateAutoResetEvent(initial_state);
  }
  assert_not_null(event_);
  SetNativePointer(memory()->HostToGuestVirtual(native_ptr), true);
}

int32_t XEvent::Set(uint32_t priority_increment, bool wait) {
  set_priority_increment(priority_increment);
  event_->Set();
  memory()->TranslateVirtual<X_KEVENT*>(guest_object())->header.signal_state =
      1;
  WakeCooperativeWaiters();
  return 1;
}

int32_t XEvent::Pulse(uint32_t priority_increment, bool wait) {
  set_priority_increment(priority_increment);
  auto* kevent = memory()->TranslateVirtual<X_KEVENT*>(guest_object());
  // KePulseEvent returns the pre-pulse signal state.
  int32_t old_signal_state = kevent->header.signal_state;
  // A parked cooperative waiter re-polls only after a host pulse has already
  // reset the event, so every pulse would be lost. Deliver as a set the first
  // waiter consumes, which for an auto-reset event with a waiter is exactly
  // pulse semantics.
  if (!manual_reset_ && waiters_.HasWaiters()) {
    Set(priority_increment, wait);
    return old_signal_state;
  }
  if (manual_reset_ && waiters_.HasWaiters()) {
    // Satisfy-all-then-reset is not emulated cooperatively, those waiters
    // miss this pulse.
    XELOGW("XEvent::Pulse: manual-reset pulse with parked fiber waiters");
  }
  event_->Pulse();
  // Pulse leaves the event reset after releasing waiters.
  kevent->header.signal_state = 0;
  WakeCooperativeWaiters();
  return old_signal_state;
}

int32_t XEvent::Reset() {
  event_->Reset();
  memory()->TranslateVirtual<X_KEVENT*>(guest_object())->header.signal_state =
      0;
  return 1;
}

void XEvent::WaitCallback() {
  // Auto-reset events atomically clear on successful wait; manual stay set.
  if (!manual_reset_) {
    memory()->TranslateVirtual<X_KEVENT*>(guest_object())->header.signal_state =
        0;
  }
}

void XEvent::CooperativeWaitBegin(XThread* thread) { waiters_.Add(thread); }

void XEvent::CooperativeWaitEnd(XThread* thread) { waiters_.Remove(thread); }
void XEvent::Query(uint32_t* out_type, uint32_t* out_state) {
  auto [type, state] = event_->Query();

  *out_type = type;
  *out_state = state;
}
void XEvent::Clear() { event_->Reset(); }

bool XEvent::Save(ByteStream* stream) {
  XELOGD("XEvent {:08X} ({})", handle(), manual_reset_ ? "manual" : "auto");
  SaveObject(stream);

  bool signaled = true;
  auto result =
      xe::threading::Wait(event_.get(), false, std::chrono::milliseconds(0));
  if (result == xe::threading::WaitResult::kSuccess) {
    signaled = true;
  } else if (result == xe::threading::WaitResult::kTimeout) {
    signaled = false;
  } else {
    assert_always();
  }

  if (signaled) {
    // Reset the event in-case it's an auto-reset.
    event_->Set();
  }

  stream->Write<bool>(signaled);
  stream->Write<bool>(manual_reset_);

  return true;
}

object_ref<XEvent> XEvent::Restore(KernelState* kernel_state,
                                   ByteStream* stream) {
  auto evt = new XEvent(nullptr);
  evt->kernel_state_ = kernel_state;

  evt->RestoreObject(stream);
  bool signaled = stream->Read<bool>();
  evt->manual_reset_ = stream->Read<bool>();

  if (evt->manual_reset_) {
    evt->event_ = xe::threading::Event::CreateManualResetEvent(false);
  } else {
    evt->event_ = xe::threading::Event::CreateAutoResetEvent(false);
  }
  assert_not_null(evt->event_);

  if (signaled) {
    evt->event_->Set();
  }

  return object_ref<XEvent>(evt);
}

}  // namespace kernel
}  // namespace xe
