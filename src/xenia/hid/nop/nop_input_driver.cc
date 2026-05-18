/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/nop/nop_input_driver.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#include "xenia/base/logging.h"
#include "xenia/hid/hid_flags.h"

namespace xe {
namespace hid {
namespace nop {

NopInputDriver::NopInputDriver(xe::ui::Window* window, size_t window_z_order)
    : InputDriver(window, window_z_order),
      button_schedule_start_time_(std::chrono::steady_clock::now()) {}

NopInputDriver::~NopInputDriver() = default;

X_STATUS NopInputDriver::Setup() {
  LogResearchControllerOnce(0, "setup");
  return X_STATUS_SUCCESS;
}

bool NopInputDriver::IsResearchControllerConnected() const {
  return cvars::hid_nop_connected || !cvars::hid_nop_buttons.empty();
}

uint16_t NopInputDriver::GetConfiguredButtons() {
  std::string buttons = cvars::hid_nop_buttons;
  std::transform(buttons.begin(), buttons.end(), buttons.begin(),
                 [](unsigned char c) { return char(std::tolower(c)); });

  uint16_t result = 0;
  size_t token_start = 0;
  while (token_start <= buttons.size()) {
    size_t token_end = buttons.find(',', token_start);
    if (token_end == std::string::npos) {
      token_end = buttons.size();
    }
    std::string token = buttons.substr(token_start, token_end - token_start);
    token.erase(std::remove_if(token.begin(), token.end(),
                               [](unsigned char c) { return std::isspace(c); }),
                token.end());
    if (token == "a") {
      result |= X_INPUT_GAMEPAD_A;
    } else if (token == "b") {
      result |= X_INPUT_GAMEPAD_B;
    } else if (token == "x") {
      result |= X_INPUT_GAMEPAD_X;
    } else if (token == "y") {
      result |= X_INPUT_GAMEPAD_Y;
    } else if (token == "start") {
      result |= X_INPUT_GAMEPAD_START;
    } else if (token == "back") {
      result |= X_INPUT_GAMEPAD_BACK;
    } else if (token == "du" || token == "up") {
      result |= X_INPUT_GAMEPAD_DPAD_UP;
    } else if (token == "dd" || token == "down") {
      result |= X_INPUT_GAMEPAD_DPAD_DOWN;
    } else if (token == "dl" || token == "left") {
      result |= X_INPUT_GAMEPAD_DPAD_LEFT;
    } else if (token == "dr" || token == "right") {
      result |= X_INPUT_GAMEPAD_DPAD_RIGHT;
    } else if (token == "lb") {
      result |= X_INPUT_GAMEPAD_LEFT_SHOULDER;
    } else if (token == "rb") {
      result |= X_INPUT_GAMEPAD_RIGHT_SHOULDER;
    }

    if (token_end == buttons.size()) {
      break;
    }
    token_start = token_end + 1;
  }
  return result;
}

uint16_t NopInputDriver::GetActiveButtons() const {
  if (cvars::hid_nop_buttons.empty()) {
    return 0;
  }

  const int32_t delay_ms = std::max(0, cvars::hid_nop_buttons_delay_ms);
  const int32_t hold_ms = cvars::hid_nop_buttons_hold_ms;
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() -
                              button_schedule_start_time_)
                              .count();
  if (elapsed_ms < delay_ms) {
    return 0;
  }
  if (hold_ms >= 0 && elapsed_ms >= int64_t(delay_ms) + hold_ms) {
    return 0;
  }
  return GetConfiguredButtons();
}

void NopInputDriver::LogResearchControllerOnce(uint32_t user_index,
                                               const char* source) {
  if (!IsResearchControllerConnected() || research_logged_) {
    return;
  }
  research_logged_ = true;
  XELOGI(
      "Nop HID research controller active via {}: user={} connected={} "
      "buttons='{}' delay_ms={} hold_ms={}",
      source, user_index, cvars::hid_nop_connected, cvars::hid_nop_buttons,
      cvars::hid_nop_buttons_delay_ms, cvars::hid_nop_buttons_hold_ms);
}

X_RESULT NopInputDriver::GetCapabilities(uint32_t user_index, uint32_t flags,
                                         X_INPUT_CAPABILITIES* out_caps) {
  if (!IsResearchControllerConnected() ||
      ((user_index & 0xFF) != 0 && (user_index & 0xFF) != 0xFF)) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  LogResearchControllerOnce(user_index, "GetCapabilities");
  if (out_caps) {
    std::memset(out_caps, 0, sizeof(*out_caps));
    out_caps->type = 0x01;
    out_caps->sub_type = 0x01;
    out_caps->gamepad.buttons = GetConfiguredButtons();
  }
  return X_ERROR_SUCCESS;
}

X_RESULT NopInputDriver::GetState(uint32_t user_index,
                                  X_INPUT_STATE* out_state) {
  if (!IsResearchControllerConnected() ||
      ((user_index & 0xFF) != 0 && (user_index & 0xFF) != 0xFF)) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  LogResearchControllerOnce(user_index, "GetState");
  if (out_state) {
    std::memset(out_state, 0, sizeof(*out_state));
    uint16_t buttons = GetActiveButtons();
    if (buttons != previous_buttons_) {
      ++packet_number_;
    }
    out_state->packet_number = packet_number_;
    out_state->gamepad.buttons = buttons;
    previous_buttons_ = buttons;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT NopInputDriver::SetState(uint32_t user_index,
                                  X_INPUT_VIBRATION* vibration) {
  return IsResearchControllerConnected() ? X_ERROR_SUCCESS
                                         : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT NopInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                      X_INPUT_KEYSTROKE* out_keystroke) {
  if (!IsResearchControllerConnected()) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return X_ERROR_EMPTY;
}

}  // namespace nop
}  // namespace hid
}  // namespace xe
