# Android Launch Status UI

## Context

The Android usability sprint is now prioritizing why games feel like they
"just crash" when launched through the APK. Previous work fixed the app-picker
URI path mismatch, added quick launch, and added an in-game OSD with Exit to
menu, but the launcher still did not preserve a plain user-facing status for
the last game attempt.

Without that status, a title compatibility failure, a native process abort, a
forced close, and an intentional OSD exit all looked similar from the menu.

## Change

- Moved the persisted last-game keys into `XeniaAndroidSettings` so the
  launcher process and emulator process use one shared contract.
- Added last-run state keys:
  - `launcher_last_run_title`
  - `launcher_last_run_target`
  - `launcher_last_run_state`
  - `launcher_last_run_started_at_ms`
  - `launcher_last_run_finished_at_ms`
- `LauncherActivity.launchGame()` records a launch as `running` before starting
  `EmulatorActivity`.
- `EmulatorActivity` also records direct ADB/deeplink launches as `running`
  when a target is present, so status works outside the app picker path too.
- `EmulatorActivity.exitGameToLauncher()` records `exited_to_menu` before
  starting the separate launcher process.
- The launcher now shows a compact "Last run" status below `Launch Last Game`:
  - `Last run: may have crashed or closed - <title>` when the previous launch
    is still marked running.
  - `Last run: exited to menu - <title>` after the explicit OSD exit path.

This is intentionally conservative. It does not claim root cause from the app
UI because Android cannot safely read focused logcat markers itself. It gives
the user an immediate clue and preserves enough title/target state for the
debug loop.

## Device Proof

- Abnormal/forced-close status proof:
  `scratch/thor-debug/20260527-launch-status-proof/last-run-maybe-crashed.png`.
- OSD Exit to menu status proof:
  `scratch/thor-debug/20260527-launch-status-proof/last-run-exited-to-menu.png`.
- OSD XML proof with `Exit to menu` bounds:
  `scratch/thor-debug/20260527-launch-status-proof/menu.xml`.

The controlled direct launch used Project Sylpheed:

`/storage/2664-21DE/roms/xbox360/Project Sylpheed - Arc of Deception (USA, Europe) (En,Ja).iso`

The first attempted ADB launch reproduced the known Android-shell quoting
hazard for filenames with parentheses. The rerun passed the full remote
`am start` command as a quoted shell string, which is the permanent pattern for
these paths.

## Validation

- `git diff --check` passed with expected CRLF replacement warnings only.
- `tools/thor/thor_build.ps1 -Mode ApkShell` passed.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` passed.
- Device screenshots proved both launcher states after deploy.

## Remaining Work

- Add a proper recent-games/status list so multiple game attempts retain their
  last known class rather than only the most recent title.
- Continue using focused host-side logcat/capture reports for root cause:
  e.g. Project Sylpheed still needs the physical-heap / guest-exception
  compatibility lane, while Burnout remains a loading/stall class.
