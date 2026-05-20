# Blue Dragon Title Proof Lane

Date: 2026-05-20 01:11 America/New_York

## Summary

Blue Dragon Disc 1 can reach the visible `press START` title screen on the AYN
Thor Max with the current A64 backend research build. This is a milestone, not a
compatibility claim: the route is timing-sensitive and can still black-idle
before the title loop with very similar cvars.

## Evidence

- Device: AYN Thor Max, ADB serial `c3ca0370`.
- Branch: `master`.
- HEAD during captures: `0872dd4af`.
- APK SHA-256:
  `EA375B75215C12AC84EB5E121C7FC0AC191B189EAD9BBB97F333CC49B2372EF7`.
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`.

Successful title captures:

- `scratch/thor-debug/20260520-005304-*`
  - 45s wait-only live trace from launch.
  - Screenshot shows `press START`.
  - A64 thread snapshots and wait trace were active.
- `scratch/thor-debug/20260520-010150-*`
  - 45s live capture without wait trace.
  - Screenshot shows `press START`.
  - A64 counters stayed active through the 45s interval:
    `entry_delta=1953977`.
- `scratch/thor-debug/20260520-011006-*`
  - New `LaunchBlueDragonTitleCapture` mode, timed screenshots disabled with
    `-TitleScreenshotSeconds "0"`, thread snapshots enabled.
  - Screenshot shows `press START`.
  - A64 counters stayed active through the 45s interval:
    `entry_delta=1969933`.

Black-idle comparison captures:

- `scratch/thor-debug/20260520-010454-*`,
  `scratch/thor-debug/20260520-010651-*`, and
  `scratch/thor-debug/20260520-010820-*`.
  - Similar Blue Dragon fastpath cvars.
  - Thread snapshots were off.
  - The route dropped to `entry_delta=0` by the 45s interval and screenshots
    were black.
- `scratch/thor-debug/20260520-005751-*`.
  - Speed mode auto-sent START/A and enabled forced signed 10:10:10:2 fallback.
  - Final screenshot was black, so speed mode is not a clean title-proof lane.

## Tooling Decision

Added `tools/thor/thor_xenia_debug.ps1 -Mode LaunchBlueDragonTitleCapture`.

This mode is intentionally separate from `LaunchBlueDragonSpeedCapture`:

- no auto START/A input;
- no forced signed 10:10:10:2 Vulkan fallback;
- default 45s capture;
- final screenshot only by default;
- optional `-TitleScreenshotSeconds` for timed proof frames, recorded in
  metadata.

The default `TitleScreenshotSeconds` is `0` because timed `screencap` calls can
perturb this timing-sensitive route and should be opt-in.

## Interpretation

The title milestone is real, but the route is not deterministic enough. The
successful captures continue past the `cache:\pack\item_rec.*` phase and remain
busy in the title loop, while black-idle captures stop earlier with all guest
execution counters flat. The fact that snapshot-enabled captures are more likely
to reach title suggests a timing/order bug around kernel waits, file/cache HLE,
or guest scheduling rather than a pure graphics failure.

## Next

1. Compare successful title snapshots against black-idle snapshots at the 30-45s
   window.
2. Add targeted wait/file/cache instrumentation only around the divergent phase,
   especially `cache:\pack\item_rec.*` and file thread handles.
3. Make the title route deterministic without relying on thread snapshot
   overhead.
4. Return to trace-off speed captures after the title route is stable.
