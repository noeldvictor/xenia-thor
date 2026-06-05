/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/platform.h"

// The lean ARM64 (qemu) cpu-test build links xenia-cpu but NOT xenia-kernel
// (which pulls GUI deps that are painful to cross-compile). xenia-cpu's
// xex_module.cc references a few xenia-kernel symbols; the CPU tests never load
// XEX modules, so these are never actually called - provide trivial definitions
// here so the lean a64 test links. This whole translation unit is empty on
// every non-ARM64 build (the x86_64 host build links the real xenia-kernel).
#if XE_ARCH_ARM64

#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xmodule.h"

namespace xe {
namespace kernel {

bool KernelState::IsKernelModule(const std::string_view name) { return false; }

object_ref<XModule> KernelState::GetModule(const std::string_view name,
                                           bool user_only) {
  return object_ref<XModule>();
}

void XObject::Release() {}

}  // namespace kernel
}  // namespace xe

#endif  // XE_ARCH_ARM64
