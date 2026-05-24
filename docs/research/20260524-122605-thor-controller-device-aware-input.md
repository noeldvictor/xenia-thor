# Thor Controller Device-Aware Input Fix

Date: 2026-05-24 12:26 EDT

Starting HEAD: `ddfef0ce6`

## Goal

Fix the AYN Thor built-in controller path for normal emulator launches after the
launcher default had already been moved to `hid=android`.

The prior fix made the emulator choose the Android HID driver, but the Activity
event bridge still had two handheld-specific blind spots:

- D-pad keys could be filtered when Android delivered them as keyboard/virtual
  events instead of explicit gamepad-source events.
- The Thor/Odin keylayout exposes `BUTTON_C` and `BUTTON_Z`, but the native
  mapper only accepted `A/B/X/Y`.

## Device Evidence

`adb shell dumpsys input` still reports the built-in controller as:

```text
Device 13: Odin Controller
Sources: KEYBOARD | GAMEPAD | JOYSTICK
Identifier: bus=0x0003, vendor=0x2020, product=0x0111
KeyLayoutFile: /system/usr/keylayout/Vendor_2020_Product_0111.kl
Motion Ranges: X, Y, Z, RZ, HAT_X, HAT_Y, GAS, BRAKE
```

The keylayout maps the extra face-button-style codes:

```text
key 0x132 BUTTON_C
key 0x135 BUTTON_Z
```

## Patch

- `EmulatorActivity` now forwards every recognized Android D-pad/gamepad key
  code to the native HID bridge, regardless of whether Android marks the event
  source as gamepad, keyboard, or virtual.
- Motion events now also accept a device-aware `Odin Controller` fallback and
  retry axis ranges through `SOURCE_JOYSTICK` and `SOURCE_GAMEPAD` before the
  generic range lookup.
- A small Java log budget (`XeniaInput`) records the first controller key/motion
  events, making future Thor checks easier without noisy logcat spam.
- Native Android HID maps `AKEYCODE_BUTTON_C` to Xbox `X` and
  `AKEYCODE_BUTTON_Z` to Xbox `Y` as handheld compatibility fallbacks.

## Validation

Build/deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShellDeploy -DeviceSerial c3ca0370
```

Both passed and streamed install succeeded.

Launch and bridge check:

```powershell
adb logcat -c
adb shell am force-stop jp.xenia.emulator.github.debug
adb shell am start -n jp.xenia.emulator.github.debug/jp.xenia.emulator.EmulatorActivity --es gpu vulkan --es cpu arm64 --es apu nop --es hid android --ez android_hide_osd true
adb shell input keyevent 19
adb shell input keyevent 96
adb shell input keyevent 98
adb logcat -d -v time
```

Logcat showed:

```text
Android HID: active as XInput controller 1
XeniaInput: key ... KEYCODE_DPAD_UP down/up
Android HID: key ... down/up
XeniaInput: key ... KEYCODE_BUTTON_A down/up
Android HID: key ... down/up
XeniaInput: key ... KEYCODE_BUTTON_C down/up
Android HID: key ... down/up
```

No `AndroidRuntime`, `UnsatisfiedLinkError`, or native fatal lines appeared in
the filtered validation log.

## Decision

Normal handheld emulator launches should keep using `hid=android`; scripted
captures can still force `hid=nop`.

If physical Thor controls still fail after this APK, the next diagnostic should
be a short `adb logcat` while pressing the actual built-in controls and checking
for `XeniaInput` rows. If Java logs appear but native `Android HID` rows do not,
the JNI bridge is the suspect. If neither appears, Android is routing the
controller to another app/mode, likely the Odin mouse/assistant layer.
