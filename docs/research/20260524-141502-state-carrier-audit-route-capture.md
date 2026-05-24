# Blue Dragon State-Carrier Audit Route Capture

Date: 2026-05-24 14:15 EDT

Branch: `master`

Starting commit: `93da0d55d`

## Goal

Run the first Thor route capture for the default-off
`arm64_blue_dragon_state_carrier_design_audit` probe and delayed body-time
comparators from:

- `docs/research/20260524-135034-blue-dragon-state-carrier-design-probe.md`

The intended output was the dynamic `f[1]` / `fpscr` carrier counter row plus
fresh `82282490,82287788` body-time rows, not a speed verdict.

## Thor Capture

APK installed on Thor:

```text
android/android_studio_project/app/build/outputs/apk/github/debug/app-github-debug.apk
SHA-256: 5A80DD15AF4471FABE16E7370D6CA5EB25FC8582993F0208BE98DB33EFCFA2FD
```

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode Install -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 210 -PerfSampleSeconds "60,120,180,210" -Arm64BlueDragonStateCarrierDesignAudit true -Arm64SpeedProfileBodyTimeFilter "82282490,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000
```

Capture paths:

- `scratch/thor-debug/20260524-140649-speed-logcat.txt`
- `scratch/thor-debug/20260524-140649-speed-logcat-filtered.txt`
- `scratch/thor-debug/20260524-140649-meta.txt`
- `scratch/thor-debug/20260524-140649-screenshot.png`
- `scratch/thor-debug/20260524-140649-perf-final.txt`

The final screenshot reached the visible opening sky / dragon-wing route with
the Microsoft Game Studios presentation frame. The fatal-marker search was
clean for AndroidRuntime, fatal exceptions, fatal signals, tombstones,
backtraces, SIGSEGV, asserts, ANRs, device-lost, GPU-hung, and Xenia guest
fatal markers.

## Result

This was a route-clean capture for the probe APK, but it did not produce useful
carrier/body-time data:

- `scratch/thor-debug/20260524-140649-meta.txt` recorded
  `arm64_blue_dragon_state_carrier_design_audit=true`,
  `arm64_speed_profile_body_time_filter=82282490,82287788`, and
  `arm64_speed_profile_body_time_after_ms=120000`.
- The same metadata recorded `log_level=0`.
- The log files did not contain the
  `A64 Blue Dragon state-carrier design audit` row.
- The log files also did not contain `82282490` or `82287788` body-time rows.

Thread sample at capture end still matches the existing speed picture:

```text
Main Thread 103%
XMA Decoder 50.0%
GPU Commands 3.8%
GPU VSync 3.8%
Draw Thread 0.0%
```

This keeps the current wall CPU/JIT plus XMA-heavy, not GPU-heavy. Android UI
frame stats in this lane are still not reliable game FPS.

## Tooling Fix

The missing rows were caused by the speed-lane default `log_level=0`: the probe
row is emitted with warning-level logging and the speed-profile body rows need
the instrumentation log lane. The speed capture already lifted to
`LogLevel=1` when `-Arm64SpeedProfileIntervalMs` was present, but this run used
body-time filters and the state-carrier audit without a top-functions interval.

`tools/thor/thor_xenia_debug.ps1` now sets the default `LogLevel` to `1` when a
Blue Dragon speed capture requests instrumentation that emits log rows, such as
body-time filters, block/call-edge profiling, thread snapshots, or route-audit
cvars. Quiet no-instrumentation captures still default to `LogLevel=0` unless
the caller passes `-LogLevel`.

Short validation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 5 -PerfSampleSeconds "5" -Arm64BlueDragonStateCarrierDesignAudit true -Arm64SpeedProfileBodyTimeFilter "82282490,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000
```

Validation capture `scratch/thor-debug/20260524-141830-*` was intentionally too
short to reach the route or target guest rows, but it proved the script fix:
metadata recorded `arm64_blue_dragon_state_carrier_design_audit=true`,
`arm64_speed_profile_body_time_filter=82282490,82287788`, and `log_level=1`
without an explicit `-LogLevel` argument. The short validation fatal-marker
search was clean.

## Decision

- Treat `scratch/thor-debug/20260524-140649-*` as route-clean proof for the
  current APK and cvars, but not as a successful state-carrier counter capture.
- Do not patch generated-code behavior from this capture.
- Do not rerun the exact same command unchanged.
- Next useful run is the same route-stabilized state-carrier audit with the
  fixed script default or explicit `-LogLevel 1`, so the warning/body rows are
  present before any carrier behavior experiment.
