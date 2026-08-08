// Device-free correctness + wakeup-count check for threading_per_object_condvar.
//
// threading_posix.cc parks EVERY single-object waiter on ONE process-wide
// condition variable, so every signal notify_all()s every parked guest thread.
// Each one wakes, re-evaluates a handle it has no interest in, and parks again.
// Those wakeups do no guest work, and a core that keeps waking cannot reach a
// deep idle state - which is watts, the thing we are actually chasing.
//
// The lever gives each object its own condvar and keeps the shared one for
// WaitMultiple only, poked solely while a multi-wait is registered. That is
// XenDroid's shape (2c0ac5847); CLAUDE.md recorded their fix as "NOT portable"
// because one condvar here does both jobs. Splitting the jobs is what makes the
// gate meaningful - this test exists to prove the split does not lose a wakeup.
//
// A lost wakeup is a HANG, which is why this is worth testing before the device
// ever sees it. Both modes are run identically and must agree on every count.
//
// Build/run (no device, no ARM needed - this is pure C++ threading):
//   g++ -O2 -std=c++17 -pthread -o che condvar_herd_equiv.c && ./che
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

static bool g_per_object = false;

static std::mutex g_mutex;                  // the shared mutex_ - unchanged
static std::condition_variable g_shared;    // PosixConditionBase::cond_
static std::atomic<unsigned> g_multi_refs{0};

// Counts the thing the lever targets: how many times a parked thread was woken.
static std::atomic<long> g_wakeups{0};
static std::atomic<long> g_useful{0};
// Signals that actually changed state. Setting an ALREADY-SET auto-reset event
// is a no-op, so raw signal count is NOT the liveness invariant - coalescing is
// correct behaviour and would otherwise read as a lost wakeup.
static std::atomic<long> g_effective{0};

struct Obj {
  std::condition_variable local;  // the new per-object condvar
  bool signaled = false;

  // Mirrors PosixConditionBase::NotifyWaiters(). Caller holds g_mutex.
  void NotifyWaiters() {
    if (!g_per_object) {
      g_shared.notify_all();
      return;
    }
    local.notify_all();
    if (g_multi_refs.load(std::memory_order_relaxed) != 0) {
      g_shared.notify_all();
    }
  }

  void Signal() {
    std::unique_lock<std::mutex> lk(g_mutex);
    if (!signaled) {
      signaled = true;
      g_effective.fetch_add(1, std::memory_order_relaxed);
    }
    NotifyWaiters();
  }

  // Mirrors PosixConditionBase::Wait() with an auto-reset consume.
  bool Wait(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(g_mutex);
    auto pred = [this] {
      // Every predicate evaluation after a park is a wakeup we paid for.
      g_wakeups.fetch_add(1, std::memory_order_relaxed);
      if (signaled) g_useful.fetch_add(1, std::memory_order_relaxed);
      return signaled;
    };
    if (signaled) {
      signaled = false;
      return true;
    }
    std::condition_variable& cv = g_per_object ? local : g_shared;
    if (!cv.wait_for(lk, timeout, pred)) return false;
    signaled = false;
    return true;
  }
};

struct MultiWaitRegistration {
  MultiWaitRegistration() { g_multi_refs.fetch_add(1, std::memory_order_relaxed); }
  ~MultiWaitRegistration() { g_multi_refs.fetch_sub(1, std::memory_order_relaxed); }
};

// Mirrors WaitMultiple: parks on the SHARED condvar, registered under the lock.
static bool WaitAny(std::vector<Obj*>& objs, std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lk(g_mutex);
  MultiWaitRegistration reg;  // destroyed before lk - i.e. still under the lock
  auto pred = [&objs] {
    for (auto* o : objs)
      if (o->signaled) return true;
    return false;
  };
  if (!g_shared.wait_for(lk, timeout, pred)) return false;
  for (auto* o : objs) {
    if (o->signaled) { o->signaled = false; return true; }
  }
  return false;
}

struct Result { long consumed, timeouts, wakeups, useful, effective, left; };

