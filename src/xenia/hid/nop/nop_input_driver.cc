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
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/ui/virtual_key.h"

namespace xe {
namespace hid {
namespace nop {
namespace {

std::string TrimString(std::string value) {
  value.erase(value.begin(),
              std::find_if(value.begin(), value.end(), [](unsigned char c) {
                return !std::isspace(c);
              }));
  value.erase(std::find_if(value.rbegin(), value.rend(),
                           [](unsigned char c) { return !std::isspace(c); })
                  .base(),
              value.end());
  return value;
}

int32_t ParseIntOrDefault(const std::string& value, int32_t default_value) {
  if (value.empty()) {
    return default_value;
  }
  char* end = nullptr;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (end == value.c_str()) {
    return default_value;
  }
  return int32_t(parsed);
}

ui::VirtualKey GetVirtualKeyForButton(uint16_t button) {
  switch (button) {
    case X_INPUT_GAMEPAD_DPAD_UP:
      return ui::VirtualKey::kXInputPadDpadUp;
    case X_INPUT_GAMEPAD_DPAD_DOWN:
      return ui::VirtualKey::kXInputPadDpadDown;
    case X_INPUT_GAMEPAD_DPAD_LEFT:
      return ui::VirtualKey::kXInputPadDpadLeft;
    case X_INPUT_GAMEPAD_DPAD_RIGHT:
      return ui::VirtualKey::kXInputPadDpadRight;
    case X_INPUT_GAMEPAD_START:
      return ui::VirtualKey::kXInputPadStart;
    case X_INPUT_GAMEPAD_BACK:
      return ui::VirtualKey::kXInputPadBack;
    case X_INPUT_GAMEPAD_LEFT_SHOULDER:
      return ui::VirtualKey::kXInputPadLShoulder;
    case X_INPUT_GAMEPAD_RIGHT_SHOULDER:
      return ui::VirtualKey::kXInputPadRShoulder;
    case X_INPUT_GAMEPAD_A:
      return ui::VirtualKey::kXInputPadA;
    case X_INPUT_GAMEPAD_B:
      return ui::VirtualKey::kXInputPadB;
    case X_INPUT_GAMEPAD_X:
      return ui::VirtualKey::kXInputPadX;
    case X_INPUT_GAMEPAD_Y:
      return ui::VirtualKey::kXInputPadY;
    default:
      return ui::VirtualKey::kNone;
  }
}

}  // namespace

NopInputDriver::NopInputDriver(xe::ui::Window* window, size_t window_z_order)
    : InputDriver(window, window_z_order),
      button_schedule_start_time_(std::chrono::steady_clock::now()) {}

NopInputDriver::~NopInputDriver() = default;

X_STATUS NopInputDriver::Setup() {
  LogResearchControllerOnce(0, "setup");
  return X_STATUS_SUCCESS;
}

bool NopInputDriver::IsResearchControllerConnected() const {
  return cvars::hid_nop_connected || !cvars::hid_nop_buttons.empty() ||
         !cvars::hid_nop_button_sequence.empty() ||
         !cvars::hid_nop_trigger_file.empty();
}

uint16_t NopInputDriver::GetButtonsFromString(std::string buttons) {
  std::replace(buttons.begin(), buttons.end(), '+', ',');
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
    } else if (token == "ls") {
      result |= X_INPUT_GAMEPAD_LEFT_THUMB;
    } else if (token == "rs") {
      result |= X_INPUT_GAMEPAD_RIGHT_THUMB;
    }

