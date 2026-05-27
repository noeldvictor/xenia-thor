# Android App Picker and Quick Launch Proof

## Context

The Android usability loop replaced the old Blue Dragon speed loop for now. The
current user-facing problem is that launching games through the APK feels
fragile: the app picker path, game crash classes, controller mapping, FPS OSD,
and Exit to menu need to behave like an emulator frontend instead of a debug
demo.

This pass validates the real launcher/document-picker flow after the recent
SAF path fixes and adds a small frontend affordance: a `Launch Last Game` row
that remembers the last game picked through Android DocumentsUI.

## Changes

- `LauncherActivity` now remembers the last selected game URI and display name
  in `xenia_android_settings`.
- The launcher shows a disabled `Launch Last Game` card until the user picks a
  game once.
- After a successful picker selection, the card shows the selected title and can
  relaunch it without reopening DocumentsUI.
- `tools/thor/thor_codex_goal_loop.ps1` now has `-Mode EnableAndroidUsability`
  so the scratch goal-loop can be re-armed to the Android usability sprint
  without regenerating stale Blue Dragon speed instructions.
- The launcher root is now scrollable in landscape, so the Settings and Window
  Demo rows are reachable with D-pad focus instead of being clipped below the
  viewport.
- The launcher treats controller `A`, `Start`, D-pad center, and Enter as
  activation keys for the focused card.
- The in-game OSD treats controller `A`, `Start`, D-pad center, and Enter as
  activation keys for the focused menu item.
- Native Android HID now implements `GetKeystroke` instead of always returning
  `X_ERROR_EMPTY`, so Start-sensitive title screens that poll
  `XamInputGetKeystroke` can receive `KEYDOWN` / `KEYUP` events.

## Device Proof

- Branch: `blue-dragon-route-engine-reset`.
- Capture folder: `scratch/thor-debug/20260527-app-picker-proof/`.
- APK build/deploy:
  - `tools/thor/thor_build.ps1 -Mode ApkShell`
  - `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`
- Initial launcher screenshot:
  `scratch/thor-debug/20260527-app-picker-proof/launcher-initial.png`.
  The new card is visible and disabled with `Pick a game once to enable quick launch`.
- Actual Android picker path:
  `scratch/thor-debug/20260527-app-picker-proof/documentsui.png` and
  `documentsui-scrolled2.png`.
  DocumentsUI opened directly at `disk > Roms > xbox360`; Burnout Revenge was
  selected from the user-facing picker, not by an ADB direct launch.
- Picker-launched Burnout proof:
  `scratch/thor-debug/20260527-app-picker-proof/burnout-after-picker-launch.png`.
  The game reached its visible loading screen with the top-left FPS OSD visible.
- OSD proof:
  `scratch/thor-debug/20260527-app-picker-proof/burnout-osd-menu.png`.
  Back opened the Xenia Thor in-game menu; `Exit to menu` was visible.
- Exit proof:
  `scratch/thor-debug/20260527-app-picker-proof/after-exit-menu-last-game.png`.
  `dumpsys activity` reported `LauncherActivity` as the resumed activity, and
  the `Launch Last Game` row showed `Burnout Revenge (USA).iso`.
- Quick-launch proof:
  `scratch/thor-debug/20260527-app-picker-proof/burnout-after-last-game-launch.png`.
  Tapping `Launch Last Game` relaunched Burnout without reopening DocumentsUI.
- Controller launcher navigation proof:
  `scratch/thor-debug/20260527-start-ui-proof/launcher-focus-bottom.png`.
  Repeated D-pad Down moved focus through the launcher and scrolled the screen
  enough to reach the lower `Window Demo` row.
- Start input proof:
  `scratch/thor-debug/20260527-start-ui-proof/start-button-logcat.txt`.
  Synthetic Android `KEYCODE_BUTTON_START` reached Java as
  `KEYCODE_BUTTON_START->KEYCODE_BUTTON_START` and reached native Android HID
  as key down/up events.

## Log Findings

- Focused fatal marker search:
  `scratch/thor-debug/20260527-app-picker-proof/focused-fatal-markers.txt`.
- Result: `focused_fatal_marker_count=0`.
- Compatibility markers:
  `scratch/thor-debug/20260527-app-picker-proof/compatibility-markers.txt`.
- Result: `compatibility_marker_count=2`, both from
  `XAudioGetVoiceCategoryVolumeChangeMask`.

Interpretation: the app picker and frontend flow are no longer the blocker for
Burnout. Burnout still lands in the known game-compatibility/stall class, but
this proof did not show an Android app crash, Java fatal exception, native fatal
signal, or launcher return-path failure.

The Start proof verifies Android-to-native input delivery. The Lost Odyssey
black-screen sample used for this quick check did not visibly reach a title
screen or emit a `GetKeystroke` consumption log, so the remaining per-title
question is whether a game is polling state, polling keystrokes, or blocked
before input handling. The native `GetKeystroke` path is now present for the
titles that need it.

## Next

- Keep using the actual app picker path for title compatibility tests.
- Prioritize hard-abort titles first, then black/loading stalls with clean fatal
  searches.
- A useful next frontend slice is a small recent-games list plus per-title last
  status/crash note so users can tell whether a title is "launched but stuck" or
  "crashed with a known reason".
