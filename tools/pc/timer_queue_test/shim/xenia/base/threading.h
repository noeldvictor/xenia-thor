#pragma once
#include <chrono>
#include <string>
#include <thread>
// Test shim: the POSIX build's Sleep is nanosleep (threading_posix.cc).
namespace xe::threading {
inline void set_name(const std::string&) {}
inline void Sleep(std::chrono::microseconds duration) {
  std::this_thread::sleep_for(duration);
}
}  // namespace xe::threading