    if (token_end == buttons.size()) {
      break;
    }
    token_start = token_end + 1;
  }
  return result;
}

NopInputDriver::Analog NopInputDriver::GetAnalogFromString(
    const std::string& buttons) {
  Analog analog;
  size_t token_start = 0;
  while (token_start <= buttons.size()) {
    size_t token_end = buttons.find(',', token_start);
    if (token_end == std::string::npos) {
      token_end = buttons.size();
    }
    std::string token = TrimString(
        buttons.substr(token_start, token_end - token_start));
    if (token == "ls_up") {
      analog.thumb_ly = INT16_MAX;
    } else if (token == "ls_down") {
      analog.thumb_ly = -INT16_MAX;
    } else if (token == "ls_left") {
      analog.thumb_lx = -INT16_MAX;
    } else if (token == "ls_right") {
      analog.thumb_lx = INT16_MAX;
    } else if (token == "rs_up") {
      analog.thumb_ry = INT16_MAX;
    } else if (token == "rs_down") {
      analog.thumb_ry = -INT16_MAX;
    } else if (token == "rs_left") {
      analog.thumb_rx = -INT16_MAX;
    } else if (token == "rs_right") {
      analog.thumb_rx = INT16_MAX;
    } else if (token == "lt") {
      analog.left_trigger = UINT8_MAX;
    } else if (token == "rt") {
      analog.right_trigger = UINT8_MAX;
    }
    if (token_end == buttons.size()) {
      break;
    }
    token_start = token_end + 1;
  }
  return analog;
}

uint16_t NopInputDriver::GetConfiguredButtons() {
  uint16_t result = GetButtonsFromString(cvars::hid_nop_buttons);

  std::string sequence = cvars::hid_nop_button_sequence;
  size_t entry_start = 0;
  while (entry_start <= sequence.size()) {
    size_t entry_end = sequence.find_first_of(";|", entry_start);
    if (entry_end == std::string::npos) {
      entry_end = sequence.size();
    }
    std::string entry =
        TrimString(sequence.substr(entry_start, entry_end - entry_start));
    const size_t at = entry.find('@');
    if (at != std::string::npos) {
      entry.resize(at);
    }
    result |= GetButtonsFromString(entry);

    if (entry_end == sequence.size()) {
      break;
    }
    entry_start = entry_end + 1;
  }

  return result;
}

uint16_t NopInputDriver::GetActiveButtons() const {
  uint16_t active_buttons = 0;
  // The trigger file: "start:300" presses START for 300 ms from now.
  if (!cvars::hid_nop_trigger_file.empty()) {
    const auto now = std::chrono::steady_clock::now();
    if (now < trigger_until_) {
      active_buttons |= trigger_buttons_;
    } else if (std::chrono::duration_cast<std::chrono::milliseconds>(
                   now - trigger_last_poll_)
                   .count() >= 100) {
      trigger_last_poll_ = now;
      FILE* f = fopen(cvars::hid_nop_trigger_file.c_str(), "rb");
      if (f) {
        char line[128] = {};
        if (!fgets(line, sizeof(line), f)) {
          line[0] = 0;
        }
        fclose(f);
        std::remove(cvars::hid_nop_trigger_file.c_str());
        std::string entry = TrimString(line);
        // "cvar:name=value" sets a cvar live (the PC has no debug server):
        // a draw-state trace can be switched on when the screen is right.
        if (entry.rfind("cvar:", 0) == 0) {
          const size_t eq = entry.find('=');
          if (eq != std::string::npos) {
            std::string name = entry.substr(5, eq - 5);
            std::string value = entry.substr(eq + 1);
            bool applied = cvar::SetCommandVarFromString(name, value);
            XELOGI("nop HID trigger: cvar {}={} -> {}", name, value,
                   applied ? "applied" : "unknown");
          }
          return active_buttons;
        }
        const size_t colon = entry.find(':');
        const int32_t hold_ms =
            colon == std::string::npos
                ? 200
                : std::max(1, ParseIntOrDefault(
                                  TrimString(entry.substr(colon + 1)), 200));
        trigger_buttons_ = GetButtonsFromString(entry.substr(0, colon));
        trigger_analog_ = GetAnalogFromString(entry.substr(0, colon));
        trigger_until_ = now + std::chrono::milliseconds(hold_ms);
        active_buttons |= trigger_buttons_;
      }
    }
  }
  if (cvars::hid_nop_buttons.empty() &&
      cvars::hid_nop_button_sequence.empty()) {
    return active_buttons;
  }

  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() -
                              button_schedule_start_time_)
                              .count();

  if (!cvars::hid_nop_buttons.empty()) {
    const int32_t delay_ms = std::max(0, cvars::hid_nop_buttons_delay_ms);
    const int32_t hold_ms = cvars::hid_nop_buttons_hold_ms;
    if (elapsed_ms >= delay_ms &&
        (hold_ms < 0 || elapsed_ms < int64_t(delay_ms) + hold_ms)) {
      active_buttons |= GetButtonsFromString(cvars::hid_nop_buttons);
    }
  }

  std::string sequence = cvars::hid_nop_button_sequence;
  size_t entry_start = 0;
  while (entry_start <= sequence.size()) {
    size_t entry_end = sequence.find_first_of(";|", entry_start);
    if (entry_end == std::string::npos) {
      entry_end = sequence.size();
    }
    std::string entry =
        TrimString(sequence.substr(entry_start, entry_end - entry_start));
    const size_t at = entry.find('@');
    if (at != std::string::npos) {
      const std::string buttons = entry.substr(0, at);
      const std::string timing = entry.substr(at + 1);
      const size_t colon = timing.find(':');
      const int32_t delay_ms = std::max(
          0, ParseIntOrDefault(
                 TrimString(timing.substr(0, colon)), 0));
      const int32_t hold_ms =
          colon == std::string::npos
              ? std::max(1, cvars::hid_nop_buttons_hold_ms)
              : ParseIntOrDefault(TrimString(timing.substr(colon + 1)), 1000);
      if (elapsed_ms >= delay_ms &&
          (hold_ms < 0 || elapsed_ms < int64_t(delay_ms) + hold_ms)) {
        active_buttons |= GetButtonsFromString(buttons);
      }
    }

    if (entry_end == sequence.size()) {
      break;
    }
    entry_start = entry_end + 1;
  }

