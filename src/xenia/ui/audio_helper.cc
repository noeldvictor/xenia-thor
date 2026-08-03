/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/audio_helper.h"

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"

// NOTE(kernel-port 2026-08): xenia-edge plays the achievement chime through a
// vendored miniaudio engine. miniaudio was deliberately NOT vendored for this
// port (see docs/research/20260731-edge-kernel-port-plan.md - we only took
// pugixml), so this helper is a no-op: achievement notifications still appear
// on screen, just silently. Re-enable by vendoring third_party/miniaudio and
// restoring Edge's version of this file (git show edge/edge:src/xenia/ui/
// audio_helper.cc).

DEFINE_path(achievement_sound_path, "",
            "Path (including filename) to achievement unlock sound. "
            "Currently unused - achievement audio is disabled in this build.",
            "UI");

namespace xe {
namespace ui {

AudioHelper& AudioHelper::Instance() {
  static AudioHelper instance;
  return instance;
}

AudioHelper::AudioHelper() {}

AudioHelper::~AudioHelper() { Shutdown(); }

void AudioHelper::Initialize() {
  if (initialized_) {
    return;
  }
  initialized_ = true;
  if (!cvars::achievement_sound_path.empty()) {
    XELOGW(
        "achievement_sound_path is set but achievement audio is disabled in "
        "this build (miniaudio not vendored)");
  }
}

void AudioHelper::Shutdown() { initialized_ = false; }

void AudioHelper::PlayAchievementSound() {
  // Intentionally silent - see the note at the top of this file.
}

}  // namespace ui
}  // namespace xe
