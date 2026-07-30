/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XTHREAD_H_
#define XENIA_KERNEL_XTHREAD_H_

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/thread.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/kernel/util/native_list.h"
#include "xenia/kernel/xmutant.h"
#include "xenia/kernel/xobject.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {

constexpr fourcc_t kThreadSaveSignature = make_fourcc("THRD");

class XEvent;

constexpr uint32_t X_CREATE_SUSPENDED = 0x00000001;

constexpr uint32_t X_TLS_OUT_OF_INDEXES = UINT32_MAX;

struct XAPC {
  static const uint32_t kSize = 40;
  static const uint32_t kDummyKernelRoutine = 0xF00DFF00;
  static const uint32_t kDummyRundownRoutine = 0xF00DFF01;

  // KAPC is 0x28(40) bytes? (what's passed to ExAllocatePoolWithTag)
  // This is 4b shorter than NT - looks like the reserved dword at +4 is gone.
  // NOTE: stored in guest memory.
  uint8_t type;                      // +0
  uint8_t unk1;                      // +1
  uint8_t processor_mode;            // +2
  uint8_t enqueued;                  // +3
  xe::be<uint32_t> thread_ptr;       // +4
  xe::be<uint32_t> flink;            // +8
  xe::be<uint32_t> blink;            // +12
  xe::be<uint32_t> kernel_routine;   // +16
  xe::be<uint32_t> rundown_routine;  // +20
  xe::be<uint32_t> normal_routine;   // +24
  xe::be<uint32_t> normal_context;   // +28
  xe::be<uint32_t> arg1;             // +32
  xe::be<uint32_t> arg2;             // +36

  void Initialize() {
    type = 18;  // ApcObject
    unk1 = 0;
    processor_mode = 0;
    enqueued = 0;
    thread_ptr = 0;
    flink = blink = 0;
    kernel_routine = 0;
    normal_routine = 0;
    normal_context = 0;
    arg1 = arg2 = 0;
  }
};

// Processor Control Region
struct X_KPCR {
  xe::be<uint32_t> tls_ptr;         // 0x0
  uint8_t unk_04[0x2C];             // 0x4
  xe::be<uint32_t> pcr_ptr;         // 0x30
  uint8_t unk_34[0x3C];             // 0x34
  xe::be<uint32_t> stack_base_ptr;  // 0x70 Stack base address (high addr)
  xe::be<uint32_t> stack_end_ptr;   // 0x74 Stack end (low addr)
  uint8_t unk_78[0x88];             // 0x78
  xe::be<uint32_t> current_thread;  // 0x100
  uint8_t unk_104[0x8];             // 0x104
  uint8_t current_cpu;              // 0x10C
  uint8_t unk_10D[0x43];            // 0x10D
  xe::be<uint32_t> dpc_active;      // 0x150
};

struct X_KTHREAD {
  X_DISPATCH_HEADER header;           // 0x0
  xe::be<uint32_t> unk_10;            // 0x10
  xe::be<uint32_t> unk_14;            // 0x14
  uint8_t unk_18[0x28];               // 0x10
  xe::be<uint32_t> unk_40;            // 0x40
  xe::be<uint32_t> unk_44;            // 0x44
  xe::be<uint32_t> unk_48;            // 0x48
  xe::be<uint32_t> unk_4C;            // 0x4C
  uint8_t unk_50[0x4];                // 0x50
  xe::be<uint16_t> unk_54;            // 0x54
  xe::be<uint16_t> unk_56;            // 0x56
  uint8_t unk_58[0x4];                // 0x58
  xe::be<uint32_t> stack_base;        // 0x5C
  xe::be<uint32_t> stack_limit;       // 0x60
  uint8_t unk_64[0x4];                // 0x64
  xe::be<uint32_t> tls_address;       // 0x68
  uint8_t unk_6C;                     // 0x6C
  uint8_t unk_6D[0x7];                // 0x6D
  xe::be<uint32_t> unk_74;            // 0x74
  xe::be<uint32_t> unk_78;            // 0x78
  xe::be<uint32_t> unk_7C;            // 0x7C
  xe::be<uint32_t> unk_80;            // 0x80
  xe::be<uint32_t> unk_84;            // 0x84
  uint8_t unk_88[0x3];                // 0x88
  uint8_t unk_8B;                     // 0x8B
  uint8_t unk_8C[0x10];               // 0x8C
  xe::be<uint32_t> unk_9C;            // 0x9C
  uint8_t unk_A0[0x10];               // 0xA0
  int32_t apc_disable_count;          // 0xB0
  uint8_t unk_B4[0x8];                // 0xB4
  uint8_t suspend_count;              // 0xBC
  uint8_t unk_BD;                     // 0xBD
  uint8_t unk_BE;                     // 0xBE
  uint8_t current_cpu;                // 0xBF
  uint8_t unk_C0[0x10];               // 0xC0
  xe::be<uint32_t> stack_alloc_base;  // 0xD0
  uint8_t unk_D4[0x5C];               // 0xD4
  xe::be<uint64_t> create_time;       // 0x130
  xe::be<uint64_t> exit_time;         // 0x138
  xe::be<uint32_t> exit_status;       // 0x140
  xe::be<uint32_t> unk_144;           // 0x144
  xe::be<uint32_t> unk_148;           // 0x148
  xe::be<uint32_t> thread_id;         // 0x14C
  xe::be<uint32_t> start_address;     // 0x150
  xe::be<uint32_t> unk_154;           // 0x154
  xe::be<uint32_t> unk_158;           // 0x158
  uint8_t unk_15C[0x4];               // 0x15C
  xe::be<uint32_t> last_error;        // 0x160
  xe::be<uint32_t> fiber_ptr;         // 0x164
  uint8_t unk_168[0x4];               // 0x168
  xe::be<uint32_t> creation_flags;    // 0x16C
  uint8_t unk_170[0xC];               // 0x170
  xe::be<uint32_t> unk_17C;           // 0x17C
  uint8_t unk_180[0x930];             // 0x180

