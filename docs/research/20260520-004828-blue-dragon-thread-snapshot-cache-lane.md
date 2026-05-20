# Blue Dragon Thread Snapshot And Cache Lane

Date: 2026-05-20 00:48 America/New_York

## Summary

This pass added a faster Thor debug loop for black-screen and idle-route
failures. The A64 profiler can now emit per-XThread guest breadcrumbs, Android
deploys preserve direct SD-card ISO access, and Thor Blue Dragon launches mount
Xbox cache devices by default.

## Implementation

- `arm64_speed_profile_thread_snapshot`
  - Routed through Android intent extras and `tools/thor/thor_xenia_debug.ps1`.
  - A64 prologues store `last_guest_function` and
    `last_guest_return_address`.
  - Speed-profile intervals log XThread handle/state plus last guest function,
    return address, LR, CTR, R1, R3, and R4.
  - The sampler retries the processor debug lock briefly, then logs
    `last_global_owner_sys_tid` and PPC global-lock owner registers if blocked.
- Storage/debug deploy
  - Debug APK declares `MANAGE_EXTERNAL_STORAGE`.
  - `thor_build.ps1` reapplies the app-op after install/deploy.
- Cache route
  - `thor_xenia_debug.ps1` now defaults `mount_cache=true`.

## Evidence

- `scratch/thor-debug/20260520-004503-*`
  - APK SHA-256: `EA375B75215C12AC84EB5E121C7FC0AC191B189EAD9BBB97F333CC49B2372EF7`
  - Thread snapshots succeeded at 15s and 30s.
  - Final `top -H` showed main guest CPU around `80.7%`, XMA decoder around
    `42.3%`, GPU commands around `19.2%`.
- `scratch/thor-debug/20260520-004656-*`
  - Same APK SHA-256.
  - `mount_cache=true`.
  - No `ResolvePath(cache:\pack)` failures were found.
  - Final interval black-idled: `entry_delta=0`, all app threads sleeping, black
    screenshot.

## Interpretation

`MountCache false` can keep the route busier, but it also leaves invalid
`cache:\pack` failures. With cache mounted, the route is cleaner and exposes the
real next wall: Blue Dragon appears to sleep after the cache/file phase. This is
now a wait/event/cache-route HLE problem to inspect with the snapshot lane and
targeted wait/event tracing, not a reason to add another speculative A64 helper
splice.

## Next Experiment

Run a cache-mounted live trace with:

- `-Arm64SpeedProfileThreadSnapshot true`
- delayed wait/event trace around the 25-45s window
- targeted log search for the final wait handles and guest LR/CTR values

Then use Ghidra only on the concrete guest PCs/objects that appear in that
runtime evidence.
