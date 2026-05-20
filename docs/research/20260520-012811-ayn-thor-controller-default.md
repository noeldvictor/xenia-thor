# AYN Thor Controller Default HID

Date: 2026-05-20 01:28:11 -04:00

## Goal

Make the AYN Thor built-in controls appear as Xenia controller 1 on Android
without depending on the `hid=nop` research controller. The `nop` path remains
useful for scripted START/A automation, but normal Thor launches need real
button and stick input.

## Current Finding

Android builds exclude SDL HID, and the Thor debug script had been launching
the emulator with `--es hid nop`. That is fine for repeatable title/speed
captures, but it makes the physical Thor controller invisible to guest
`XamInput` calls.

Thor-side `dumpsys input` reports the built-in pad as `Odin Controller`,
vendor `0x2020`, product `0x0111`, with `KEYBOARD | GAMEPAD | JOYSTICK`
sources. The live axis set includes `X/Y`, `Z/RZ`, `HAT_X/HAT_Y`, and
`BRAKE/GAS`, so the bridge keeps `Z/RZ` available for right stick and uses
`BRAKE/GAS` for trigger fallback.

## Mapping

- Android `KEYCODE_BUTTON_A/B/X/Y` -> XInput `A/B/X/Y`.
- Android `KEYCODE_BUTTON_L1/R1` -> XInput shoulders.
- Android `KEYCODE_BUTTON_L2/R2` -> XInput analog triggers as digital full
  press when reported as buttons.
- Android `KEYCODE_BUTTON_START/SELECT` -> XInput start/back.
- Android d-pad keycodes and hat axes -> XInput d-pad.
- Android `AXIS_X/Y` -> XInput left stick, with Y inverted.
- Android `AXIS_Z/RZ` or `AXIS_RX/RY` -> XInput right stick, with Y inverted.
- Android `AXIS_LTRIGGER/RTRIGGER`, then `BRAKE/GAS`, then Z/RZ fallback when
  RX/RY exists -> XInput triggers.

## Notes

This is a research-first Android HID bridge for Thor bring-up. It reports user
0 connected by default because the Thor has built-in controls, and it logs the
first few native key/motion updates for ADB validation.

Validation on 2026-05-20:

- Native Android build passed for ARM64 and x86_64.
- Full APK build passed.
- Installed to Thor serial `c3ca0370`.
- Blue Dragon launch with default `hid=android` logged
  `Android HID: active as XInput controller 1`.
- Synthetic `adb shell input keyevent KEYCODE_BUTTON_START` logged Android HID
  down/up events, proving the Java -> JNI -> XInput state bridge is live.
