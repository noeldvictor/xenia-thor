/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/android/android_audio_system.h"

#include <memory>

#include "xenia/apu/android/android_audio_driver.h"
#include "xenia/base/assert.h"
#include "xenia/base/logging.h"

namespace xe {
namespace apu {
namespace android {

AndroidAudioSystem::AndroidAudioSystem(cpu::Processor* processor)
    : AudioSystem(processor) {
  XELOGI("AndroidAudioSystem selected native Android audio backend");
}

AndroidAudioSystem::~AndroidAudioSystem() = default;

X_STATUS AndroidAudioSystem::CreateDriver(size_t index,
                                          xe::threading::Semaphore* semaphore,
                                          AudioDriver** out_driver) {
  (void)index;
  assert_not_null(out_driver);
  XELOGI("AndroidAudioSystem creating Android audio driver {}", index);
  auto driver = std::make_unique<AndroidAudioDriver>(memory_, semaphore);
  if (!driver->Initialize()) {
    driver->Shutdown();
    return X_STATUS_UNSUCCESSFUL;
  }

  *out_driver = driver.release();
  return X_STATUS_SUCCESS;
}

void AndroidAudioSystem::DestroyDriver(AudioDriver* driver) {
  assert_not_null(driver);
  auto android_driver = dynamic_cast<AndroidAudioDriver*>(driver);
  assert_not_null(android_driver);
  android_driver->Shutdown();
  delete android_driver;
}

}  // namespace android
}  // namespace apu
}  // namespace xe
