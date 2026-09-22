/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_RENDERDOC_H_
#define XENIA_UI_RENDERDOC_H_

#include <cstdint>

namespace xe {
namespace ui {

// RenderDoc in-application API (third_party/renderdoc/renderdoc_app.h). The
// capture library is only present when the process was started under
// RenderDoc (renderdoccmd capture on the PC, the VK_LAYER_RENDERDOC_Capture
// layer on Android); nothing is loaded otherwise. A frame capture is
// requested by changing the renderdoc_trigger_capture cvar (live, through the
// nop HID trigger file or the debug server), which makes a capture the same
// way the F12 key does, but without the window focus a key needs (2026-09-21).

// True when RenderDoc is loaded in this process.
bool RenderDocIsLoaded();

// Called once per guest swap: when renderdoc_trigger_capture changed since
// the last call, requests a capture of the next frame. Returns true when a
// capture was requested.
bool RenderDocPollTrigger();

// The number of captures RenderDoc has written for this process (0 when
// RenderDoc is not loaded).
uint32_t RenderDocCaptureCount();

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_RENDERDOC_H_
