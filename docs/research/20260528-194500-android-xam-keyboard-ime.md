# Android XAM Keyboard IME

## Trigger

The live Burnout Revenge packet
`scratch/thor-debug/remote-debug-20260528-193140` showed the game alive at
`SAVE/LOAD -> Enter Player File Name` with the old ImGui `KeyboardInputDialog`
drawn over the game. Status was `classification=launched_no_crash_marker`,
`title_id=454107DC`, `media_id=223759FA`, and no Android/native/guest crash
markers.

## Root Cause

`XamShowKeyboardUI` used the desktop ImGui dialog on Android. That makes text
entry depend on Xenia's render-surface input path instead of Android's IME, so
the prompt is visible but unfriendly on a handheld. The Thor buttons continue
through the Android HID path as guest gamepad input; they are not a real mobile
keyboard experience.

## Fix

- Added `WindowedAppActivity.showXamKeyboardInputDialogBlocking`.
- The Android dialog uses `AlertDialog + EditText`, selects the default text,
  applies the XAM buffer length as an Android length filter, sets IME action
  Done, and forces the soft keyboard visible over the game.
- Added `AndroidWindowedAppContext::ShowKeyboardInputDialog`.
- On Android, `XamShowKeyboardUI` now dispatches through the native Android
  dialog path while preserving the XAM system-UI notification and overlapped
  completion flow.
- Non-Android builds keep the existing ImGui keyboard dialog.

## Validation

- `git diff --check` passed with CRLF warnings only.
- `thor_build.ps1 -Mode NativeCore` passed.
- `thor_build.ps1 -Mode ApkShellDeploy -NoSubst -DeviceSerial c3ca0370`
  passed and installed to the Thor.
- APK SHA256:
  `1188C632E126A2668A14377A90D182EF3CA2C82A0527D339FCD4B88D30C378EB`.

## Follow-Up

The post-deploy direct Burnout relaunch reached the attract/title background
but did not reproduce the name prompt through debug-button input during this
slice. The next physical-control pass should hit the player-file-name prompt
again and confirm that Android shows the IME instead of the old ImGui text
field.
