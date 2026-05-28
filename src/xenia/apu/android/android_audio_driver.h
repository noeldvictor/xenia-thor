/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APU_ANDROID_ANDROID_AUDIO_DRIVER_H_
#define XENIA_APU_ANDROID_ANDROID_AUDIO_DRIVER_H_

#include <aaudio/AAudio.h>

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

#include "xenia/apu/audio_driver.h"
#include "xenia/base/threading.h"

namespace xe {
namespace apu {
namespace android {

class AndroidAudioDriver final : public AudioDriver {
 public:
  AndroidAudioDriver(Memory* memory, xe::threading::Semaphore* semaphore);
  ~AndroidAudioDriver() override;

  bool Initialize();
  void SubmitFrame(uint32_t frame_ptr) override;
  void Shutdown();

 private:
  struct QueuedFrame {
    std::vector<float> samples;
    size_t offset_frames = 0;
  };

  static aaudio_data_callback_result_t DataCallback(AAudioStream* stream,
                                                    void* user_data,
                                                    void* audio_data,
                                                    int32_t num_frames);
  static void ErrorCallback(AAudioStream* stream, void* user_data,
                            aaudio_result_t error);
  aaudio_data_callback_result_t FillAudio(float* output, int32_t num_frames);
  void ReleaseConsumedFrameLocked();

  xe::threading::Semaphore* semaphore_ = nullptr;
  AAudioStream* stream_ = nullptr;
  std::deque<std::unique_ptr<QueuedFrame>> frames_queued_;
  std::mutex frames_mutex_;
  bool shutdown_ = false;

  static constexpr int32_t kFrameFrequency = 48000;
  static constexpr int32_t kOutputChannels = 2;
  static constexpr size_t kInputChannels = 6;
  static constexpr size_t kChannelSamples = 256;
  static constexpr size_t kInputFrameSamples = kInputChannels * kChannelSamples;
  static constexpr size_t kOutputFrameSamples = kOutputChannels * kChannelSamples;
  static constexpr size_t kMaximumQueuedFrames = 64;
};

}  // namespace android
}  // namespace apu
}  // namespace xe

#endif  // XENIA_APU_ANDROID_ANDROID_AUDIO_DRIVER_H_
