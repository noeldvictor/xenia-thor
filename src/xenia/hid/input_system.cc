/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/input_system.h"

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/base/profiling.h"
#include "xenia/base/clock.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/hid/input_driver.h"

namespace xe {
namespace hid {

InputSystem::InputSystem(xe::ui::Window* window) : window_(window) {}

InputSystem::~InputSystem() = default;

X_STATUS InputSystem::Setup() { return X_STATUS_SUCCESS; }

void InputSystem::AddDriver(std::unique_ptr<InputDriver> driver) {
  drivers_.push_back(std::move(driver));
}

X_RESULT InputSystem::GetCapabilities(uint32_t user_index, uint32_t flags,
                                      X_INPUT_CAPABILITIES* out_caps) {
  SCOPE_profile_cpu_f("hid");

  bool any_connected = false;
  for (auto& driver : drivers_) {
    X_RESULT result = driver->GetCapabilities(user_index, flags, out_caps);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS) {
      return result;
    }
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

std::unique_lock<std::recursive_mutex> InputSystem::lock() {
  return std::unique_lock<std::recursive_mutex>{lock_};
}

void InputSystem::UpdateUsedSlot(uint32_t slot, bool connected) {
  if (slot >= XUserMaxUserCount) {
    return;
  }
  if (connected_slots_.test(slot) == connected) {
    // No state change, so nothing to do.
    return;
  }
  XELOGI(controller_slot_state_change_message_[connected], slot);
  connected_slots_.flip(slot);
  // NOTE(kernel-port): Edge additionally raises
  // kXNotificationSystemInputDevicesChanged here through kernel_state(). The
  // xenia-hid library does not link against xenia-kernel in this fork (it
  // links xenia-base only), so the broadcast is omitted; guests learn about
  // slot changes on their next poll instead.
  // Re-enable path: add "xenia-kernel" to the xenia-hid links in
  // src/xenia/hid/premake5.lua (watch for the resulting link cycle) or route
  // the notification out through a callback installed by the emulator.
}

X_RESULT InputSystem::GetState(uint32_t user_index, uint32_t flags,
                               X_INPUT_STATE* out_state) {
  SCOPE_profile_cpu_f("hid");

  // If UI is blocking input, return zeroed state to the game.
  if (ui_input_blockers_.load() > 0) {
    std::memset(out_state, 0, sizeof(X_INPUT_STATE));
    return X_ERROR_SUCCESS;
  }

  X_RESULT result = GetStateForUI(user_index, flags, out_state);

  // Speed-toggle hotkey (Back + RB): flip guest time between 1x and 2x.
  if (result == X_ERROR_SUCCESS) {
    HandleSpeedToggleHotkey(user_index, out_state);
  }

  // Mask buttons that were held when a UI dialog closed until they're
  // released, so the close-press doesn't carry through into the game.
  if (result == X_ERROR_SUCCESS && user_index < XUserMaxUserCount &&
      consumed_buttons_[user_index] != 0) {
    uint16_t buttons = out_state->gamepad.buttons;
    consumed_buttons_[user_index] &= buttons;
    out_state->gamepad.buttons = buttons & ~consumed_buttons_[user_index];
  }

  return result;
}

// Back + RB toggles guest fast-forward between 1x and 2x.
//
// EDGE-TRIGGERED: fires once when the combo becomes held and not again until
// it is released, so holding it does not flip the speed every poll (guests
// poll input every frame, so a level-triggered check would toggle ~60x/sec).
//
// Both buttons are SWALLOWED while the combo is held, for two reasons: Back
// alone opens the OSD in this fork, and RB is a live gameplay button in most
// titles - letting either reach the guest would fire an unwanted action every
// time you change speed.
void InputSystem::HandleSpeedToggleHotkey(uint32_t user_index,
                                          X_INPUT_STATE* out_state) {
  if (!cvars::hotkey_speed_toggle || user_index >= XUserMaxUserCount) {
    return;
  }
  constexpr uint16_t kCombo =
      X_INPUT_GAMEPAD_BACK | X_INPUT_GAMEPAD_RIGHT_SHOULDER;
  // gamepad.buttons is a big-endian wrapper type, so read it into a plain
  // uint16_t and assign back (same pattern as the consumed_buttons_ masking).
  const uint16_t buttons = uint16_t(out_state->gamepad.buttons);
  const bool held = (buttons & kCombo) == kCombo;
  if (held && !speed_toggle_combo_held_[user_index]) {
    const double current = xe::Clock::guest_time_scalar();
    // Compare against the 2x target rather than 1.0 so an unrelated scalar
    // (a title profile, a debug setting) is restored rather than clobbered.
    const bool going_fast = current < kSpeedToggleScalar;
    xe::Clock::set_guest_time_scalar(going_fast ? kSpeedToggleScalar : 1.0);
    XELOGI("Speed toggle: guest time scalar {} -> {}", current,
           going_fast ? kSpeedToggleScalar : 1.0);
  }
  speed_toggle_combo_held_[user_index] = held;
  if (held) {
    out_state->gamepad.buttons = uint16_t(buttons & ~kCombo);
  }
}

X_RESULT InputSystem::GetStateForUI(uint32_t user_index, uint32_t flags,
                                    X_INPUT_STATE* out_state) {
  SCOPE_profile_cpu_f("hid");

  bool any_connected = false;
  for (auto& driver : drivers_) {
    X_RESULT result = driver->GetState(user_index, out_state);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS) {
      UpdateUsedSlot(user_index, true);
      if (user_index < XUserMaxUserCount && out_state->gamepad.buttons != 0) {
        last_used_slot_ = user_index;
      }
      return result;
    }
  }
  UpdateUsedSlot(user_index, false);
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

void InputSystem::AddUIInputBlocker() { ui_input_blockers_.fetch_add(1); }

void InputSystem::RemoveUIInputBlocker() {
  // Before removing the blocker, capture any currently pressed buttons. These
  // will be masked from game input until they are released, preventing the
  // button press that closed the UI from carrying over into the game.
  X_INPUT_STATE state = {};
  for (uint32_t user_index = 0; user_index < XUserMaxUserCount; user_index++) {
    if (GetStateForUI(user_index, X_INPUT_FLAG_GAMEPAD, &state) ==
        X_ERROR_SUCCESS) {
      consumed_buttons_[user_index] |= state.gamepad.buttons;
    }
  }

  ui_input_blockers_.fetch_sub(1);
}

X_RESULT InputSystem::SetState(uint32_t user_index,
                               X_INPUT_VIBRATION* vibration) {
  SCOPE_profile_cpu_f("hid");

  bool any_connected = false;
  for (auto& driver : drivers_) {
    X_RESULT result = driver->SetState(user_index, vibration);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS) {
      return result;
    }
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

X_RESULT InputSystem::GetKeystroke(uint32_t user_index, uint32_t flags,
                                   X_INPUT_KEYSTROKE* out_keystroke) {
  SCOPE_profile_cpu_f("hid");

  // If UI is blocking input, return empty keystroke to the game.
  if (ui_input_blockers_.load() > 0) {
    std::memset(out_keystroke, 0, sizeof(X_INPUT_KEYSTROKE));
    return X_ERROR_EMPTY;
  }

  bool any_connected = false;
  for (auto& driver : drivers_) {
    X_RESULT result = driver->GetKeystroke(user_index, flags, out_keystroke);
    if (result != X_ERROR_DEVICE_NOT_CONNECTED) {
      any_connected = true;
    }
    if (result == X_ERROR_SUCCESS) {
      if (user_index < XUserMaxUserCount) {
        last_used_slot_ = user_index;
      }
      return result;
    }
    if (result == X_ERROR_EMPTY) {
      return result;
    }
  }
  return any_connected ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
}

}  // namespace hid
}  // namespace xe
