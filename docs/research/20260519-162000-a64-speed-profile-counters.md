# A64 Speed Profile Counters

Date: 2026-05-19 16:20 EDT
Target: AYN Thor Max, Blue Dragon Disc 1
Branch: `master`

## Question

Can the Thor speed lane identify where the ARM64 backend is spending guest
execution time without enabling the old per-call trace firehose?

## Change

Added an opt-in A64 speed profiler:

- `arm64_speed_profile_interval_ms`
- `arm64_speed_profile_top_functions`
- `arm64_speed_profile_min_delta`

When enabled, generated A64 code increments inline counters for:

- guest function entries
- host-to-guest thunk entries
- guest-to-host thunk calls
- direct guest calls
- indirect guest calls
- extern/HLE calls
- runtime resolve calls and misses

The profiler prints periodic warning-level summaries so it still appears in
the quiet Blue Dragon speed lane when the script raises `log_level` from `0`
to `1` for profiled runs. It is default-off.

## Validation Run

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 100 -PerfSampleSeconds "45,90" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 16 -Arm64SpeedProfileMinDelta 1
```

Capture:

- Log: `scratch/thor-debug/20260519-161600-speed-logcat.txt`
- Filtered log: `scratch/thor-debug/20260519-161600-speed-logcat-filtered.txt`
- Metadata: `scratch/thor-debug/20260519-161600-meta.txt`
- Screenshot: `scratch/thor-debug/20260519-161600-screenshot.png`
- APK SHA-256: `FA6BC83AF068D1A94D66969BD449D76DFC109B9542F61A387827F0A4F362C080`

No `AndroidRuntime`, fatal signal, `VK_ERROR_DEVICE_LOST`, GPU hung, tombstone,
or ANR line was found in the capture.

## Findings

The profiler produced periodic summaries. The final interval reported:

- `funcs=5750`
- `entry_delta=239863955`
- `h2g=3698/26037`
- `g2h=366166/3577941`
- `direct=237498961/2065602607`
- `indirect=2518769/8423002`
- `extern=357748/3495432`
- `resolves=584/5750`
- `resolve_misses=0/0`

Hottest final-interval guest functions:

- `826BFCCC '__restgprlr_29'`, delta `56,900,645`
- `826BFC7C '__savegprlr_29'`, delta `56,900,643`
- `8248B040 'sub_8248B040'`, delta `56,621,644`
- `8246B408 'sub_8246B408'`, delta `56,619,099`
- `827294CC 'sub_827294CC'`, delta `1,996,587`
- `8272A3A4 'sub_8272A3A4'`, delta `1,449,796`

The profiler confirms the current speed wall is still CPU/A64 guest dispatch
and guest call overhead, not Vulkan. The extreme count around
`__savegprlr_29`, `__restgprlr_29`, `8248B040`, and the known draw-wait
function `8246B408` gives the next concrete target.

## Decision

Keep the profiler default-off but use it as the standard next A64 speed lane.
The next optimization research should inspect the `__savegprlr_*` /
`__restgprlr_*` helper semantics and decide whether the A64 backend can inline
or special-case those compiler helpers safely.
