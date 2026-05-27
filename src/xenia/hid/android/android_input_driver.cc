/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Thor research fork contributors.                       *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/android/android_input_driver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <android/keycodes.h>
#include <jni.h>

#include "xenia/base/logging.h"
#include "xenia/hid/hid_flags.h"
#include "xenia/ui/virtual_key.h"

namespace xe {
namespace hid {
namespace android {
namespace {

struct AndroidGamepadSharedState {
  std::mutex mutex;
  bool connected = true;
  uint32_t packet_number = 1;
  uint16_t key_buttons = 0;
  uint16_t hat_buttons = 0;
  bool left_trigger_digital = false;
  bool right_trigger_digital = false;
  uint8_t left_trigger_analog = 0;
  uint8_t right_trigger_analog = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
  int16_t thumb_rx = 0;
  int16_t thumb_ry = 0;
  int32_t device_id = -1;
  int log_budget = 16;
  uint16_t previous_keystroke_buttons = 0;

  uint16_t buttons() const { return uint16_t(key_buttons | hat_buttons); }
  uint8_t left_trigger() const {
    return left_trigger_digital ? 255 : left_trigger_analog;
  }
  uint8_t right_trigger() const {
    return right_trigger_digital ? 255 : right_trigger_analog;
  }
};

AndroidGamepadSharedState g_android_gamepad_state;

bool IsUserZero(uint32_t user_index) {
  const uint32_t masked_user_index = user_index & 0xFF;
  return masked_user_index == 0 || masked_user_index == 0xFF;
}

void AdvancePacket(AndroidGamepadSharedState& state) {
  ++state.packet_number;
  if (state.packet_number == 0) {
    state.packet_number = 1;
  }
}

uint16_t GetAndroidKeyButton(jint key_code) {
  switch (key_code) {
    case AKEYCODE_DPAD_UP:
      return X_INPUT_GAMEPAD_DPAD_UP;
    case AKEYCODE_DPAD_DOWN:
      return X_INPUT_GAMEPAD_DPAD_DOWN;
    case AKEYCODE_DPAD_LEFT:
      return X_INPUT_GAMEPAD_DPAD_LEFT;
    case AKEYCODE_DPAD_RIGHT:
      return X_INPUT_GAMEPAD_DPAD_RIGHT;
    case AKEYCODE_BUTTON_A:
      return X_INPUT_GAMEPAD_A;
    case AKEYCODE_BUTTON_B:
      return X_INPUT_GAMEPAD_B;
    case AKEYCODE_BUTTON_C:
      // Some Android handheld key layouts expose the west face button as C.
      return X_INPUT_GAMEPAD_X;
    case AKEYCODE_BUTTON_X:
      return X_INPUT_GAMEPAD_X;
    case AKEYCODE_BUTTON_Y:
      return X_INPUT_GAMEPAD_Y;
    case AKEYCODE_BUTTON_Z:
      // Some Android handheld key layouts expose the north face button as Z.
      return X_INPUT_GAMEPAD_Y;
    case AKEYCODE_BUTTON_L1:
      return X_INPUT_GAMEPAD_LEFT_SHOULDER;
    case AKEYCODE_BUTTON_R1:
      return X_INPUT_GAMEPAD_RIGHT_SHOULDER;
    case AKEYCODE_BUTTON_THUMBL:
      return X_INPUT_GAMEPAD_LEFT_THUMB;
    case AKEYCODE_BUTTON_THUMBR:
      return X_INPUT_GAMEPAD_RIGHT_THUMB;
    case AKEYCODE_BUTTON_START:
      return X_INPUT_GAMEPAD_START;
    case AKEYCODE_BUTTON_SELECT:
      return X_INPUT_GAMEPAD_BACK;
    case AKEYCODE_BUTTON_MODE:
      return cvars::guide_button ? X_INPUT_GAMEPAD_GUIDE : 0;
    default:
      return 0;
  }
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
    case X_INPUT_GAMEPAD_LEFT_THUMB:
      return ui::VirtualKey::kXInputPadLThumbPress;
    case X_INPUT_GAMEPAD_RIGHT_THUMB:
      return ui::VirtualKey::kXInputPadRThumbPress;
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

bool IsAndroidTriggerKey(jint key_code, bool& is_left_trigger_out) {
  if (key_code == AKEYCODE_BUTTON_L2) {
    is_left_trigger_out = true;
    return true;
  }
  if (key_code == AKEYCODE_BUTTON_R2) {
    is_left_trigger_out = false;
    return true;
  }
  return false;
}

int16_t FloatToStick(jfloat value, bool invert) {
  float clamped = std::clamp(float(value), -1.0f, 1.0f);
  if (invert) {
    clamped = -clamped;
  }
  if (clamped >= 0.0f) {
    return int16_t(std::round(clamped * 32767.0f));
  }
  return int16_t(std::round(clamped * 32768.0f));
}

uint8_t FloatToTrigger(jfloat value) {
  return uint8_t(std::round(std::clamp(float(value), 0.0f, 1.0f) * 255.0f));
}

uint16_t HatToButtons(jfloat hat_x, jfloat hat_y) {
  uint16_t buttons = 0;
  if (hat_x <= -0.5f) {
    buttons |= X_INPUT_GAMEPAD_DPAD_LEFT;
  } else if (hat_x >= 0.5f) {
    buttons |= X_INPUT_GAMEPAD_DPAD_RIGHT;
  }
  if (hat_y <= -0.5f) {
    buttons |= X_INPUT_GAMEPAD_DPAD_UP;
  } else if (hat_y >= 0.5f) {
    buttons |= X_INPUT_GAMEPAD_DPAD_DOWN;
  }
  return buttons;
}

void FillCapabilities(X_INPUT_CAPABILITIES* out_caps) {
  std::memset(out_caps, 0, sizeof(*out_caps));
  out_caps->type = 0x01;
  out_caps->sub_type = 0x01;
  out_caps->flags = X_INPUT_CAPS_FFB_SUPPORTED;
  out_caps->gamepad.buttons =
      uint16_t(0xF3FF | (cvars::guide_button ? X_INPUT_GAMEPAD_GUIDE : 0));
  out_caps->gamepad.left_trigger = 255;
  out_caps->gamepad.right_trigger = 255;
  out_caps->gamepad.thumb_lx = static_cast<int16_t>(0xFFFFu);
  out_caps->gamepad.thumb_ly = static_cast<int16_t>(0xFFFFu);
  out_caps->gamepad.thumb_rx = static_cast<int16_t>(0xFFFFu);
  out_caps->gamepad.thumb_ry = static_cast<int16_t>(0xFFFFu);
  out_caps->vibration.left_motor_speed = 65535;
  out_caps->vibration.right_motor_speed = 65535;
}

void FillState(const AndroidGamepadSharedState& shared_state,
               X_INPUT_STATE* out_state) {
  std::memset(out_state, 0, sizeof(*out_state));
  out_state->packet_number = shared_state.packet_number;
  out_state->gamepad.buttons = shared_state.buttons();
  out_state->gamepad.left_trigger = shared_state.left_trigger();
  out_state->gamepad.right_trigger = shared_state.right_trigger();
  out_state->gamepad.thumb_lx = shared_state.thumb_lx;
  out_state->gamepad.thumb_ly = shared_state.thumb_ly;
  out_state->gamepad.thumb_rx = shared_state.thumb_rx;
  out_state->gamepad.thumb_ry = shared_state.thumb_ry;
}

void LogAndroidEventLocked(AndroidGamepadSharedState& state,
                           const char* source, int32_t device_id,
                           const char* detail) {
  if (state.log_budget <= 0) {
    return;
  }
  --state.log_budget;
  XELOGI("Android HID: {} device={} {}", source, device_id, detail);
}

void ResetStateForSetup() {
  auto& state = g_android_gamepad_state;
  std::lock_guard<std::mutex> guard(state.mutex);
  state.connected = true;
  state.key_buttons = 0;
  state.hat_buttons = 0;
  state.left_trigger_digital = false;
  state.right_trigger_digital = false;
  state.left_trigger_analog = 0;
  state.right_trigger_analog = 0;
  state.thumb_lx = 0;
  state.thumb_ly = 0;
  state.thumb_rx = 0;
  state.thumb_ry = 0;
  state.device_id = -1;
  state.previous_keystroke_buttons = 0;
  state.log_budget = 16;
  AdvancePacket(state);
}

void UpdateAndroidGamepadKey(jint key_code, bool pressed, jint repeat_count,
                             jint device_id) {
  auto& state = g_android_gamepad_state;
  std::lock_guard<std::mutex> guard(state.mutex);
  state.connected = true;
  state.device_id = device_id;

  bool changed = false;
  bool is_left_trigger = false;
  if (IsAndroidTriggerKey(key_code, is_left_trigger)) {
    bool& digital_trigger =
        is_left_trigger ? state.left_trigger_digital
                        : state.right_trigger_digital;
    if (digital_trigger != pressed) {
      digital_trigger = pressed;
      changed = true;
    }
  } else {
    const uint16_t button = GetAndroidKeyButton(key_code);
    if (!button) {
      return;
    }
    const uint16_t old_buttons = state.key_buttons;
    if (pressed) {
      state.key_buttons |= button;
    } else {
      state.key_buttons &= ~button;
    }
    changed = old_buttons != state.key_buttons;
  }

  if (changed) {
    AdvancePacket(state);
  }

  if (changed || (pressed && repeat_count == 0)) {
    LogAndroidEventLocked(state, "key", device_id,
                          pressed ? "down" : "up");
  }
}

void UpdateAndroidGamepadMotion(jint device_id, jfloat left_x, jfloat left_y,
                                jfloat right_x, jfloat right_y,
                                jfloat left_trigger, jfloat right_trigger,
                                jfloat hat_x, jfloat hat_y) {
  auto& state = g_android_gamepad_state;
  std::lock_guard<std::mutex> guard(state.mutex);
  state.connected = true;
  state.device_id = device_id;

  const int16_t thumb_lx = FloatToStick(left_x, false);
  const int16_t thumb_ly = FloatToStick(left_y, true);
  const int16_t thumb_rx = FloatToStick(right_x, false);
  const int16_t thumb_ry = FloatToStick(right_y, true);
  const uint8_t left_trigger_byte = FloatToTrigger(left_trigger);
  const uint8_t right_trigger_byte = FloatToTrigger(right_trigger);
  const uint16_t hat_buttons = HatToButtons(hat_x, hat_y);

  bool changed = false;
  changed |= state.thumb_lx != thumb_lx;
  changed |= state.thumb_ly != thumb_ly;
  changed |= state.thumb_rx != thumb_rx;
  changed |= state.thumb_ry != thumb_ry;
  changed |= state.left_trigger_analog != left_trigger_byte;
  changed |= state.right_trigger_analog != right_trigger_byte;
  changed |= state.hat_buttons != hat_buttons;

  if (!changed) {
    return;
  }

  state.thumb_lx = thumb_lx;
  state.thumb_ly = thumb_ly;
  state.thumb_rx = thumb_rx;
  state.thumb_ry = thumb_ry;
  state.left_trigger_analog = left_trigger_byte;
  state.right_trigger_analog = right_trigger_byte;
  state.hat_buttons = hat_buttons;
  AdvancePacket(state);
  LogAndroidEventLocked(state, "motion", device_id, "state changed");
}

}  // namespace

AndroidInputDriver::AndroidInputDriver(xe::ui::Window* window,
                                       size_t window_z_order)
    : InputDriver(window, window_z_order) {}

AndroidInputDriver::~AndroidInputDriver() = default;

X_STATUS AndroidInputDriver::Setup() {
  ResetStateForSetup();
  XELOGI(
      "Android HID: active as XInput controller 1. Thor/Android gamepad "
      "buttons, sticks, triggers, and hat are mapped to user 0.");
  return X_STATUS_SUCCESS;
}

X_RESULT AndroidInputDriver::GetCapabilities(uint32_t user_index,
                                             uint32_t flags,
                                             X_INPUT_CAPABILITIES* out_caps) {
  (void)flags;
  if (!IsUserZero(user_index) || !out_caps) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  auto& state = g_android_gamepad_state;
  std::lock_guard<std::mutex> guard(state.mutex);
  if (!state.connected) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  FillCapabilities(out_caps);
  return X_ERROR_SUCCESS;
}

X_RESULT AndroidInputDriver::GetState(uint32_t user_index,
                                      X_INPUT_STATE* out_state) {
  if (!IsUserZero(user_index) || !out_state) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  auto& state = g_android_gamepad_state;
  std::lock_guard<std::mutex> guard(state.mutex);
  if (!state.connected) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  FillState(state, out_state);
  if (!is_active()) {
    std::memset(&out_state->gamepad, 0, sizeof(out_state->gamepad));
  }
  return X_ERROR_SUCCESS;
}

X_RESULT AndroidInputDriver::SetState(uint32_t user_index,
                                      X_INPUT_VIBRATION* vibration) {
  (void)vibration;
  if (!IsUserZero(user_index)) {
    return X_ERROR_BAD_ARGUMENTS;
  }
  return X_ERROR_SUCCESS;
}

X_RESULT AndroidInputDriver::GetKeystroke(uint32_t user_index, uint32_t flags,
                                          X_INPUT_KEYSTROKE* out_keystroke) {
  (void)flags;
  const uint8_t actual_user_index = static_cast<uint8_t>(user_index & 0xFF);
  if (actual_user_index != 0 && actual_user_index != 0xFF) {
    return X_ERROR_BAD_ARGUMENTS;
  }
  if (!out_keystroke) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  auto& state = g_android_gamepad_state;
  std::lock_guard<std::mutex> guard(state.mutex);
  if (!state.connected) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  std::memset(out_keystroke, 0, sizeof(*out_keystroke));

  const uint16_t active_buttons = is_active() ? state.buttons() : 0;
  const uint16_t changed_buttons =
      uint16_t(active_buttons ^ state.previous_keystroke_buttons);
  if (!changed_buttons) {
    return X_ERROR_EMPTY;
  }

  constexpr std::array<uint16_t, 14> kButtonOrder = {
      X_INPUT_GAMEPAD_DPAD_UP,        X_INPUT_GAMEPAD_DPAD_DOWN,
      X_INPUT_GAMEPAD_DPAD_LEFT,      X_INPUT_GAMEPAD_DPAD_RIGHT,
      X_INPUT_GAMEPAD_START,          X_INPUT_GAMEPAD_BACK,
      X_INPUT_GAMEPAD_LEFT_THUMB,     X_INPUT_GAMEPAD_RIGHT_THUMB,
      X_INPUT_GAMEPAD_LEFT_SHOULDER,  X_INPUT_GAMEPAD_RIGHT_SHOULDER,
      X_INPUT_GAMEPAD_A,              X_INPUT_GAMEPAD_B,
      X_INPUT_GAMEPAD_X,              X_INPUT_GAMEPAD_Y,
  };

  auto emit_button = [&](uint16_t button, uint16_t keystroke_flags) {
    const ui::VirtualKey virtual_key = GetVirtualKeyForButton(button);
    if (virtual_key == ui::VirtualKey::kNone) {
      return false;
    }
    out_keystroke->virtual_key = uint16_t(virtual_key);
    out_keystroke->unicode = 0;
    out_keystroke->flags = keystroke_flags;
    out_keystroke->user_index = actual_user_index == 0xFF ? 0 : actual_user_index;
    out_keystroke->hid_code = 0;
    return true;
  };

  for (uint16_t button : kButtonOrder) {
    if ((changed_buttons & button) && !(active_buttons & button) &&
        emit_button(button, X_INPUT_KEYSTROKE_KEYUP)) {
      state.previous_keystroke_buttons &= uint16_t(~button);
      LogAndroidEventLocked(state, "keystroke", state.device_id, "up");
      return X_ERROR_SUCCESS;
    }
  }
  for (uint16_t button : kButtonOrder) {
    if ((changed_buttons & button) && (active_buttons & button) &&
        emit_button(button, X_INPUT_KEYSTROKE_KEYDOWN)) {
      state.previous_keystroke_buttons |= button;
      LogAndroidEventLocked(state, "keystroke", state.device_id, "down");
      return X_ERROR_SUCCESS;
    }
  }

  state.previous_keystroke_buttons = active_buttons;
  return X_ERROR_EMPTY;
}

}  // namespace android
}  // namespace hid
}  // namespace xe

extern "C" JNIEXPORT void JNICALL
Java_jp_xenia_emulator_EmulatorActivity_nativeOnAndroidGamepadKey(
    JNIEnv* env, jclass clazz, jint key_code, jboolean pressed,
    jint repeat_count, jint device_id) {
  (void)env;
  (void)clazz;
  xe::hid::android::UpdateAndroidGamepadKey(key_code, pressed == JNI_TRUE,
                                            repeat_count, device_id);
}

extern "C" JNIEXPORT void JNICALL
Java_jp_xenia_emulator_EmulatorActivity_nativeOnAndroidGamepadMotion(
    JNIEnv* env, jclass clazz, jint device_id, jfloat left_x, jfloat left_y,
    jfloat right_x, jfloat right_y, jfloat left_trigger,
    jfloat right_trigger, jfloat hat_x, jfloat hat_y) {
  (void)env;
  (void)clazz;
  xe::hid::android::UpdateAndroidGamepadMotion(
      device_id, left_x, left_y, right_x, right_y, left_trigger, right_trigger,
      hat_x, hat_y);
}
