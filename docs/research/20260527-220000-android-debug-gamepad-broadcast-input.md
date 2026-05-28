# Android Debug Gamepad Broadcast Input

## Summary

Generic ADB keyevents are not reliable proof for Project Sylpheed native menu
input on Thor. A controlled difficulty-menu test showed that both
`adb shell input keyevent 96` and `adb shell input gamepad keyevent 96` failed
to advance the game. This slice adds a debug-only package broadcast that calls
the same native Android gamepad path as real controller input.

This is a testability patch, not a game compatibility fix.

## Patch

- Added a debug-only `DEBUG_GAMEPAD_KEY` receiver in
  `EmulatorActivity`.
- Added `tools/thor/thor_android_debug_gamepad_input.ps1`.
- The helper sends a package-scoped broadcast to
  `jp.xenia.emulator.github.debug.DEBUG_GAMEPAD_KEY`.
- Default mode sends already-mapped Xbox-style button IDs into
  `nativeOnAndroidGamepadKey`.
- `-Physical` lets the app apply the current physical-to-Xbox remap instead.
- Release builds are unaffected because the receiver is registered only when
  `BuildConfig.DEBUG` is true.

## Validation

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell`
  passed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`
  passed and installed the APK.
- APK SHA256:
  `044E11EE28797344052430C8DE0035B815A9C45196FCB03739C069AEEA6500EE`.
- PowerShell parser check for
  `tools/thor/thor_android_debug_gamepad_input.ps1` passed.
- `git diff --check` passed with only expected CRLF warnings.

## Proof Packet

Packet:
`scratch/thor-debug/20260527-215455-debug-gamepad-broadcast-verify`

Broadcast:

```text
package=jp.xenia.emulator.github.debug
action=jp.xenia.emulator.github.debug.DEBUG_GAMEPAD_KEY
button=A
key_code=96
hold_ms=120
physical_mapping=False
Broadcast completed: result=0
```

Focused log:

```text
I XeniaInput: debug-gamepad receiver registered action=jp.xenia.emulator.github.debug.DEBUG_GAMEPAD_KEY
I XeniaInput: debug-gamepad KEYCODE_BUTTON_A down holdMs=120
I XeniaInput: debug-gamepad KEYCODE_BUTTON_A up
```

Scoped status report:

```text
classification=project_sylpheed_heap_release_failures_no_crash_marker
android_runtime_count=0
native_signal_count=0
base_heap_release_count=13
physical_heap_count=13
base_heap_release_address=1DB18CC0
physical_heap_release_address=BDB18CC0
physical_heap_parent_address=1DB18CC0
```

The direct launch in this packet validates the debug input helper only. It is
not a game-library UX or compatibility claim.

## Next

Use either the physical Thor A/OK path or
`tools/thor/thor_android_debug_gamepad_input.ps1` for the next controlled
Project Sylpheed New Game repro. The useful target is a game-library route with
screen/log/status proof that captures the heap-release addresses and any scoped
native abort marker after advancing past the difficulty menu.
