# Thor Launcher Android HID Fix

Date: 2026-05-24 02:37:10 -04:00

## Goal

Make normal AYN Thor launcher game launches use the real Android gamepad bridge
instead of the research-only `nop` controller.

## Finding

The native Android HID bridge already exists and maps Android gamepad buttons,
sticks, triggers, and hat axes to XInput user 0. Thor-side input discovery also
still reports the built-in pad as `Odin Controller`, vendor `0x2020`, product
`0x0111`, with `KEYBOARD | GAMEPAD | JOYSTICK` classes and the expected
`Vendor_2020_Product_0111.kl` keylayout.

The broken normal-play path was `LauncherActivity`: the SAF game picker built
the `EmulatorActivity` launch cvar bundle with `hid=nop`. That made the
physical Thor pad invisible to guest `XamInput` during normal launcher-based
play tests even though the Android HID bridge was compiled and available.

## Patch

- Added `DEFAULT_EMULATOR_HID_DRIVER = "android"` to
  `LauncherActivity.java`.
- Changed normal picker game launches from `hid=nop` to the default Android HID
  driver constant.
- Left `hid=nop` unchanged in scripted Blue Dragon speed/title automation,
  where it is still useful for repeatable START/A routes.

## Validation

Commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShellDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragon -DeviceSerial c3ca0370 -HidDriver android -LogLevel 2 -HideAndroidOsd false
adb -s c3ca0370 shell input keyevent 108
adb -s c3ca0370 shell input keyevent 96
```

Artifacts:

- `scratch/thor-debug/20260524-023633-thor-controller-android-hid-info-logcat.txt`
- `scratch/thor-debug/20260524-023633-thor-controller-android-hid-info-meta.txt`

Evidence:

- `ApkShellDeploy` passed and streamed install succeeded on Thor serial
  `c3ca0370`.
- Blue Dragon launched from the known SD-card path with `hid=android`.
- Logcat showed `Android HID: active as XInput controller 1`.
- Synthetic Android START and A keyevents produced native `Android HID: key`
  down/up rows, proving the Java -> JNI -> XInput shared-state path is live.
- Fatal-marker search was clean:
  `Fatal signal=0`, `AndroidRuntime=0`, `VK_ERROR_DEVICE_LOST=0`,
  `GPU is hung=0`.

## Decision

Normal launcher-based Thor play tests should use `hid=android` by default.
Keep `hid=nop` available for deterministic automation captures only.
