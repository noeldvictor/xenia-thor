/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/nop/nop_audio_system.h"

#include <chrono>

#include "xenia/apu/audio_driver.h"
#include "xenia/apu/apu_flags.h"
#include "xenia/base/assert.h"
#include "xenia/base/logging.h"
#include "xenia/base/threading.h"

namespace xe {
namespace apu {
namespace nop {

namespace {

class NopAudioDriver final : public AudioDriver {
 public:
  NopAudioDriver(Memory* memory, xe::threading::Semaphore* semaphore)
      : AudioDriver(memory), semaphore_(semaphore) {}

  void SubmitFrame(uint32_t samples_ptr) override {
    (void)samples_ptr;
    if (semaphore_) {
      xe::threading::Sleep(std::chrono::milliseconds(5));
      semaphore_->Release(1, nullptr);
    }
  }

 private:
  xe::threading::Semaphore* semaphore_ = nullptr;
};

}  // namespace

std::unique_ptr<AudioSystem> NopAudioSystem::Create(cpu::Processor* processor) {
  return std::make_unique<NopAudioSystem>(processor);
}

NopAudioSystem::NopAudioSystem(cpu::Processor* processor)
    : AudioSystem(processor) {}

NopAudioSystem::~NopAudioSystem() = default;

X_STATUS NopAudioSystem::CreateDriver(size_t index,
                                      xe::threading::Semaphore* semaphore,
                                      AudioDriver** out_driver) {
  (void)index;
  assert_not_null(out_driver);
  *out_driver = new NopAudioDriver(memory(), semaphore);
  XELOGI("NopAudioSystem created silent audio driver {}", index);
  return X_STATUS_SUCCESS;
}

void NopAudioSystem::DestroyDriver(AudioDriver* driver) { delete driver; }

}  // namespace nop
}  // namespace apu
}  // namespace xe
