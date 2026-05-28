# Project Sylpheed Difficulty Menu Current Proof

## Context

The Project Sylpheed title/menu geometry is still fixed. The device was already
running `EmulatorActivity` when this heartbeat started, so this slice avoided a
force-stop/relaunch and captured the live state first.

## Proof

Current snapshot packet:

- `scratch/thor-debug/remote-debug-20260527-212624`
- Branch `blue-dragon-route-engine-reset`
- Commit `9c435e1e2`
- Active PID `31461`
- Screenshot: `screen.png`

The screenshot shows the Project Sylpheed `DIFFICULTY` menu rendered full-screen
with the FPS OSD visible. The game is alive and still presenting:

- `classification=project_sylpheed_heap_release_failures_no_crash_marker`
- `base_heap_release_count=17`
- `physical_heap_count=17`
- `base_heap_release_address=1DB18CC0`
- `physical_heap_release_address=BDB18CC0`
- `physical_heap_parent_address=1DB18CC0`
- `android_runtime_count=0`
- `native_signal_count=0`
- late logcat still contains `VdSwap(...)`

Two controlled remote-input packets were then captured after clearing logcat:

- `scratch/thor-debug/20260527-212744-sylpheed-difficulty-a-press`
- `scratch/thor-debug/20260527-212951-sylpheed-difficulty-gamepad-a-press`

Both used the existing live difficulty menu and recorded before/after screenshots
plus short video. Neither `adb shell input keyevent 96` nor
`adb shell input gamepad keyevent 96` advanced the game menu, and neither packet
produced new Xenia crash markers. The after screenshots still show the
difficulty menu.

## Interpretation

The heap-release issue is real, but the current live state is not crashed. ADB
keyevent injection is not a valid proof path for pressing Xbox A inside this
native game menu. The next crash proof needs either physical Thor controller
input from this screen or a better remote gamepad injection path that is proven
to reach `nativeOnAndroidGamepadKey`.

## Next

For Project Sylpheed New Game crash work, capture the screen/log first, then use
physical A/OK on Thor or build a verified remote gamepad input helper. Do not
classify the current difficulty-menu state as a crash unless a scoped native
abort or guest crash marker appears.
