// Runs src/xenia/base/testing/threading_test.cc (the POSIX threading layer:
// events, semaphores, mutants, timers, WaitMultiple, threads) for ARM64 under
// qemu-aarch64, in either wakeup mode:
//   ./threading_tests                 threading_per_object_condvar = false
//   ./threading_tests --per-object    threading_per_object_condvar = true
// A lost wakeup in the per-object mode shows as a hung or failed test here,
// not as a hung game on the Thor.
//
// Build (links the arm64 libraries of the Android NativeCore build; OBJ is its
// obj/local/arm64-v8a directory under android/.../intermediates/cxx):
//   $NDK/clang++ --target=aarch64-linux-android29 -std=c++17 -O1 -static \
//       -I . -I src -I third_party/fmt/include -DFMT_HEADER_ONLY \
//       -o scratch/threading_tests tools/qemu/threading_test_main.cc \
//       src/xenia/base/testing/threading_test.cc \
//       $OBJ/libxenia-base.a $OBJ/libfmt.a -static-libstdc++
//   qemu-aarch64 scratch/threading_tests "~[timer] ~HighResolutionTimer"
//   qemu-aarch64 scratch/threading_tests --per-object "~[timer] ~HighResolutionTimer"
// The [timer] tests crash under qemu-user in both modes (POSIX interval
// timers); everything else must pass. 2026-09-23: 24 cases, 259 assertions,
// 30 of 30 repeats in each mode.
#define CATCH_CONFIG_RUNNER
#include <cstdio>
#include <cstring>
#include <vector>

#include "third_party/catch/single_include/catch2/catch.hpp"
#include "xenia/base/cvar.h"

DECLARE_bool(threading_per_object_condvar);

// The static test binary has no liblog; xenia-base's Android logging lands
// here instead.
extern "C" int __android_log_write(int, const char*, const char* text) {
  std::fprintf(stderr, "%s\n", text ? text : "");
  return 0;
}
extern "C" int __android_log_print(int, const char*, const char* fmt, ...) {
  return 0;
}

int main(int argc, char** argv) {
  bool per_object = false;
  std::vector<char*> args;
  for (int i = 0; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--per-object")) {
      per_object = true;
    } else {
      args.push_back(argv[i]);
    }
  }
  cvars::threading_per_object_condvar = per_object;
  std::printf("threading_per_object_condvar=%d\n", per_object ? 1 : 0);
  return Catch::Session().run(int(args.size()), args.data());
}
