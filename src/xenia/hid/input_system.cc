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
