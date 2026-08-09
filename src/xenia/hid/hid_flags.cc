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
DEFINE_bool(
    hotkey_speed_toggle, true,
    "Back + RB toggles guest fast-forward between 1x and hotkey_speed_scalar. "
    "Scales the GUEST CLOCK, so game logic, timers and audio all run at that "
    "rate - the usual emulator fast-forward, not a framerate change. "
    "Edge-triggered, so holding the combo flips once rather than every input "
    "poll. Both buttons are swallowed while the combo is held (Back alone "
    "opens the OSD, and RB is a live gameplay button in most titles).",
    "HID");
DEFINE_double(
    hotkey_speed_scalar, 2.0,
    "Fast-forward multiplier for the Back + RB hotkey. Clamped to 1.25-8.0. "
    "Above ~4x the audio mixer cannot keep up and the guest's own frame pacing "
    "starts to dominate, so higher values buy less than they look like they "
    "should - 2x and 3x are the useful settings.",
    "HID");
DEFINE_bool(
    hotkey_speed_hold, false,
    "Make Back + RB HOLD-to-fast-forward instead of toggle: speed returns to "
    "1x the moment the combo is released. Preferred for skipping short "
    "cutscenes and grind, where a toggle means two chord presses and "
    "overshooting; the toggle is better for long unattended stretches. "
    "Off = toggle (the default, and what the OSD button does).",
    "HID");
DEFINE_bool(
    hotkey_slowmo_toggle, true,
    "Back + LB toggles slow motion between 1x and hotkey_slowmo_scalar. Same "
    "guest-clock mechanism as fast-forward in the other direction, which makes "
    "reaction-timed sections and frame-precise inputs tractable, and is a "
    "genuinely useful DEBUGGING tool - a stall or a physics glitch that is "
    "invisible at speed is often obvious at 0.5x.",
    "HID");
DEFINE_double(
    hotkey_slowmo_scalar, 0.5,
    "Slow-motion multiplier for the Back + LB hotkey. Clamped to 0.1-0.9.",
    "HID");
DEFINE_bool(
    hotkey_save_state, true,
    "Back + X saves a quick state and Back + Y loads it, without opening the "
    "OSD. The in-game menu can already do both, but opening it PAUSES the "
    "guest and takes several presses - which is the wrong shape for the thing "
    "save states are actually for (retrying a hard section). The state file is "
    "the same one the menu's Save/Load buttons use, so the two agree.",
    "HID");
DEFINE_string(
    hotkey_state_path, "",
    "Absolute path the Back + X / Back + Y quick-state hotkeys read and write. "
    "Set by the Android UI at launch to the SAME per-title file the in-game "
    "menu uses, so a quick save is visible to the menu and vice versa. Empty "
    "disables the hotkeys (they log rather than guessing a path - writing a "
    "save state somewhere the user cannot find it is worse than not saving).",
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
