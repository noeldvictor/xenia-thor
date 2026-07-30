/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XMUTANT_H_
#define XENIA_KERNEL_XMUTANT_H_

#include <atomic>

#include "xenia/base/threading.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
class XThread;

// Dual-mode mutant (guest scheduler stage 1, ported from xenia-edge with a
// master-tree adaptation):
// - Legacy mode (guest_scheduler off, the default): backed by a host Mutant,
//   exactly the long-proven behavior of this tree.
// - Cooperative mode (guest_scheduler on): backed by a free-signal SEMAPHORE
//   plus XThread-level ownership. A host mutant's owner is the host THREAD,
//   which many guest fibers share and can migrate between, so it cannot
//   represent guest ownership under the cooperative scheduler (a co-resident
//   fiber would alias the owner's recursion). Edge tracks the owner list in
//   the guest KTHREAD/KMUTANT structs; master's structs lack those fields, so
//   ownership lists live host-side on the XThread instead.
class XMutant : public XObject {
 public:
  static const XObject::Type kObjectType = XObject::Type::Mutant;

  explicit XMutant(KernelState* kernel_state);
  ~XMutant() override;

  void Initialize(bool initial_owner);
  void InitializeNative(void* native_ptr, X_DISPATCH_HEADER* header);

  X_STATUS ReleaseMutant(uint32_t priority_increment, bool abandon, bool wait);

  bool Save(ByteStream* stream) override;
  static object_ref<XMutant> Restore(KernelState* kernel_state,
                                     ByteStream* stream);

  // Marks every mutant owned by |thread| abandoned and frees it. Called from
  // XThread::Exit/Terminate when the cooperative scheduler is active (a
  // fiber-backed thread has no host-thread exit to abandon primitives for
  // it). No-op in legacy mode.
  static void AbandonAllOwnedByThread(KernelState* kernel_state,
                                      XThread* thread);

  bool IsReenteredByCurrentThread() override;
  X_STATUS AcquireStatus() override;

  void CooperativeWaitBegin(XThread* thread) override;
  void CooperativeWaitEnd(XThread* thread) override;
  bool CooperativeMayAcquire(XThread* thread) override;

 protected:
  xe::threading::WaitHandle* GetWaitHandle() override {
    return free_signal_
               ? static_cast<xe::threading::WaitHandle*>(free_signal_.get())
               : static_cast<xe::threading::WaitHandle*>(mutant_.get());
  }
  void WaitCallback() override;

 private:
  XMutant();

  // Legacy mode.
  std::unique_ptr<xe::threading::Mutant> mutant_;

  // Cooperative mode: signaled while unowned. A count rather than a host
  // mutant, whose owner is the host thread, which many guest threads share
  // and can migrate between.
  std::unique_ptr<xe::threading::Semaphore> free_signal_;
  // The only source of truth for ownership (both modes track it; legacy mode
  // only informationally, as before).
  std::atomic<XThread*> owning_thread_{nullptr};
  // Recursive acquires never touch free_signal_, so count them here. Only the
  // current owner mutates it, so no synchronization.
  uint32_t recursion_count_ = 0;
  // Set when the owner exited without releasing; the next acquirer sees
  // X_STATUS_ABANDONED_WAIT_0 (host-side stand-in for KMUTANT.abandoned).
  std::atomic<bool> abandoned_{false};
  // Parked fibers waiting to acquire, in order. Without this a running fiber
  // that re-acquires in a loop starves a parked waiter forever, where NT
  // hands a released mutant to the waiter.
  CooperativeWaiterFifo waiters_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XMUTANT_H_
