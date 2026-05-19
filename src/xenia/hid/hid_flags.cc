/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/hid/hid_flags.h"

DEFINE_bool(guide_button, false, "Forward guide button presses to guest.",
            "HID");
DEFINE_bool(hid_nop_connected, false,
            "Make the nop HID driver report a connected research controller.",
            "HID");
DEFINE_string(
    hid_nop_buttons, "",
    "Comma-separated research buttons held by the nop HID driver. Supported: "
    "a,b,x,y,start,back,du,dd,dl,dr,lb,rb.",
    "HID");
DEFINE_string(
    hid_nop_button_sequence, "",
    "Semicolon-separated research nop HID pulses in the form "
    "buttons@delay_ms:hold_ms, for example start@45000:3000;a@70000:3000.",
    "HID");
DEFINE_int32(
    hid_nop_buttons_delay_ms, 0,
    "Research delay before the nop HID driver reports hid_nop_buttons.",
    "HID");
DEFINE_int32(
    hid_nop_buttons_hold_ms, -1,
    "Research hold duration for hid_nop_buttons after delay, or negative to "
    "hold indefinitely.",
    "HID");
