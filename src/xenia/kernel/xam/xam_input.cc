/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/emulator.h"
#include "xenia/hid/input.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/xbox.h"

DEFINE_bool(xam_input_trace, false,
            "Log the guest's controller polls (first few, then every button "
            "state change) and whether a XAM dialog is discarding them. For "
            "diagnosing a title that renders but ignores the pad.",
            "HID");

namespace xe {
namespace kernel {
namespace xam {

using xe::hid::X_INPUT_CAPABILITIES;
using xe::hid::X_INPUT_FLAG;
using xe::hid::X_INPUT_KEYSTROKE;
using xe::hid::X_INPUT_STATE;
using xe::hid::X_INPUT_VIBRATION;

dword_result_t XAutomationpUnbindController_entry(dword_t user_index) {
  if (user_index >= XUserMaxUserCount) {
    return 0;
  }

  return 1;
}
DECLARE_XAM_EXPORT1(XAutomationpUnbindController, kInput, kStub);

void XamResetInactivity_entry() {
  // Do we need to do anything?
}
DECLARE_XAM_EXPORT1(XamResetInactivity, kInput, kStub);

dword_result_t XamEnableInactivityProcessing_entry(dword_t inactivity_index,
                                                   dword_t enable) {
  // Enables/disables screen saver and auto shutoff
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamEnableInactivityProcessing, kInput, kStub);

dword_result_t XamInputGetCapabilitiesEx_entry(
    dword_t unk, dword_t user_index, dword_t flags,
    pointer_t<X_INPUT_CAPABILITIES> caps) {
  if (unk > 1) {
    return X_ERROR_NOT_SUPPORTED;
  }

  // Fail-safe check
  if (!caps) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  caps.Zero();

  if ((flags & X_INPUT_FLAG::X_INPUT_FLAG_ANY_USER) != 0) {
    // should trap
  }

  if ((flags & 4) != 0) {
    // should trap
  }

  uint32_t actual_user_index = user_index;
  if ((actual_user_index & XUserIndexAny) == XUserIndexAny ||
      (flags & X_INPUT_FLAG::X_INPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    actual_user_index = 0;
  }

  uint32_t actual_flags = flags;
  if (!flags) {
    actual_flags = X_INPUT_FLAG::X_INPUT_FLAG_GAMEPAD |
                   X_INPUT_FLAG::X_INPUT_FLAG_KEYBOARD;
  }

  auto input_system = kernel_state()->emulator()->input_system();
  auto lock = input_system->lock();
  return input_system->GetCapabilities(actual_user_index, actual_flags, caps);
}
DECLARE_XAM_EXPORT1(XamInputGetCapabilitiesEx, kInput, kSketchy);

// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetcapabilities(v=vs.85).aspx
dword_result_t XamInputGetCapabilities_entry(
    dword_t user_index, dword_t flags, pointer_t<X_INPUT_CAPABILITIES> caps) {
  // chrispy: actually, it appears that caps is never checked for null, it is
  // memset at the start regardless
  return XamInputGetCapabilitiesEx_entry(1, user_index, flags, caps);
}
DECLARE_XAM_EXPORT1(XamInputGetCapabilities, kInput, kSketchy);

// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetstate(v=vs.85).aspx
dword_result_t XamInputGetState_entry(dword_t user_index, dword_t flags,
                                      pointer_t<X_INPUT_STATE> input_state) {
  if (input_state) {
    memset((void*)input_state.host_address(), 0, sizeof(X_INPUT_STATE));
  }
  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  if (kernel_state()->xam_state()->IsUIActive()) {
    // Swallows the pad entirely (zeroed state, success). If a XAM dialog is ever
    // left present, the guest goes permanently deaf to input while still
    // rendering - which looks exactly like a hung title screen. Say so.
    static std::atomic<uint32_t> swallowed{0};
    uint32_t count = swallowed.fetch_add(1, std::memory_order_relaxed);
    if ((count % 600) == 0) {
      XELOGW(
          "XamInputGetState: a XAM dialog is present - input is being "
          "discarded (poll {}). The guest will not see the controller until "
          "the dialog is dismissed.",
          count);
    }
    return X_ERROR_SUCCESS;
  }

  // Games call this with a NULL state ptr, probably as a query.

  uint32_t actual_user_index = user_index;
  // chrispy: change this, logic is not right
  if ((actual_user_index & XUserIndexAny) == XUserIndexAny ||
      (flags & X_INPUT_FLAG::X_INPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    actual_user_index = 0;
  }

  X_RESULT result;
  auto input_system = kernel_state()->emulator()->input_system();
  {
    auto lock = input_system->lock();
    result = input_system->GetState(
        user_index, !flags ? X_INPUT_FLAG::X_INPUT_FLAG_GAMEPAD : flags,
        input_state);
  }

  if (input_state && result == X_ERROR_SUCCESS) {
    if (auto patch = kernel_state()->xmp_volume_patch()) {
      patch->OnInputPoll(input_state->packet_number);
    }
  }

  // Input reaching the host but not the guest is invisible otherwise - the game
  // simply ignores the pad and looks hung on a title screen. Report the first
  // few polls (does the guest poll at all?) and every button state change (does
  // the press actually arrive?). Cheap: this is a kHighFrequency export, so it
  // is capped and only fires on transitions.
  if (cvars::xam_input_trace && input_state) {
    static std::atomic<uint32_t> poll_count{0};
    static std::atomic<uint16_t> last_buttons{0};
    uint16_t buttons = input_state->gamepad.buttons;
    uint32_t polls = poll_count.fetch_add(1, std::memory_order_relaxed);
    uint16_t previous = last_buttons.exchange(buttons, std::memory_order_relaxed);
    if (polls < 4 || buttons != previous) {
      XELOGI(
          "XamInputGetState: user={} flags={:08X} result={:08X} buttons={:04X} "
          "poll #{}",
          uint32_t(user_index), uint32_t(flags), uint32_t(result), buttons,
          polls);
    }
  }

  return result;
}
DECLARE_XAM_EXPORT2(XamInputGetState, kInput, kImplemented, kHighFrequency);

// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputsetstate(v=vs.85).aspx
dword_result_t XamInputSetState_entry(
    dword_t user_index,
    dword_t flags, /* flags, as far as i can see, is not used*/
    pointer_t<X_INPUT_VIBRATION> vibration) {
  if (user_index >= XUserMaxUserCount) {
    return X_E_DEVICE_NOT_CONNECTED;
  }
  if (!vibration) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  auto input_system = kernel_state()->emulator()->input_system();
  auto lock = input_system->lock();
  return input_system->SetState(user_index, vibration);
}
DECLARE_XAM_EXPORT1(XamInputSetState, kInput, kImplemented);

// https://msdn.microsoft.com/en-us/library/windows/desktop/microsoft.directx_sdk.reference.xinputgetkeystroke(v=vs.85).aspx
dword_result_t XamInputGetKeystroke_entry(
    dword_t user_index, dword_t flags, pointer_t<X_INPUT_KEYSTROKE> keystroke) {
  // https://github.com/CodeAsm/ffplay360/blob/master/Common/AtgXime.cpp
  // user index = index or XUSER_INDEX_ANY
  // flags = XINPUT_FLAG_GAMEPAD (| _ANYUSER | _ANYDEVICE)

  if (!keystroke) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  uint32_t actual_user_index = user_index;
  if ((actual_user_index & XUserIndexAny) == XUserIndexAny ||
      (flags & X_INPUT_FLAG::X_INPUT_FLAG_ANY_USER)) {
    // Always pin user to 0.
    actual_user_index = 0;
  }

  auto input_system = kernel_state()->emulator()->input_system();
  auto lock = input_system->lock();
  return input_system->GetKeystroke(user_index, flags, keystroke);
}
DECLARE_XAM_EXPORT1(XamInputGetKeystroke, kInput, kImplemented);

// Same as non-ex, just takes a pointer to user index.
dword_result_t XamInputGetKeystrokeEx_entry(
    lpdword_t user_index_ptr, dword_t flags,
    pointer_t<X_INPUT_KEYSTROKE> keystroke) {
  if (!keystroke) {
    return X_ERROR_BAD_ARGUMENTS;
  }

  keystroke.Zero();

  if (kernel_state()->xam_state()->IsUIActive()) {
    return X_ERROR_SUCCESS;
  }

  uint32_t user_index = *user_index_ptr;
  auto input_system = kernel_state()->emulator()->input_system();
  auto lock = input_system->lock();
  if ((user_index & XUserIndexAny) == XUserIndexAny) {
    // Always pin user to 0.
    user_index = 0;
  }

  if (flags & X_INPUT_FLAG::X_INPUT_FLAG_ANY_USER) {
    // That flag means we should iterate over every connected controller and
    // check which one have pending request.
    auto result = X_ERROR_DEVICE_NOT_CONNECTED;
    for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
      auto result = input_system->GetKeystroke(i, flags, keystroke);

      // Return result from first user that have pending request
      if (result == X_ERROR_SUCCESS) {
        *user_index_ptr = keystroke->user_index;
        return result;
      }
    }
    return result;
  }

  auto result = input_system->GetKeystroke(user_index, flags, keystroke);

  if (XSUCCEEDED(result)) {
    *user_index_ptr = keystroke->user_index;
  }
  return result;
}
DECLARE_XAM_EXPORT1(XamInputGetKeystrokeEx, kInput, kImplemented);

X_HRESULT_result_t XamUserGetDeviceContext_entry(dword_t user_index,
                                                 dword_t device_type,
                                                 lpdword_t out_ptr) {
  // Games check the result - usually with some masking.
  // If this function fails they assume zero, so let's fail AND
  // set zero just to be safe.
  *out_ptr = 0;
  if (kernel_state()->xam_state()->IsUserSignedIn(user_index) ||
      (user_index & XUserIndexAny) == XUserIndexAny) {
    *out_ptr = (uint32_t)user_index;
    return X_E_SUCCESS;
  } else {
    return X_E_DEVICE_NOT_CONNECTED;
  }
}
DECLARE_XAM_EXPORT1(XamUserGetDeviceContext, kInput, kStub);

X_HRESULT_result_t XamInputNonControllerGetRawEx_entry(
    dword_t device_id, lpdword_t buffer_ptr, lpdword_t buffer_length_ptr,
    lpword_t state_ptr) {
  if (device_id != 5 && device_id != 6) {
    return X_ERROR_INVALID_PARAMETER;
  }
  if (!state_ptr || !buffer_length_ptr || !buffer_ptr) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (*buffer_length_ptr == 0 || *buffer_length_ptr > hid::kPortalBufferSize) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto portal = kernel_state()->emulator()->input_system()->GetPortal();
  if (!portal) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  uint32_t bytes_read = *buffer_length_ptr;
  uint16_t state = 0;

  const auto result = portal->Read(
      {kernel_memory()->TranslateVirtual(buffer_ptr.guest_address()),
       *buffer_length_ptr},
      bytes_read, state);

  if (XSUCCEEDED(result)) {
    *buffer_length_ptr = bytes_read;
    *state_ptr = state;
  }
  return result;
}
DECLARE_XAM_EXPORT1(XamInputNonControllerGetRawEx, kInput, kSketchy);

X_HRESULT_result_t XamInputNonControllerSetRawEx_entry(dword_t device_id,
                                                       lpdword_t buffer_ptr,
                                                       dword_t buffer_length) {
  if (device_id != 5 && device_id != 6) {
    return X_ERROR_INVALID_PARAMETER;
  }
  if (!buffer_ptr || !buffer_length || buffer_length > hid::kPortalBufferSize) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto portal = kernel_state()->emulator()->input_system()->GetPortal();
  if (!portal) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }

  return portal->Write(
      {kernel_memory()->TranslateVirtual(buffer_ptr.guest_address()),
       buffer_length});
}
DECLARE_XAM_EXPORT1(XamInputNonControllerSetRawEx, kInput, kSketchy);

X_HRESULT_result_t XamInputNonControllerGetRaw_entry(
    lpword_t state_ptr, lpdword_t buffer_length_ptr, lpdword_t buffer_ptr) {
  return XamInputNonControllerGetRawEx_entry(5, buffer_ptr, buffer_length_ptr,
                                             state_ptr);
}
DECLARE_XAM_EXPORT1(XamInputNonControllerGetRaw, kInput, kSketchy);

X_HRESULT_result_t XamInputNonControllerSetRaw_entry(dword_t buffer_length,
                                                     lpdword_t buffer_ptr) {
  // Normally there are handled separatelly with different first param, but
  // whatever.
  return XamInputNonControllerSetRawEx_entry(5, buffer_ptr, buffer_length);
}
DECLARE_XAM_EXPORT1(XamInputNonControllerSetRaw, kInput, kSketchy);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Input);
