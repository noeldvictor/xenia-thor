/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_ANDROID_ANDROID_AUDIO_SYSTEM_H_
#define XENIA_APU_ANDROID_ANDROID_AUDIO_SYSTEM_H_

#include "xenia/apu/audio_system.h"

namespace xe {
namespace apu {
namespace android {

class AndroidAudioSystem final : public AudioSystem {
 public:
  explicit AndroidAudioSystem(cpu::Processor* processor);
  ~AndroidAudioSystem() override;

  static bool IsAvailable() { return true; }

  X_STATUS CreateDriver(size_t index, xe::threading::Semaphore* semaphore,
                        AudioDriver** out_driver) override;
  void DestroyDriver(AudioDriver* driver) override;
};

}  // namespace android
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_ANDROID_ANDROID_AUDIO_SYSTEM_H_