  return active_buttons;
}

void NopInputDriver::LogResearchControllerOnce(uint32_t user_index,
                                               const char* source) {
  if (!IsResearchControllerConnected() || research_logged_) {
    return;
  }
  research_logged_ = true;
  XELOGI(
      "Nop HID research controller active via {}: user={} connected={} "
      "buttons='{}' sequence='{}' delay_ms={} hold_ms={}",
      source, user_index, cvars::hid_nop_connected, cvars::hid_nop_buttons,
      cvars::hid_nop_button_sequence, cvars::hid_nop_buttons_delay_ms,
      cvars::hid_nop_buttons_hold_ms);
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
    // The trigger file's sticks and triggers, while its entry is held.
    Analog analog;
    if (std::chrono::steady_clock::now() < trigger_until_) {
      analog = trigger_analog_;
    }
    const bool analog_changed =
        std::memcmp(&analog, &previous_analog_, sizeof(analog)) != 0;
    if (buttons != previous_buttons_ || analog_changed) {
      ++packet_number_;
    }
    out_state->packet_number = packet_number_;
    out_state->gamepad.buttons = buttons;
    out_state->gamepad.left_trigger = analog.left_trigger;
    out_state->gamepad.right_trigger = analog.right_trigger;
    out_state->gamepad.thumb_lx = analog.thumb_lx;
    out_state->gamepad.thumb_ly = analog.thumb_ly;
    out_state->gamepad.thumb_rx = analog.thumb_rx;
    out_state->gamepad.thumb_ry = analog.thumb_ry;
    previous_buttons_ = buttons;
    previous_analog_ = analog;
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
  uint8_t actual_user_index = static_cast<uint8_t>(user_index & 0xFF);
  if (!IsResearchControllerConnected() ||
      (actual_user_index != 0 && actual_user_index != 0xFF)) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  if (!out_keystroke) {
    return X_ERROR_BAD_ARGUMENTS;
  }
  if (actual_user_index == 0xFF) {
    actual_user_index = 0;
  }

  LogResearchControllerOnce(user_index, "GetKeystroke");
  std::memset(out_keystroke, 0, sizeof(*out_keystroke));

  const uint16_t active_buttons = GetActiveButtons();
  const uint16_t changed_buttons = active_buttons ^ previous_keystroke_buttons_;
  if (!changed_buttons) {
    return X_ERROR_EMPTY;
  }

  constexpr uint16_t kButtonOrder[] = {
      X_INPUT_GAMEPAD_DPAD_UP,        X_INPUT_GAMEPAD_DPAD_DOWN,
      X_INPUT_GAMEPAD_DPAD_LEFT,      X_INPUT_GAMEPAD_DPAD_RIGHT,
      X_INPUT_GAMEPAD_START,          X_INPUT_GAMEPAD_BACK,
      X_INPUT_GAMEPAD_LEFT_SHOULDER,  X_INPUT_GAMEPAD_RIGHT_SHOULDER,
      X_INPUT_GAMEPAD_A,              X_INPUT_GAMEPAD_B,
      X_INPUT_GAMEPAD_X,              X_INPUT_GAMEPAD_Y,
  };

  auto emit_button = [&](uint16_t button, uint16_t flags) {
    const ui::VirtualKey virtual_key = GetVirtualKeyForButton(button);
    if (virtual_key == ui::VirtualKey::kNone) {
      return false;
    }
    out_keystroke->virtual_key = uint16_t(virtual_key);
    out_keystroke->unicode = 0;
    out_keystroke->flags = flags;
    out_keystroke->user_index = actual_user_index;
    out_keystroke->hid_code = 0;
    return true;
  };

  for (uint16_t button : kButtonOrder) {
    if ((changed_buttons & button) && !(active_buttons & button) &&
        emit_button(button, X_INPUT_KEYSTROKE_KEYUP)) {
      previous_keystroke_buttons_ &= ~button;
      return X_ERROR_SUCCESS;
    }
  }
  for (uint16_t button : kButtonOrder) {
    if ((changed_buttons & button) && (active_buttons & button) &&
        emit_button(button, X_INPUT_KEYSTROKE_KEYDOWN)) {
      previous_keystroke_buttons_ |= button;
      return X_ERROR_SUCCESS;
    }
  }

  previous_keystroke_buttons_ = active_buttons;
  return X_ERROR_EMPTY;
}

}  // namespace nop
}  // namespace hid
}  // namespace xe
