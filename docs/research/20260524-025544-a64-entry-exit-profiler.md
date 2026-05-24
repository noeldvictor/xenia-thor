# A64 Entry/Exit Profiler

Date: 2026-05-24 02:55:44 -04:00

## Goal

Answer whether the current Blue Dragon `822825E0 -> 82282490` recursive wall is
mostly generated prolog, epilog, and stackpoint overhead before changing the
stack synchronization default or chasing call/prolog cost.

## Patch

Added a default-off A64 speed-profile lane:

- Cvar: `arm64_speed_profile_entry_exit_time_filter`.
- Thor flag: `-Arm64SpeedProfileEntryExitTimeFilter`.
- Android extra forwarding:
  `arm64_speed_profile_entry_exit_time_filter`.
- Metadata key:
  `arm64_speed_profile_entry_exit_time_filter`.
- Log rows:
  `A64 speed profile entry/exit top ...`.
- Parser:
  `tools/thor/thor_a64_entry_exit_report.ps1`.

The profiler is function-start filtered and honors
`arm64_speed_profile_body_time_after_ms`, so it can wait until the opening route
is stable before collecting recursive hot-path data. It records generated
prolog ticks from function entry through the body-time start boundary and
generated epilog ticks from the body-time end boundary through stackpoint pop,
host LR restore, stack deallocation, and the pre-return counter update.

## Validation

Build and deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 30 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64BlueDragonVmxCopyLoopFastpath false -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28" -Arm64SpeedProfileEntryExitTimeFilter "82282490,82281D28" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_entry_exit_report.ps1 -LogPath scratch\thor-debug\20260524-025000-speed-logcat.txt
```

Artifacts:

- Capture: `scratch/thor-debug/20260524-025000-*`
- Logcat: `scratch/thor-debug/20260524-025000-speed-logcat.txt`
- Metadata: `scratch/thor-debug/20260524-025000-meta.txt`
- Screenshot: `scratch/thor-debug/20260524-025000-screenshot.png`

Route and stability:

- Screenshot shows the visible Blue Dragon opening sky/wing route.
- Fatal-marker search was clean:
  `Fatal signal=0`, `AndroidRuntime=0`, `VK_ERROR_DEVICE_LOST=0`,
  `GPU is hung=0`.

## Result

`82282490` after delayed activation:

- Entry/exit rows: `4`
- Prolog total: `138693`
- Epilog total: `134700`
- Combined total: `273393`
- Entry rows summed entries: `186010`
- Entry/exit ticks per call: `1.47`
- Body total: `29486579`
- Body ticks per call: `158.521`
- Entry/exit share of body total: `0.93%`
- Code size: `87328`

`82281D28` after delayed activation:

- Entry/exit rows: `4`
- Prolog total: `68748`
- Epilog total: `66034`
- Combined total: `134782`
- Entry rows summed entries: `87115`
- Entry/exit ticks per call: `1.547`
- Body total: `8624393`
- Body ticks per call: `99`
- Entry/exit share of body total: `1.56%`
- Code size: `97068`

## Read

This strongly argues against stackpoint/prolog/epilog overhead being the
dominant Blue Dragon opening-route wall. The recursive call lane is still real,
but the measured generated entry/exit overhead for the filtered functions is
around one to two CNTVCT ticks per invocation, while body work is roughly
hundreds of ticks per invocation in the same delayed route intervals.

Do not globally disable `a64_enable_host_guest_stack_synchronization` from this
evidence. The earlier stack-sync-off code-size shrink remains useful, but the
runtime wall is inside the generated body/inclusive recursive work rather than
the stackpoint boundary itself.

## Next

Do not spend the next slice on another unchanged stack-sync or entry/exit A/B.
Use the new profiler only when checking a new function or route. For `82282490`,
move to lower-noise exclusive attribution inside the body: combine or subtract
block-body and call-edge data for `822825E0`, `822825C8`, and their callees, or
resume body-local state-traffic reductions with fresh body evidence.
