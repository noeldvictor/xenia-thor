/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/renderdoc.h"

#include <memory>
#include <mutex>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/ui/renderdoc_api.h"

DEFINE_int32(
    renderdoc_trigger_capture, 0,
    "RenderDoc: a frame capture is requested every time this value changes "
    "while the process runs under RenderDoc (renderdoccmd capture on the PC, "
    "the RenderDoc layer on Android). Set it live through the nop HID trigger "
    "file (cvar:renderdoc_trigger_capture=1) or the debug server. "
    "gpu_bd_renderdoc_capture_frame captures at a fixed swap number instead.",
    "GPU");
DEFINE_string(
    renderdoc_capture_path, "",
    "RenderDoc: the capture file path template (RenderDoc appends the frame "
    "number and .rdc). Empty keeps the template renderdoccmd set.",
    "GPU");

namespace xe {
namespace ui {

namespace {

std::unique_ptr<RenderDocAPI> renderdoc_api;
std::once_flag renderdoc_load_once;
int32_t renderdoc_last_trigger = 0;

const RENDERDOC_API_1_0_0* GetApi() {
  std::call_once(renderdoc_load_once, []() {
    renderdoc_api = RenderDocAPI::CreateIfConnected();
    if (renderdoc_api && !cvars::renderdoc_capture_path.empty()) {
      renderdoc_api->api_1_0_0()->SetLogFilePathTemplate(
          cvars::renderdoc_capture_path.c_str());
    }
  });
  return renderdoc_api ? renderdoc_api->api_1_0_0() : nullptr;
}

}  // namespace

bool RenderDocIsLoaded() { return GetApi() != nullptr; }

bool RenderDocPollTrigger() {
  const int32_t trigger = cvars::renderdoc_trigger_capture;
  if (trigger == renderdoc_last_trigger) {
    return false;
  }
  renderdoc_last_trigger = trigger;
  const RENDERDOC_API_1_0_0* api = GetApi();
  if (!api) {
    XELOGW("RenderDoc: capture requested but RenderDoc is not loaded");
    return false;
  }
  api->TriggerCapture();
  XELOGI("RenderDoc: capture of the next frame requested (trigger {})",
         trigger);
  return true;
}

uint32_t RenderDocCaptureCount() {
  const RENDERDOC_API_1_0_0* api = GetApi();
  return api ? api->GetNumCaptures() : 0;
}

}  // namespace ui
}  // namespace xe
