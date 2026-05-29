# Android FPS OSD Guest-Swap Source

## Problem

The Android FPS badge was misleading. A live user packet captured Burnout Revenge
with Xenia's guest-crash dialog visible while the badge still said `59.3 FPS`:

- `scratch/thor-debug/fps-osd-audit-20260528-230858/screen.png`
- `scratch/thor-debug/fps-osd-audit-20260528-230858/logcat.txt`

The code path confirmed the cause: `EmulatorActivity` counted Android
`Choreographer` callbacks and displayed that as emulator FPS. That measures the
activity/compositor tick, so it can stay near display refresh even when guest
execution has crashed or paused.

The same log shows the last `VdSwap` rows ending around `05-28 23:03:43`, while
the user-visible screenshot was captured later with the crash dialog still
onscreen. The badge was not measuring fresh guest frames.

## Fix

- `VulkanPerfCountersRecordIssueSwap()` now always increments the guest
  `issue_swap_count`, even when verbose Vulkan perf logging is disabled.
- Added `VulkanPerfCountersGetIssueSwapCount()`.
- Added Android JNI bridge
  `EmulatorActivity.nativeGetGuestSwapCount()`.
- `EmulatorActivity` still uses `Choreographer` as a cheap UI ticker, but the
  FPS value is now computed from native guest `VdSwap` deltas.

This keeps the overlay cheap while making the displayed number a game swap rate
instead of a UI refresh rate. If the guest stops swapping after a crash/pause,
the badge drops toward `0.0 FPS`.

## Validation

- `git diff --check` passed with CRLF warnings only.
- `NativeCore` passed.
- `ApkShellDeploy -NoSubst -DeviceSerial c3ca0370` passed.
- Deployed APK SHA256:
  `F5894FDDEC8BD0AB9A4FB8379FB1A76DB4284561155AF599993116155F7545BB`.
- Post-fix Burnout launch proof:
  `scratch/thor-debug/fps-osd-guest-swap-proof-20260528-231719`.
- Post-fix screenshot shows the badge on active Burnout movie frames:
  `screen.png`.
- Post-fix log has no `UnsatisfiedLinkError`, JNI fatal, AndroidRuntime crash,
  or scoped `E xenia` marker.
- Post-fix log contains `vdswap_lines=1953`, with recent `VdSwap` rows near the
  screenshot timestamp, matching the active `59.3 FPS` badge.

## Next

If another title reports bad FPS, capture screen and log first, then compare the
badge against fresh `VdSwap`/present evidence. The old UI-frame counter is no
longer the FPS source.