  // This struct is actually quite long... so uh, not filling this out!
};
static_assert_size(X_KTHREAD, 0xAB0);

class XThread : public XObject, public cpu::Thread {
 public:
  static const XObject::Type kObjectType = XObject::Type::Thread;

  static constexpr uint32_t kStackAddressRangeBegin = 0x70000000;
  static constexpr uint32_t kStackAddressRangeEnd = 0x7F000000;

  struct CreationParams {
    uint32_t stack_size;
    uint32_t xapi_thread_startup;
    uint32_t start_address;
    uint32_t start_context;
    uint32_t creation_flags;
  };

  XThread(KernelState* kernel_state);
  XThread(KernelState* kernel_state, uint32_t stack_size,
          uint32_t xapi_thread_startup, uint32_t start_address,
          uint32_t start_context, uint32_t creation_flags, bool guest_thread,
          bool main_thread = false);
  ~XThread() override;

  static bool IsInThread(XThread* other);
  static bool IsInThread();
  static XThread* GetCurrentThread();
  static uint32_t GetCurrentThreadHandle();
  static uint32_t GetCurrentThreadId();

  // Returns the currently-running thread iff it is a scheduler-managed fiber
  // (guest code on the dispatch host thread), otherwise nullptr. Use this to
  // decide whether a blocking call yields cooperatively or blocks the host
  // thread.
  static XThread* GetCurrentFiberThread();
  // Rebinds the per-host-thread TLS (XThread / cpu::Thread / ThreadState) so
  // |thread| becomes the current guest thread on the calling host thread. Used
  // on each fiber switch by the guest scheduler. Pass nullptr to clear.
  static void SetCurrentThread(XThread* thread);

  // True for threads that run a host C++ routine (XHostThread) rather than
  // guest PPC code. These always use a real host thread, never a cooperative
  // fiber, since they run host loops/blocking and other code dereferences
  // their thread().
  virtual bool is_host_thread() const { return false; }

  // True if this thread has a user-mode APC queued (or pending). Used by the
  // cooperative scheduler's alertable waits to return USER_APC, the same way
  // a host alertable wait wakes on a queued APC.
  bool HasPendingUserApc();

  // The fiber this guest thread runs on when the cooperative scheduler is
  // active (null under the host-thread model). Created in the fiber path of
  // Create().
  xe::threading::Fiber* fiber() const { return fiber_.get(); }

  // Drops the self reference from Create and any surviving handle. The delete
  // point for a fiber thread, so the caller must ensure it is not executing.
  void ReclaimExited();

  // The object this thread is registered on as a cooperative waiter, owned by
  // XObject::Enter/LeaveCooperativeWait. Atomic because the waiting fiber
  // clears it just as a terminating thread may be reading it, and either
  // order is fine since a redundant release is a no-op.
  XObject* cooperative_wait_object() const {
    return cooperative_wait_object_.load(std::memory_order_acquire);
  }
  void set_cooperative_wait_object(XObject* object) {
    cooperative_wait_object_.store(object, std::memory_order_release);
  }

