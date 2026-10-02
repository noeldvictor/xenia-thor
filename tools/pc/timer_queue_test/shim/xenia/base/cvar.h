#pragma once
#include <cstdint>
// Test shim: the timer queue's cvars as plain globals (Android defaults).
#define XE_ANDROID_DEFAULT(android_value, other_value) (android_value)
#define DEFINE_bool(name, def, desc, cat) \
  namespace cvars {                       \
  bool name = def;                        \
  }
#define DEFINE_int32(name, def, desc, cat) \
  namespace cvars {                        \
  int32_t name = def;                      \
  }
