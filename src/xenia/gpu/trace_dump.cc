/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/trace_dump.h"

#include <algorithm>
#include <chrono>

#include "third_party/stb/stb_image_write.h"
#include "xenia/base/filesystem.h"
#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/base/string.h"
#include "xenia/base/threading.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/memory.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/xbox.h"

// The stb_image_write implementation lives in stb_image_write_impl.cc, so the
// trace viewer can write a PNG without pulling this file (and its
// target_trace_file cvar) into the same link (2026-09-21).
#undef _CRT_SECURE_NO_WARNINGS
#undef _CRT_NONSTDC_NO_DEPRECATE
#include "third_party/stb/stb_image_write.h"

DEFINE_path(target_trace_file, "", "Specifies the trace file to load.", "GPU");
DEFINE_path(trace_dump_path, "", "Output path for dumped files.", "GPU");
DEFINE_int32(trace_dump_frame, 0,
             "The frame of the trace to render: an index, or -1 for the last "
             "frame (a streamed trace of a title that stopped swapping ends on "
             "the frame that stays on the panel).",
             "GPU");
DEFINE_int32(trace_dump_bench_iterations, 0,
             "After the capture, play the frame this many times more with warm "
             "caches and log the wall time of each (\"Trace bench:\"): the "
             "command processor's CPU cost of a frame without the device "
             "(tools/pc/trace_bench.py). With "
             "vulkan_trace_draw_outcomes_per_frame, each replay also logs the "
             "per-draw CPU buckets.",
             "GPU");

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

TraceDump::TraceDump() = default;

TraceDump::~TraceDump() = default;

int TraceDump::Main(const std::vector<std::string>& args) {
  // Grab path from the flag or unnamed argument.
  std::filesystem::path path;
  std::filesystem::path output_path;
  if (!cvars::target_trace_file.empty()) {
    // Passed as a named argument.
    // TODO(benvanik): find something better than gflags that supports
    // unicode.
    path = cvars::target_trace_file;
  } else if (args.size() >= 2) {
    // Passed as an unnamed argument.
    path = xe::to_path(args[1]);

    if (args.size() >= 3) {
      output_path = xe::to_path(args[2]);
    }
  }

  if (path.empty()) {
    XELOGE("No trace file specified");
    return 5;
  }

  // Normalize the path and make absolute.
  auto abs_path = std::filesystem::absolute(path);
  XELOGI("Loading trace file {}...", xe::path_to_utf8(abs_path));

  if (!Setup()) {
    XELOGE("Unable to setup trace dump tool");
    return 4;
  }
  if (!Load(std::move(abs_path))) {
    XELOGE("Unable to load trace file; not found?");
    return 5;
  }

  // Root file name for outputs.
  if (output_path.empty()) {
    base_output_path_ = cvars::trace_dump_path;
    auto output_name = path.filename().replace_extension();

    base_output_path_ = base_output_path_ / output_name;
  } else {
    base_output_path_ = output_path;
  }

  // Ensure output path exists.
  xe::filesystem::CreateParentFolder(base_output_path_);

  return Run();
}

bool TraceDump::Setup() {
  // Create the emulator but don't initialize so we can setup the window.
  emulator_ = std::make_unique<Emulator>("", "", "", "");
  // The guest output capture below needs a presenter; there is no window.
  emulator_->set_offscreen_presentation(true);
  X_STATUS result = emulator_->Setup(
      nullptr, nullptr, false, nullptr,
      [this]() { return CreateGraphicsSystem(); }, nullptr);
  if (XFAILED(result)) {
    XELOGE("Failed to setup emulator: {:08X}", result);
    return false;
  }
  graphics_system_ = emulator_->graphics_system();
  player_ = std::make_unique<TracePlayer>(graphics_system_);
  return true;
}

bool TraceDump::Load(const std::filesystem::path& trace_file_path) {
  trace_file_path_ = trace_file_path;

  if (!player_->Open(xe::path_to_utf8(trace_file_path_))) {
    XELOGE("Could not load trace file");
    return false;
  }

  return true;
}

int TraceDump::Run() {
  BeginHostCapture();
  int frame_count = player_->frame_count();
  int frame = cvars::trace_dump_frame < 0 ? frame_count - 1 : cvars::trace_dump_frame;
  frame = std::max(0, std::min(frame, frame_count - 1));
  XELOGI("Trace dump: rendering frame {} of {}", frame, frame_count);
  player_->SeekFrame(frame);
  player_->SeekCommand(
      static_cast<int>(player_->current_frame()->commands.size() - 1));
  player_->WaitOnPlayback();
  EndHostCapture();

  // Capture.
  int result = 0;
  ui::Presenter* presenter = graphics_system_->presenter();
  ui::RawImage raw_image;
  if (presenter && presenter->CaptureGuestOutput(raw_image)) {
    // Save framebuffer png.
    auto png_path = base_output_path_.replace_extension(".png");
    auto handle = filesystem::OpenFile(png_path, "wb");
    auto callback = [](void* context, void* data, int size) {
      fwrite(data, 1, size, (FILE*)context);
    };
    stbi_write_png_to_func(callback, handle, static_cast<int>(raw_image.width),
                           static_cast<int>(raw_image.height), 4,
                           raw_image.data.data(),
                           static_cast<int>(raw_image.stride));
    fclose(handle);
  } else {
    result = 1;
  }

  player_->set_skip_unchanged_memory(true);
  for (int32_t i = 0; i < cvars::trace_dump_bench_iterations; ++i) {
    auto bench_start = std::chrono::steady_clock::now();
    player_->ReplayCurrentFrame();
    player_->WaitOnPlayback();
    XELOGI("Trace bench: replay {} {} us", i,
           std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now() - bench_start)
               .count());
  }

  player_.reset();
  emulator_.reset();
  return result;
}

}  //  namespace gpu
}  //  namespace xe
