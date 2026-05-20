# A64 Idle Snapshot Title Lane

Date: 2026-05-20 01:21 America/New_York

## Summary

Always-on A64 thread snapshots can change Blue Dragon's fragile boot-to-title
timing. This pass added an idle-only snapshot mode that waits until A64 counters
have shown activity, then logs one thread snapshot only if a later profile
interval goes flat. The goal is to capture black-idle state without touching the
early route.

## Implementation

- Added cvar `arm64_speed_profile_thread_snapshot_on_idle`.
- Routed it through:
  - Android `EmulatorActivity` intent extras.
  - `tools/thor/thor_xenia_debug.ps1` as
    `-Arm64SpeedProfileThreadSnapshotOnIdle`.
  - Capture metadata as
    `arm64_speed_profile_thread_snapshot_on_idle=...`.
- A64 backend behavior:
  - Track whether any speed-profile interval had activity.
  - Reset the one-shot idle guard on activity.
  - When a later interval has zero entries/transitions/resolves, log
    `A64 idle thread snapshot trigger` and emit the same per-XThread snapshot
    used by the full snapshot lane.

## Validation

- Build/deploy command:
  `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`.
- Result: build succeeded, APK installed, app-op was still allowed.
- APK SHA-256:
  `FB1FA6AF9EACC02755FA7E3CAFF23E57FEC15AEB6BA1048B39D3FCF456813905`.

Capture:

- `scratch/thor-debug/20260520-012015-*`
  - `LaunchBlueDragonTitleCapture`
  - `-TitleScreenshotSeconds "0"`
  - `-Arm64SpeedProfileThreadSnapshotOnIdle true`
  - regular `-Arm64SpeedProfileThreadSnapshot false`
  - screenshot shows `press START`
  - speed summaries:
    - first active interval: `entry_delta=3874568`
    - 30s window: `entry_delta=1620242`
    - 45s window: `entry_delta=1945360`
  - no `A64 idle thread snapshot trigger` line appeared because the route
    stayed active and reached title.

## Interpretation

The idle-only cvar gives us a less intrusive way to catch future black-idle
runs. In the first validation run it did not perturb the route into an idle
state and did not spam snapshots. This should be the default diagnostic when
trying to reproduce no-title black-idle behavior.

## Next

1. Keep running title captures with idle-only snapshots until a black-idle run
   occurs.
2. When it fires, compare the idle snapshot's thread PCs/handles against the
   successful `20260520-012015` title route.
3. Add targeted wait/file/cache probes only after the idle snapshot identifies
   the specific guest thread/object that stopped progress.