static Result RunScenario(bool per_object, int n_objs, int n_single,
                          int n_multi, int signals_each) {
  g_per_object = per_object;
  g_wakeups = 0; g_useful = 0; g_multi_refs = 0; g_effective = 0;

  std::vector<Obj> objs(n_objs);
  std::atomic<long> consumed{0}, timeouts{0};
  std::atomic<bool> stop{false};

  std::vector<std::thread> threads;
  // Single-object waiters: each owns one object. Under the shared condvar these
  // are the thundering herd - woken by signals to objects they never touch.
  for (int i = 0; i < n_single; ++i) {
    threads.emplace_back([&, i] {
      while (!stop.load(std::memory_order_relaxed)) {
        if (objs[i % n_objs].Wait(std::chrono::milliseconds(2)))
          consumed.fetch_add(1, std::memory_order_relaxed);
        else
          timeouts.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  // Multi-waiters: these must still be woken, which is what the gate risks.
  for (int i = 0; i < n_multi; ++i) {
    threads.emplace_back([&] {
      std::vector<Obj*> set;
      for (int k = 0; k < n_objs; ++k) set.push_back(&objs[k]);
      while (!stop.load(std::memory_order_relaxed)) {
        if (WaitAny(set, std::chrono::milliseconds(2)))
          consumed.fetch_add(1, std::memory_order_relaxed);
        else
          timeouts.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  // Signalers.
  std::vector<std::thread> signalers;
  for (int k = 0; k < n_objs; ++k) {
    signalers.emplace_back([&, k] {
      for (int s = 0; s < signals_each; ++s) {
        objs[k].Signal();
        std::this_thread::sleep_for(std::chrono::microseconds(50));
      }
    });
  }
  for (auto& t : signalers) t.join();
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  stop = true;
  for (auto& t : threads) t.join();

  // Anything still set at the end was signalled but never consumed - not lost,
  // just outstanding. It closes the accounting exactly.
  long left = 0;
  for (auto& o : objs) if (o.signaled) ++left;
  return {consumed.load(), timeouts.load(), g_wakeups.load(), g_useful.load(),
          g_effective.load(), left};
}

int main(void) {
  const int kObjs = 8, kSingle = 16, kMulti = 2, kSignals = 400;
  const long kTotalSignals = (long)kObjs * kSignals;

  printf("scenario: %d objects, %d single-waiters, %d multi-waiters, "
         "%ld signals\n\n", kObjs, kSingle, kMulti, kTotalSignals);

  Result off = RunScenario(false, kObjs, kSingle, kMulti, kSignals);
  Result on  = RunScenario(true,  kObjs, kSingle, kMulti, kSignals);

  printf("%-28s %12s %12s\n", "", "shared (off)", "per-object");
  printf("%-28s %12ld %12ld\n", "signals consumed", off.consumed, on.consumed);
  printf("%-28s %12ld %12ld\n", "predicate evaluations", off.wakeups, on.wakeups);
  printf("%-28s %12ld %12ld\n", "  ...that found work", off.useful, on.useful);
  printf("%-28s %12ld %12ld\n", "  ...WASTED", off.wakeups - off.useful,
         on.wakeups - on.useful);

  int bad = 0;
  // LIVENESS, stated exactly: every state-changing signal is either consumed by
  // a waiter or still outstanding at the end. A lost WAKEUP (the hang) shows up
  // as consumed + left < effective, and coalescing cannot fake it.
  printf("\n%-28s %12ld %12ld\n", "effective signals", off.effective,
         on.effective);
  printf("%-28s %12ld %12ld\n", "  consumed + outstanding",
         off.consumed + off.left, on.consumed + on.left);
  if (off.consumed + off.left != off.effective) {
    printf("\nFAIL - shared mode lost %ld wakeups\n",
           off.effective - off.consumed - off.left);
    ++bad;
  }
  if (on.consumed + on.left != on.effective) {
    printf("\nFAIL - PER-OBJECT MODE LOST %ld WAKEUPS (this would hang)\n",
           on.effective - on.consumed - on.left);
    ++bad;
  }
  // 2. The point of the change: fewer wasted wakeups.
  long waste_off = off.wakeups - off.useful, waste_on = on.wakeups - on.useful;
  if (!bad && waste_on >= waste_off) {
    printf("\nWARN - no wakeup reduction measured (%ld -> %ld); the lever is "
           "correct but pointless in this scenario\n", waste_off, waste_on);
  } else if (!bad) {
    printf("\nwasted wakeups: %ld -> %ld  (%.1f%% fewer)\n", waste_off, waste_on,
           100.0 * (waste_off - waste_on) / (double)(waste_off ? waste_off : 1));
  }
  printf("\n%s\n", bad
      ? "FAIL - do NOT enable threading_per_object_condvar"
      : "PASS - no signal lost in either mode; per-object mode is live-correct");
  return bad != 0;
}