  // Intrusive scheduler links, owned exclusively by GuestScheduler and only
  // touched under its lock. Embedding them here keeps the queue operations
  // allocation-free. A thread is in at most one of the ready or blocked
  // lists.
  struct SchedulerLinks {
    XThread* ready_next = nullptr;  // link for the ready OR blocked list
    int cpu = -1;                   // CPU owning the list we are on
    int queued_prio = 0;     // priority level of the ready list we are on
    bool queued = false;     // in the ready list
    bool blocked = false;    // parked in the blocked (waiting) list
    bool suspended = false;  // parked with a nonzero suspend count
    bool running = false;    // executing on a dispatch thread
    bool preempted = false;  // slice cut short by a higher-priority thread
    bool has_run = false;    // diagnostic: dispatched at least once
    // Set once the fiber has exited (master-tree stand-in for Edge's guest
    // KTHREAD thread_state=TERMINATED), guarding zombie revival in MarkReady.
    bool exited = false;
    // Set by an external Terminate, exits the fiber at its next
    // ExitIfTerminated check.
    std::atomic<bool> terminate_pending{false};
    // Absolute raw-tick end of the granted timeslice, 0 = grant fresh at
    // dispatch. Preemption preserves it so the quantum end still arrives.
    uint64_t quantum_deadline_tick = 0;
    // Re-poll gating, written by BlockCurrentThread, read by RereadyBlocked.
    bool wait_gated = false;        // skip re-polls until something below fires
    bool wait_alertable = false;    // also re-poll on a pending user APC
    uint32_t wait_epoch = 0;        // object epoch sampled before the last poll
    uint64_t wait_deadline_ms = 0;  // absolute host uptime, 0 = none
  };
  SchedulerLinks& scheduler_links() { return scheduler_links_; }

  static uint32_t GetLastError();
  static void SetLastError(uint32_t error_code);

  const CreationParams* creation_params() const { return &creation_params_; }
  uint32_t tls_ptr() const { return tls_static_address_; }
  uint32_t pcr_ptr() const { return pcr_address_; }
  // True if the thread is created by the guest app.
  bool is_guest_thread() const { return guest_thread_; }
  bool main_thread() const { return main_thread_; }
  bool is_running() const { return running_; }

  uint32_t thread_id() const { return thread_id_; }
  uint32_t stack_base() const { return stack_base_; }    // high address
  uint32_t stack_limit() const { return stack_limit_; }  // low address
  uint32_t last_error();
  void set_last_error(uint32_t error_code);
  void set_name(const std::string_view name);

  X_STATUS Create();
  X_STATUS Exit(int exit_code);
  X_STATUS Terminate(int exit_code);

  virtual void Execute();

  virtual void Reenter(uint32_t address);

  void EnterCriticalRegion();
  void LeaveCriticalRegion();
  uint32_t RaiseIrql(uint32_t new_irql);
  void LowerIrql(uint32_t new_irql);

  void CheckApcs();
  void LockApc();
  void UnlockApc(bool queue_delivery);
  util::NativeList* apc_list() { return &apc_list_; }
  void NoteApcQueued();
  void NoteApcDequeued();
  volatile uint32_t* a64_apc_pending_count_ptr() {
    return reinterpret_cast<volatile uint32_t*>(&apc_pending_count_);
  }
  void EnqueueApc(uint32_t normal_routine, uint32_t normal_context,
                  uint32_t arg1, uint32_t arg2);

  int32_t priority() const { return priority_; }
  int32_t QueryPriority();
  void SetPriority(int32_t increment);

  // Xbox thread IDs:
  // 0 - core 0, thread 0 - user
  // 1 - core 0, thread 1 - user
  // 2 - core 1, thread 0 - sometimes xcontent
  // 3 - core 1, thread 1 - user
  // 4 - core 2, thread 0 - xaudio
  // 5 - core 2, thread 1 - user
  void SetAffinity(uint32_t affinity);
  uint8_t active_cpu() const;
  void SetActiveCpu(uint8_t cpu_index);

  bool GetTLSValue(uint32_t slot, uint32_t* value_out);
  bool SetTLSValue(uint32_t slot, uint32_t value);

