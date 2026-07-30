/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xmutant.h"

#include "xenia/base/byte_stream.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/guest_scheduler.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xthread.h"

namespace xe {
namespace kernel {

XMutant::XMutant(KernelState* kernel_state)
    : XObject(kernel_state, kObjectType) {}

XMutant::XMutant() : XObject(kObjectType) {}

XMutant::~XMutant() = default;

void XMutant::Initialize(bool initial_owner) {
  assert_false(mutant_);
  assert_false(free_signal_);

  if (GuestScheduler::enabled()) {
    // Cooperative mode: free-signal semaphore + XThread-level ownership (see
    // the class comment). Counts 1 while unowned, 0 while held.
    free_signal_ = xe::threading::Semaphore::Create(initial_owner ? 0 : 1, 1);
    assert_not_null(free_signal_);
    if (initial_owner) {
      // Initial-owner acquire doesn't go through Wait, so record it here.
      XThread* self =
          XThread::IsInThread() ? XThread::GetCurrentThread() : nullptr;
      if (self) {
        owning_thread_ = self;
        recursion_count_ = 1;
        self->AddOwnedMutant(this);
      }
    }
    return;
  }

  mutant_ = xe::threading::Mutant::Create(initial_owner);
  assert_not_null(mutant_);
}

void XMutant::InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header) {
  assert_false(mutant_);
  assert_false(free_signal_);

  // Haven't seen this yet, but it's possible.
  assert_always();
}

X_STATUS XMutant::ReleaseMutant(uint32_t priority_increment, bool abandon,
                                bool wait) {
  // TODO(benvanik): abandoning.
  assert_false(abandon);

  if (free_signal_) {
    // Guest ownership decides, not the host primitive, so a release from the
    // wrong guest thread is rejected even when both share a dispatch thread.
    XThread* self =
        XThread::IsInThread() ? XThread::GetCurrentThread() : nullptr;
    if (!self || owning_thread_.load() != self) {
      return X_STATUS_MUTANT_NOT_OWNED;
    }
    --recursion_count_;
    if (recursion_count_ == 0) {
      // Clear ownership before signaling, or a waiter that acquires on
      // another host thread would be stranded with a null owning_thread_.
      owning_thread_ = nullptr;
      self->RemoveOwnedMutant(this);
      free_signal_->Release(1, nullptr);
      WakeCooperativeWaiters();
    }
    return X_STATUS_SUCCESS;
  }

  // Legacy mode.
  // Call should succeed if we own the mutant, so go ahead and do this.
  if (owning_thread_.load() == XThread::GetCurrentThread()) {
    owning_thread_ = nullptr;
  }
  if (mutant_->Release()) {
    return X_STATUS_SUCCESS;
  } else {
    return X_STATUS_MUTANT_NOT_OWNED;
  }
}

bool XMutant::Save(ByteStream* stream) {
  if (!SaveObject(stream)) {
    return false;
  }

  XThread* owner = owning_thread_.load();
  uint32_t owning_thread_handle = owner ? owner->handle() : 0;
  stream->Write<uint32_t>(owning_thread_handle);
  XELOGD("XMutant {:08X} (owner: {:08X})", handle(), owning_thread_handle);

  return true;
}

object_ref<XMutant> XMutant::Restore(KernelState* kernel_state,
                                     ByteStream* stream) {
  auto mutant = new XMutant();
  mutant->kernel_state_ = kernel_state;

  if (!mutant->RestoreObject(stream)) {
    delete mutant;
    return nullptr;
  }

  mutant->Initialize(false);

  auto owning_thread_handle = stream->Read<uint32_t>();
  if (owning_thread_handle) {
    // Do NOT pre-set owning_thread_ in cooperative mode: the queued Wait at
    // thread start must hit the first-acquire branch in WaitCallback.
    auto owner_thread = kernel_state->object_table()->LookupObject<XThread>(
        owning_thread_handle);
    if (!GuestScheduler::enabled()) {
      mutant->owning_thread_ = owner_thread.get();
    }
    owner_thread->AcquireMutantOnStartup(retain_object(mutant));
  }

  return object_ref<XMutant>(mutant);
}

void XMutant::AbandonAllOwnedByThread(KernelState* kernel_state,
                                      XThread* thread) {
  if (!GuestScheduler::enabled()) {
    // Legacy mode: the host OS abandons host mutants at host-thread exit,
    // exactly as before this port.
    return;
  }
  // Released here rather than left to the OS, since a fiber-backed thread has
  // no host-thread exit to abandon the primitive for it.
  for (XMutant* mutant : thread->TakeOwnedMutants()) {
    XThread* expected = thread;
    if (mutant->owning_thread_.compare_exchange_strong(expected, nullptr)) {
      // Only the thread that held it releases, however deep its recursion.
      mutant->recursion_count_ = 0;
      mutant->abandoned_ = true;
      mutant->free_signal_->Release(1, nullptr);
      mutant->WakeCooperativeWaiters();
    }
  }
}

bool XMutant::IsReenteredByCurrentThread() {
  if (!free_signal_) {
    // Legacy host mutants handle recursion themselves.
    return false;
  }
  // GetCurrentThread asserts on the host threads that wait on a guest mutant
  // during teardown, so gate on IsInThread like the wait path does.
  if (!XThread::IsInThread()) {
    return false;
  }
  XThread* owner = owning_thread_.load();
  return owner && owner == XThread::GetCurrentThread();
}

X_STATUS XMutant::AcquireStatus() {
  if (free_signal_ && abandoned_.exchange(false)) {
    return X_STATUS_ABANDONED_WAIT_0;
  }
  return X_STATUS_SUCCESS;
}

void XMutant::WaitCallback() {
  XThread* self = XThread::IsInThread() ? XThread::GetCurrentThread() : nullptr;
  if (!free_signal_) {
    owning_thread_ = self;
    return;
  }
  if (!self) {
    return;
  }
  XThread* prev = owning_thread_.exchange(self);
  if (prev != self) {
    recursion_count_ = 1;
    self->AddOwnedMutant(this);
  } else {
    ++recursion_count_;
  }
}

void XMutant::CooperativeWaitBegin(XThread* thread) { waiters_.Add(thread); }

void XMutant::CooperativeWaitEnd(XThread* thread) {
  // Poke the new front so it re-polls now.
  if (waiters_.Remove(thread)) {
    WakeCooperativeWaiters();
  }
}

bool XMutant::CooperativeMayAcquire(XThread* thread) {
  // The owner bypasses the queue so a recursive acquire cannot self-deadlock
  // behind its own waiters.
  return owning_thread_.load() == thread || waiters_.MayAcquire(thread);
}

}  // namespace kernel
}  // namespace xe
