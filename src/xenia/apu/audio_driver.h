/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_AUDIO_DRIVER_H_
#define XENIA_APU_AUDIO_DRIVER_H_

#include "xenia/memory.h"
#include "xenia/xbox.h"

namespace xe {
namespace apu {

class AudioDriver {
 public:
  // Frame geometry (Edge kernel-port: the XMP media player mixes host-side
  // float frames of this size).
  static constexpr uint32_t kFrameFrequencyDefault = 48000;
  static constexpr uint32_t kFrameChannelsDefault = 6;
  static constexpr uint32_t kChannelSamplesDefault = 256;
  static constexpr uint32_t kFrameSamplesMax =
      kFrameChannelsDefault * kChannelSamplesDefault;
  static constexpr uint32_t kFrameSizeMax = sizeof(float) * kFrameSamplesMax;

  explicit AudioDriver(Memory* memory);
  virtual ~AudioDriver();

  // Guest-buffer submission (the XMA/XAudio path this fork uses).
  virtual void SubmitFrame(uint32_t samples_ptr) = 0;

 protected:
  inline uint8_t* TranslatePhysical(uint32_t guest_address) const {
    return memory_->TranslatePhysical(guest_address);
  }

  Memory* memory_ = nullptr;
};

}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_AUDIO_DRIVER_H_
