// Timer queue check on the POSIX path (the Thor's): firing time, new-timer
// pickup, recurring timers, disarm, many producers, and idle CPU.
// Build: g++ -O2 -std=c++17 -pthread -I shim -I <repo> -DTQ_SOURCE=... test_tq.cc
#include TQ_SOURCE

#include <time.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <random>
#include <thread>
#include <vector>

using clk = std::chrono::steady_clock;
using xe::threading::QueueTimerOnce;
using xe::threading::QueueTimerRecurring;

static double ProcessCpuSeconds() {
  timespec t;
  clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
  return t.tv_sec + t.tv_nsec * 1e-9;
}

static double Us(clk::duration d) {
  return std::chrono::duration<double, std::micro>(d).count();
}

int main() {
  int failures = 0;
  // Let the dispatch thread start.
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // 1. Idle CPU: nothing armed for 1 s.
  {
    double c0 = ProcessCpuSeconds();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    double c1 = ProcessCpuSeconds();
    std::printf("idle: timer thread CPU %.2f%% of a core\n", (c1 - c0) * 100.0);
  }

  // 2. One-shot timers, queued one at a time while the thread sleeps:
  // lateness = fire time - due time.
  {
    std::mt19937 rng(1);
    std::uniform_int_distribution<int> due_us(200, 5000);
    std::vector<double> late;
    int early = 0;
    for (int i = 0; i < 300; ++i) {
      std::atomic<int64_t> fired{0};
      auto due = clk::now() + std::chrono::microseconds(due_us(rng));
      QueueTimerOnce(
          [](void* p) {
            static_cast<std::atomic<int64_t>*>(p)->store(
                clk::now().time_since_epoch().count());
          },
          &fired, due);
      while (!fired.load()) std::this_thread::sleep_for(std::chrono::microseconds(100));
      auto at = clk::time_point(clk::duration(fired.load()));
      if (at < due) ++early;
      late.push_back(Us(at - due));
    }
    std::sort(late.begin(), late.end());
    std::printf("one-shot (300): late median %.0f us, p90 %.0f us, p99 %.0f us, max %.0f us, early %d\n",
                late[late.size() / 2], late[late.size() * 9 / 10], late[late.size() * 99 / 100],
                late.back(), early);
    if (early) ++failures;
  }

  // 3. Recurring 2 ms timer for 400 ms.
  {
    std::atomic<int> count{0};
    auto item = QueueTimerRecurring(
        [](void* p) { static_cast<std::atomic<int>*>(p)->fetch_add(1); }, &count,
        clk::now() + std::chrono::milliseconds(2), std::chrono::milliseconds(2));
    double c0 = ProcessCpuSeconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    double c1 = ProcessCpuSeconds();
    if (auto it = item.lock()) it->Disarm();
    int n = count.load();
    std::printf("recurring 2 ms over 400 ms: %d fires (expect ~200), CPU %.2f%% of a core\n", n,
                (c1 - c0) / 0.4 * 100.0);
    if (n < 190 || n > 202) ++failures;
  }

  // 4. Disarm before due: must not fire.
  {
    std::atomic<int> fired{0};
    auto item = QueueTimerOnce(
        [](void* p) { static_cast<std::atomic<int>*>(p)->store(1); }, &fired,
        clk::now() + std::chrono::milliseconds(30));
    if (auto it = item.lock()) it->Disarm();
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    std::printf("disarm: fired %d (expect 0)\n", fired.load());
    if (fired.load()) ++failures;
  }

  // 5. Eight producers, 2000 timers each, due in 0-3 ms: all fire, none early.
  {
    constexpr int kThreads = 8, kEach = 2000;
    struct Rec {
      clk::time_point due;
      std::atomic<int64_t> at{0};
    };
    std::vector<Rec> recs(kThreads * kEach);
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
      threads.emplace_back([&, t] {
        std::mt19937 rng(100 + t);
        std::uniform_int_distribution<int> due_us(0, 3000);
        for (int i = 0; i < kEach; ++i) {
          Rec& r = recs[t * kEach + i];
          r.due = clk::now() + std::chrono::microseconds(due_us(rng));
          QueueTimerOnce(
              [](void* p) {
                static_cast<Rec*>(p)->at.store(clk::now().time_since_epoch().count());
              },
              &r, r.due);
          if ((i & 63) == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
      });
    }
    for (auto& th : threads) th.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int missing = 0, early = 0;
    std::vector<double> late;
    for (auto& r : recs) {
      int64_t a = r.at.load();
      if (!a) {
        ++missing;
        continue;
      }
      auto at = clk::time_point(clk::duration(a));
      if (at < r.due) ++early;
      late.push_back(Us(at - r.due));
    }
    std::sort(late.begin(), late.end());
    std::printf("8 producers x %d: missing %d, early %d, late median %.0f us, p99 %.0f us\n",
                kEach, missing, early, late.empty() ? 0.0 : late[late.size() / 2],
                late.empty() ? 0.0 : late[late.size() * 99 / 100]);
    if (missing || early) ++failures;
  }

  // 6. Idle again after the load.
  {
    double c0 = ProcessCpuSeconds();
    std::this_thread::sleep_for(std::chrono::seconds(1));
    double c1 = ProcessCpuSeconds();
    std::printf("idle after load: timer thread CPU %.2f%% of a core\n", (c1 - c0) * 100.0);
  }
  std::printf(failures ? "FAIL (%d)\n" : "PASS\n", failures);
  std::fflush(stdout);
  std::_Exit(failures ? 1 : 0);  // the queue's thread never ends
}
