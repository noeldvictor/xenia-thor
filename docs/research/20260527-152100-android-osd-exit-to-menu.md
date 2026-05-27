# Android OSD Exit To Menu

## Context

The first in-game OSD replaced Back-as-finish with Back-as-menu so the user no
longer sees a normal activity finish as a crash. That left the menu missing an
obvious way to intentionally leave the game.

## Change

- Added an `Exit to menu` button to the in-game OSD.
- The action starts `LauncherActivity` with `FLAG_ACTIVITY_NEW_TASK` and
  `FLAG_ACTIVITY_CLEAR_TASK`, then finishes `EmulatorActivity`.
- An initial `CLEAR_TOP` / `SINGLE_TOP` version could resume stale DocumentsUI
  picker state from an older launcher flow; clearing the task makes Exit return
  to the Xenia Thor menu root.
- Starting the launcher directly before `finish()` was also insufficient:
  native emulator teardown briefly displayed `LauncherActivity`, then process
  death removed it and returned to Android home. An `AlarmManager`
  `PendingIntent` relaunch was then blocked by Android background-activity
  launch rules after the emulator process exited.
- The final path keeps `LauncherActivity` in a lightweight `:launcher` process.
  Exit can start the launcher directly, then native emulator teardown can kill
  the emulator process without taking the menu activity with it.
- This keeps Back reserved for opening/closing the OSD and gives the user an
  explicit game-exit path back to the Xenia Thor launcher menu.

## Validation

- `tools/thor/thor_build.ps1 -Mode ApkShell` passed.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` passed.
- Direct Project Sylpheed launch opened the in-game OSD with `Exit to menu`
  visible:
  `scratch/thor-debug/20260527-user-screen/menu-exit-game-v5.png`.
- Tapping `Exit to menu` returned to the Xenia Thor launcher:
  `scratch/thor-debug/20260527-user-screen/after-exit-to-menu-v5.png`.
- `dumpsys activity` confirmed the resumed activity was
  `jp.xenia.emulator.github.debug/jp.xenia.emulator.LauncherActivity`.
- Focused fatal-marker search found no `FATAL EXCEPTION`, `AndroidRuntime`,
  `Fatal signal`, `SIGSEGV`, or `SIGABRT` lines in
  `scratch/thor-debug/20260527-user-screen/exit-to-menu-v5-logcat.txt`.
