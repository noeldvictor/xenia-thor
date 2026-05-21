/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_NOP_NOP_INPUT_DRIVER_H_
#define XENIA_HID_NOP_NOP_INPUT_DRIVER_H_

#include <chrono>
#include <string>

#include "xenia/hid/input_driver.h"

namespace xe {
namespace hid {
namespace nop {

class NopInputDriver final : public InputDriver {
 public:
  explicit NopInputDriver(xe::ui::Window* window, size_t window_z_order);
  ~NopInputDriver() override;

  X_STATUS Setup() override;

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps) override;
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) override;
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) override;
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke) override;

 private:
  static uint16_t GetButtonsFromString(std::string buttons);
  static uint16_t GetConfiguredButtons();
  uint16_t GetActiveButtons() const;
  bool IsResearchControllerConnected() const;
  void LogResearchControllerOnce(uint32_t user_index, const char* source);

  uint32_t packet_number_ = 1;
  uint16_t previous_buttons_ = 0;
  uint16_t previous_keystroke_buttons_ = 0;
  bool research_logged_ = false;
  std::chrono::steady_clock::time_point button_schedule_start_time_;
};

}  // namespace nop
}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_NOP_NOP_INPUT_DRIVER_H_
