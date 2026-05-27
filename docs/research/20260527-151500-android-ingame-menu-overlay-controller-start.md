# Android In-Game Menu, Overlay Cleanup, and Start Mapping

## Context

The user reported that the game screen still had content covering it, Back felt
like it crashed the app, and the physical Thor Start button did nothing. A
manual screenshot showed Project Sylpheed running under Xenia, but the Android
launch OSD was covering the game with a top title card and a bottom donor-fork
research badge.

The Cemu Thor experiment README calls out a two-panel in-game OSD with sections
for display, performance, audio, controls, controller help, and tools. For this
Xenia Thor pass, the useful product pattern is the in-game overlay and controller
help/remap entry point, not donor branding or Cemu-specific functionality.

## Changes

- Rebranded Android APK-facing strings from generic/donor wording to Xenia Thor:
  `app_name`, launch OSD badge text, and settings text.
- Removed donor-fork wording from current Android/runtime memory messages.
- Changed normal/balanced launches to hide the launch OSD by default, including
  migrated existing preferences. The Research profile can still show the launch
  OSD.
- Added immersive sticky fullscreen handling to `WindowedAppActivity`, restored
  on resume/focus.
- Added a top-left FPS overlay:
  - Persistent Settings checkbox: `Show FPS overlay`.
  - In-game menu checkbox: `Show FPS`.
  - Current source is Android `Choreographer` display callbacks so the UI has a
    live number even when the SurfaceView native invalidation path does not
    update Java. This is useful playability feedback, but it is not exact guest
    `VdSwap` FPS yet.
- Added an in-game Xenia Thor menu to `EmulatorActivity`:
  - Back toggles the menu instead of finishing the emulator activity.
  - Menu buttons: Resume, Controller map, Settings, Exit to menu.
  - Controller Help panel shows current physical-to-XInput mappings.
  - Last input panel shows what Android reported and what Xenia maps it to.
  - Exit to menu starts `LauncherActivity` and finishes `EmulatorActivity` so
    direct emulator launches also return to the Xenia Thor launcher menu.
- Added `KEYCODE_MENU` as a bindable Android key and fallback-mapped it to
  `KEYCODE_BUTTON_SELECT`, because the Thor Menu button should act as Xbox
  Back/Select.

## Evidence

- Original user-problem screenshot:
  `scratch/thor-debug/20260527-user-screen/current-screen.png`.
- Intermediate fixed launch-OSD screenshot:
  `scratch/thor-debug/20260527-user-screen/after-hide-overlay.png`.
- Final in-game menu screenshot:
  `scratch/thor-debug/20260527-user-screen/ingame-menu.png`.
- Final clean fullscreen game view:
  `scratch/thor-debug/20260527-user-screen/clean-game-view.png`.
- Final FPS badge proof:
  `scratch/thor-debug/20260527-user-screen/choreographer-fps-top-left.png`.
- Final FPS/menu/select proof:
  `scratch/thor-debug/20260527-user-screen/final-fps-menu-select-v2.png`.
- Back-before-fix log:
  `scratch/thor-debug/20260527-user-screen/back-current-logcat.txt`.
- Menu/input verification log:
  `scratch/thor-debug/20260527-user-screen/ingame-menu-logcat.txt`.
- Final menu/select verification log:
  `scratch/thor-debug/20260527-user-screen/final-fps-menu-select-v2-logcat.txt`.

## Validation

- `tools/thor/thor_build.ps1 -Mode ApkShell` passed.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` passed.
- Direct Project Sylpheed launch reached a visible game screen.
- Synthetic `KEYCODE_BUTTON_START` reached Java and native Android HID:
  `KEYCODE_BUTTON_START->KEYCODE_BUTTON_START`.
- Synthetic `KEYCODE_MENU` reached Java and native Android HID as the Select
  fallback: `KEYCODE_MENU->KEYCODE_BUTTON_SELECT`.
- Back opened the in-game menu and the process stayed alive.
- A second Back closed the menu and restored fullscreen without Android system
  bars or the launch OSD.
- The top-left FPS badge showed a live value in-game, and the in-game menu
  exposed the persistent `Show FPS` OSD setting.
- A follow-up pass added `Exit to menu` as the explicit game-exit action while
  leaving Back as the menu toggle.
- `git diff --check` passed with normal CRLF warnings only after docs.

## Follow-Up

- If the physical Thor Start button still does not register, open the in-game
  menu and watch the Last input line while pressing Start. If it reports a
  different key than `START`, bind that key to Start in Controller map.
- A later pass should expand the OSD into full sections for performance,
  graphics, audio, tools, and per-game notes. This pass intentionally kept the
  menu small and immediately useful.
- A later pass can replace the display-callback FPS badge with exact native
  present / guest `VdSwap` FPS once that counter is plumbed through JNI.
