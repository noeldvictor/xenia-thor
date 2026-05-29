# Burnout XAM Keyboard JNI Fix

## Problem

Burnout Revenge could reach the player-file creation route, but selecting
Create player file and accepting Autosave bounced back to the Load/Create
prompt. The user never saw the name-entry keyboard.

## Evidence

- Live repro packet:
  `scratch/thor-debug/manual-burnout-loop-20260528-222253`.
- Repro screenshots:
  - `after-start-2.png`: Load/Create prompt.
  - `after-create.png`: Autosave prompt.
  - `after-autosave-yes.png`: bounced back to Load/Create.
- Log proof in `after-autosave-logcat.txt`:
  - `XamShowKeyboardUI(... Enter Player File Name ...)`
  - `XamShowKeyboardUI: using Android IME path`
  - No `XeniaKeyboard` Java rows after the XAM call.
- Active content proof after the failed route:
  `files/content/454107DC/00000003/BurnoutRevenge/BurnoutRevenge` existed as
  a zero-byte file.

## Root Cause

`XamShowKeyboardUI` was called from an emulator/native worker thread. The
Android keyboard path asked `GetAndroidThreadJniEnv()` for a JNI environment,
but that helper only returned a thread-local value and did not attach detached
native threads to the Java VM. The Android Java overlay was never called, so
the XAM keyboard completed like a cancel/failure and Burnout looped.

## Fix

- `src/xenia/base/main_android.cc`: attach detached native threads to the Java
  VM on demand in `GetAndroidThreadJniEnv()`, store the `JNIEnv*` in the
  existing pthread key, and let the existing destructor detach non-main
  threads.
- Existing Android XAM keyboard overlay and launcher save reset tools now work
  through the real game-library launch path.

## Validation

- Builds:
  - `tools\thor\thor_build.ps1 -Mode NativeCore -DeviceSerial c3ca0370`
  - `tools\thor\thor_build.ps1 -Mode ApkShellDeploy -NoSubst -DeviceSerial c3ca0370`
- Deployed APK SHA256:
  `72F85D1BE284EB28A5ABF6CB019C719196FF4401435BCA432D26600E6DE8A0AD`
- Device proof packet:
  `scratch/thor-debug/burnout-keyboard-fix-20260528-223841`.
- Key screenshots:
  - `keyboard-after-autosave.png`: Android keyboard visible with Burnout default text.
  - `after-keyboard-done.png`: progressed to player-file slots.
  - `after-slot1.png`: progressed into the World Tour main menu.
- Log proof:
  - `XeniaKeyboard: Showing overlay keyboard title="Enter Player File Name"`
  - `XeniaKeyboard: Overlay accepted`
- Save proof in `post-slot-save-tree.txt`:
  - `files/content/454107DC/00000001/Player Files/Player Files` size `21316`.
  - `files/content/454107DC/00000001/Player Files/__thumbnail.png` size `12008`.

## Notes

The old zero-byte `00000003/BurnoutRevenge/BurnoutRevenge` temp package can
still remain after the first create step, but it no longer blocks the player
file route once the keyboard completes. Keep the launcher Reset/Back up save
data action for user recovery from bad save states.