  uint32_t suspend_count();
  X_STATUS Resume(uint32_t* out_suspend_count = nullptr);
  X_STATUS Suspend(uint32_t* out_suspend_count = nullptr);
  X_STATUS Delay(uint32_t processor_mode, uint32_t alertable,
                 uint64_t interval);

  xe::threading::Thread* thread() { return thread_.get(); }

  virtual bool Save(ByteStream* stream) override;
  static object_ref<XThread> Restore(KernelState* kernel_state,
                                     ByteStream* stream);

  // Internal - do not use.
  void AcquireMutantOnStartup(object_ref<XMutant> mutant) {
    pending_mutant_acquires_.push_back(mutant);
  }

  // Host-side list of mutants this thread currently owns (cooperative
  // scheduler mode only) - the master-tree stand-in for Edge's guest
  // KTHREAD.mutants_list, consumed by XMutant::AbandonAllOwnedByThread at
  // thread exit. Insert/Remove run on the owning thread; the sweep may run
  // from another (terminating) host thread, hence the mutex.
  void AddOwnedMutant(XMutant* mutant) {
    std::lock_guard<std::mutex> lock(owned_mutants_mutex_);
    owned_mutants_.push_back(mutant);
  }
  void RemoveOwnedMutant(XMutant* mutant) {
    std::lock_guard<std::mutex> lock(owned_mutants_mutex_);
    auto it = std::find(owned_mutants_.begin(), owned_mutants_.end(), mutant);
    if (it != owned_mutants_.end()) {
      owned_mutants_.erase(it);
    }
  }
  std::vector<XMutant*> TakeOwnedMutants() {
    std::lock_guard<std::mutex> lock(owned_mutants_mutex_);
    return std::move(owned_mutants_);
  }

 protected:
  bool AllocateStack(uint32_t size);
  void FreeStack();
  void InitializeGuestObject();

  void DeliverAPCs();
  void RundownAPCs();

  xe::threading::WaitHandle* GetWaitHandle() override {
    // Under the cooperative scheduler there is no host thread, so a
    // fiber-backed thread exposes an event signaled on exit for other threads
    // to wait on.
    if (thread_) {
      return thread_.get();
    }
    return fiber_exit_event_.get();
  }

  // When the cooperative scheduler is active, the guest thread runs on this
  // fiber instead of its own host thread (cpu::Thread::thread_).
  std::unique_ptr<xe::threading::Fiber> fiber_;
  SchedulerLinks scheduler_links_;
  // Set by the first ReclaimExited so both terminal paths reclaim once.
  std::atomic<bool> self_reference_dropped_{false};
  // Owned by XObject::Enter/LeaveCooperativeWait.
  std::atomic<XObject*> cooperative_wait_object_{nullptr};
  // Signaled when a fiber-backed thread exits, so waits on the thread object
  // resolve (the host thread handle that normally serves this role is
  // absent).
  std::unique_ptr<xe::threading::Event> fiber_exit_event_;

  CreationParams creation_params_ = {0};

  std::vector<object_ref<XMutant>> pending_mutant_acquires_;
  std::mutex owned_mutants_mutex_;
  std::vector<XMutant*> owned_mutants_;

  uint32_t thread_id_ = 0;
  uint32_t scratch_address_ = 0;
  uint32_t scratch_size_ = 0;
  uint32_t tls_static_address_ = 0;
  uint32_t tls_dynamic_address_ = 0;
  uint32_t tls_total_size_ = 0;
  uint32_t pcr_address_ = 0;
  uint32_t stack_alloc_base_ = 0;  // Stack alloc base
  uint32_t stack_alloc_size_ = 0;  // Stack alloc size
  uint32_t stack_base_ = 0;        // High address
  uint32_t stack_limit_ = 0;       // Low address
  bool guest_thread_ = false;
  bool main_thread_ = false;  // Entry-point thread
  bool running_ = false;

  int32_t priority_ = 0;

  xe::global_critical_region global_critical_region_;
  std::atomic<uint32_t> irql_ = {0};
  std::atomic<uint32_t> apc_pending_count_ = {0};
  util::NativeList apc_list_;
};

class XHostThread : public XThread {
 public:
  XHostThread(KernelState* kernel_state, uint32_t stack_size,
              uint32_t creation_flags, std::function<int()> host_fn);

  // Host threads always run on a real host thread, never a cooperative
  // fiber - they run host loops/blocking code.
  bool is_host_thread() const override { return true; }

  virtual void Execute();

 private:
  std::function<int()> host_fn_;
};

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_XTHREAD_H_
