/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Link stubs for the cpu-tests binary on EVERY architecture: the load-time
// D3D-HLE handlers in processor.cc reach into gpu::CommandProcessor to write
// the PM4 ring directly, but the cpu-tests project links neither xenia-gpu (x64
// host) nor the gpu library at all (lean ARM64 qemu cross build). Without these
// no-op definitions the test binary fails to link (LNK2019 UpdateWritePointer).
// Safe: the HLE handlers are cvar-gated and no CommandProcessor is ever
// constructed in the tests. The real app links xenia-gpu and gets the real one.

#include "xenia/gpu/command_processor.h"

namespace xe {
namespace gpu {

void CommandProcessor::UpdateWritePointer(uint32_t value) {}

}  // namespace gpu
}  // namespace xe
