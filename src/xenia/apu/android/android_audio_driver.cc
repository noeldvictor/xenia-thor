/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/android/android_audio_driver.h"

#include <algorithm>
#include <cstring>

#include "xenia/apu/apu_flags.h"
#include "xenia/apu/conversion.h"
#include "xenia/base/logging.h"

namespace xe {
namespace apu {
namespace android {

AndroidAudioDriver::AndroidAudioDriver(Memory* memory,
                                       xe::threading::Semaphore* semaphore)
    : AudioDriver(memory), semaphore_(semaphore) {}

AndroidAudioDriver::~AndroidAudioDriver() { Shutdown(); }

bool AndroidAudioDriver::Initialize() {
  AAudioStreamBuilder* builder = nullptr;
  aaudio_result_t result = AAudio_createStreamBuilder(&builder);
  if (result != AAUDIO_OK || !builder) {
    XELOGE("AndroidAudioDriver: AAudio_createStreamBuilder failed: {}",
           AAudio_convertResultToText(result));
    return false;
  }

  AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
  AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
  AAudioStreamBuilder_setPerformanceMode(builder,
                                         AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setSampleRate(builder, kFrameFrequency);
  AAudioStreamBuilder_setChannelCount(builder, kOutputChannels);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setDataCallback(builder, DataCallback, this);
  AAudioStreamBuilder_setErrorCallback(builder, ErrorCallback, this);

  result = AAudioStreamBuilder_openStream(builder, &stream_);
  AAudioStreamBuilder_delete(builder);
  if (result != AAUDIO_OK || !stream_) {
    XELOGE("AndroidAudioDriver: AAudioStreamBuilder_openStream failed: {}",
           AAudio_convertResultToText(result));
    stream_ = nullptr;
    return false;
  }

  result = AAudioStream_requestStart(stream_);
  if (result != AAUDIO_OK) {
    XELOGE("AndroidAudioDriver: AAudioStream_requestStart failed: {}",
           AAudio_convertResultToText(result));
    Shutdown();
    return false;
  }

  XELOGI("AndroidAudioDriver opened AAudio output: sample_rate={} channels={}",
         AAudioStream_getSampleRate(stream_),
         AAudioStream_getChannelCount(stream_));
  return true;
}

void AndroidAudioDriver::SubmitFrame(uint32_t frame_ptr) {
  const auto input_frame = memory_->TranslateVirtual<float*>(frame_ptr);
  if (!input_frame) {
    if (semaphore_) {
      semaphore_->Release(1, nullptr);
    }
    return;
  }

  auto frame = std::make_unique<QueuedFrame>();
  frame->samples.resize(kOutputFrameSamples);
  if (cvars::mute) {
    std::fill(frame->samples.begin(), frame->samples.end(), 0.0f);
  } else {
    conversion::sequential_6_BE_to_interleaved_2_LE(
        frame->samples.data(), input_frame, kChannelSamples);
  }

  std::unique_lock<std::mutex> guard(frames_mutex_);
  if (shutdown_ || !stream_) {
    guard.unlock();
    if (semaphore_) {
      semaphore_->Release(1, nullptr);
    }
    return;
  }
  while (frames_queued_.size() >= kMaximumQueuedFrames) {
    frames_queued_.pop_front();
    if (semaphore_) {
      semaphore_->Release(1, nullptr);
    }
  }
  frames_queued_.push_back(std::move(frame));
}

void AndroidAudioDriver::Shutdown() {
  AAudioStream* stream = nullptr;
  {
    std::unique_lock<std::mutex> guard(frames_mutex_);
    if (shutdown_) {
      return;
    }
    shutdown_ = true;
    stream = stream_;
    stream_ = nullptr;
    while (!frames_queued_.empty()) {
      frames_queued_.pop_front();
      if (semaphore_) {
        semaphore_->Release(1, nullptr);
      }
    }
  }

  if (stream) {
    AAudioStream_requestStop(stream);
    AAudioStream_close(stream);
  }
}

aaudio_data_callback_result_t AndroidAudioDriver::DataCallback(
    AAudioStream* stream, void* user_data, void* audio_data,
    int32_t num_frames) {
  (void)stream;
  auto driver = static_cast<AndroidAudioDriver*>(user_data);
  if (!driver || !audio_data || num_frames <= 0) {
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }
  return driver->FillAudio(static_cast<float*>(audio_data), num_frames);
}

void AndroidAudioDriver::ErrorCallback(AAudioStream* stream, void* user_data,
                                       aaudio_result_t error) {
  (void)stream;
  auto driver = static_cast<AndroidAudioDriver*>(user_data);
  XELOGE("AndroidAudioDriver AAudio stream error: {}",
         AAudio_convertResultToText(error));
  if (driver) {
    std::unique_lock<std::mutex> guard(driver->frames_mutex_);
    driver->stream_ = nullptr;
  }
}

aaudio_data_callback_result_t AndroidAudioDriver::FillAudio(float* output,
                                                            int32_t num_frames) {
  size_t frames_remaining = static_cast<size_t>(num_frames);
  float* write_ptr = output;

  std::unique_lock<std::mutex> guard(frames_mutex_);
  while (frames_remaining > 0) {
    if (frames_queued_.empty()) {
      std::fill(write_ptr, write_ptr + frames_remaining * kOutputChannels, 0.0f);
      break;
    }

    auto& frame = frames_queued_.front();
    const size_t available_frames = kChannelSamples - frame->offset_frames;
    const size_t copy_frames = std::min(frames_remaining, available_frames);
    const float* frame_ptr =
        frame->samples.data() + frame->offset_frames * kOutputChannels;
    std::memcpy(write_ptr, frame_ptr,
                copy_frames * kOutputChannels * sizeof(float));
    write_ptr += copy_frames * kOutputChannels;
    frames_remaining -= copy_frames;
    frame->offset_frames += copy_frames;

    if (frame->offset_frames >= kChannelSamples) {
      ReleaseConsumedFrameLocked();
    }
  }

  return shutdown_ ? AAUDIO_CALLBACK_RESULT_STOP
                   : AAUDIO_CALLBACK_RESULT_CONTINUE;
}

void AndroidAudioDriver::ReleaseConsumedFrameLocked() {
  frames_queued_.pop_front();
  if (semaphore_) {
    semaphore_->Release(1, nullptr);
  }
}

}  // namespace android
}  // namespace apu
}  // namespace xe
