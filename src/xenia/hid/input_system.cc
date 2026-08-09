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

  // Gameplay hotkeys (Back + RB/LB/X/Y): fast-forward, slow motion and quick
  // save/load, all without opening the OSD.
  if (result == X_ERROR_SUCCESS) {
    HandleHotkeys(user_index, out_state);
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

double InputSystem::FastForwardScalar() {
  double s = cvars::hotkey_speed_scalar;
  if (!(s >= 1.25)) s = 1.25;  // NaN-safe: !(NaN >= x) is true
  if (s > 8.0) s = 8.0;
  return s;
}

double InputSystem::SlowMotionScalar() {
  double s = cvars::hotkey_slowmo_scalar;
  if (!(s >= 0.1)) s = 0.1;
  if (s > 0.9) s = 0.9;
  return s;
}

// Back + <button> gameplay hotkeys. These live here rather than in the OSD
// because the OSD PAUSES the guest to open - which makes it structurally
// useless for the thing fast-forward is for. You cannot skip a cutscene from a
// menu that stops the cutscene.
//
// EDGE-TRIGGERED: each combo fires once when it becomes held and not again
// until released, so holding it does not re-fire every poll (guests poll input
// every frame, so a level-triggered check would toggle ~60x/sec).
//
// Every combo's buttons are SWALLOWED while held, for two reasons: Back alone
// opens the OSD in this fork, and RB/LB/X/Y are live gameplay buttons in most
// titles - letting either half reach the guest would fire an unwanted action
// every time you use a hotkey.
//
// ORDER MATTERS: the save/load combos are tested BEFORE the speed combos and
// return early, so a chord that happens to include several of these buttons
// cannot both save a state and change speed off one press.
void InputSystem::HandleHotkeys(uint32_t user_index,
                                X_INPUT_STATE* out_state) {
  if (user_index >= XUserMaxUserCount) {
    return;
  }
  // gamepad.buttons is a big-endian wrapper type, so read it into a plain
  // uint16_t and assign back (same pattern as the consumed_buttons_ masking).
  const uint16_t buttons = uint16_t(out_state->gamepad.buttons);
  const bool back = (buttons & X_INPUT_GAMEPAD_BACK) != 0;

  // Swallow a combo's buttons and record its edge. Returns true on the press
  // edge only.
  auto edge = [&](bool enabled, uint16_t mask, std::array<bool, XUserMaxUserCount>& state) {
    const bool held = enabled && back && (buttons & mask) == mask;
    const bool fired = held && !state[user_index];
    state[user_index] = held;
    if (held) {
      out_state->gamepad.buttons =
          uint16_t(uint16_t(out_state->gamepad.buttons) &
                   ~(mask | X_INPUT_GAMEPAD_BACK));
    }
    return fired;
  };

  // --- Quick save / quick load (Back + X / Back + Y) ---------------------
  const bool save_fired = edge(cvars::hotkey_save_state, X_INPUT_GAMEPAD_X,
                               save_state_combo_held_);
  const bool load_fired = edge(cvars::hotkey_save_state, X_INPUT_GAMEPAD_Y,
                               load_state_combo_held_);
  if (save_fired || load_fired) {
    if (hotkey_handler_) {
      hotkey_handler_(save_fired ? HotkeyAction::kSaveState
                                 : HotkeyAction::kLoadState);
    } else {
      // Not a silent no-op: on a build where nobody installed the handler this
      // is the only way to tell "the hotkey did not fire" from "the state
      // failed to save", and those have completely different fixes.
      XELOGW("Hotkey: {} requested but no handler is installed",
             save_fired ? "save state" : "load state");
    }
    return;
  }

  // --- Slow motion (Back + LB) -------------------------------------------
  const double slowmo = SlowMotionScalar();
  if (edge(cvars::hotkey_slowmo_toggle, X_INPUT_GAMEPAD_LEFT_SHOULDER,
           slowmo_combo_held_)) {
    const double current = xe::Clock::guest_time_scalar();
    // Compare against the target rather than 1.0 so an unrelated scalar (a
    // title profile, a debug setting) is restored rather than clobbered.
    const bool going_slow = current > slowmo;
    xe::Clock::set_guest_time_scalar(going_slow ? slowmo : 1.0);
    XELOGI("Slow motion: guest time scalar {} -> {}", current,
           going_slow ? slowmo : 1.0);
    return;
  }
  if (slowmo_combo_held_[user_index]) {
    return;  // held, already handled - do not also run the speed combo
  }

  // --- Fast forward (Back + RB), toggle or hold --------------------------
  if (!cvars::hotkey_speed_toggle) {
    return;
  }
  const double fast = FastForwardScalar();
  const bool was_held = speed_toggle_combo_held_[user_index];
  const bool fired = edge(true, X_INPUT_GAMEPAD_RIGHT_SHOULDER,
                          speed_toggle_combo_held_);
  const bool held = speed_toggle_combo_held_[user_index];

  if (cvars::hotkey_speed_hold) {
    if (fired) {
      // Capture what to go back to, so hold-mode composes with a title profile
      // instead of forcing 1x on release.
      speed_hold_restore_[user_index] = xe::Clock::guest_time_scalar();
      xe::Clock::set_guest_time_scalar(fast);
      XELOGI("Fast forward (hold): guest time scalar {} -> {}",
             speed_hold_restore_[user_index], fast);
    } else if (was_held && !held) {
      double restore = speed_hold_restore_[user_index];
      // Guard against a captured value that was itself the fast speed (two
      // sources fighting) - restoring it would latch fast-forward on release,
      // which is the one outcome hold mode must never produce.
      if (!(restore > 0.0) || restore >= fast) restore = 1.0;
      xe::Clock::set_guest_time_scalar(restore);
      XELOGI("Fast forward (hold) released: guest time scalar -> {}", restore);
    }
    return;
  }

  if (fired) {
    const double current = xe::Clock::guest_time_scalar();
    const bool going_fast = current < fast;
    xe::Clock::set_guest_time_scalar(going_fast ? fast : 1.0);
    XELOGI("Speed toggle: guest time scalar {} -> {}", current,
           going_fast ? fast : 1.0);
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
