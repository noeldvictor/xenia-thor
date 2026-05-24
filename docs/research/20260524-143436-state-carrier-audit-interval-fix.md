# Blue Dragon State-Carrier Audit Interval Fix

Date: 2026-05-24 14:45 EDT

Branch: `master`

Starting commit: `4669170b1`

## Goal

Recover the intended dynamic state-carrier counter rows for the
`82282490:8228252C-822825C4` / `82282598 -> 82287788` lane before any
generated-code behavior patch.

This continues:

- `docs/research/20260524-141502-state-carrier-audit-route-capture.md`
- `docs/research/20260524-133027-8228252c-state-carrier-design-audit.md`

## Tooling Root Cause

The first rerun with the log-level fix still reached the visible opening route
but emitted no counter rows:

- `scratch/thor-debug/20260524-142827-speed-logcat.txt`
- `scratch/thor-debug/20260524-142827-speed-logcat-filtered.txt`
- `scratch/thor-debug/20260524-142827-meta.txt`
- `scratch/thor-debug/20260524-142827-screenshot.png`

Metadata recorded `log_level=1`,
`arm64_blue_dragon_state_carrier_design_audit=true`, and
`arm64_speed_profile_body_time_filter=82282490,82287788`, but
`arm64_speed_profile_interval_ms=` was blank. Source review confirmed both
the state-carrier row and body-time rows are emitted by the periodic A64 speed
profiler, so they require `arm64_speed_profile_interval_ms != 0`.

`tools/thor/thor_xenia_debug.ps1` now also defaults
`Arm64SpeedProfileIntervalMs` to `15000` when a Blue Dragon speed capture asks
for row-producing speed-profiler instrumentation and the caller did not pass an
explicit interval. Quiet no-instrumentation captures are unchanged.

## Thor Capture

APK:

```text
android/android_studio_project/app/build/outputs/apk/github/debug/app-github-debug.apk
SHA-256: 5A80DD15AF4471FABE16E7370D6CA5EB25FC8582993F0208BE98DB33EFCFA2FD
```

Command, intentionally leaving `-Arm64SpeedProfileIntervalMs` implicit to prove
the script default:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 210 -PerfSampleSeconds "60,120,180,210" -Arm64BlueDragonStateCarrierDesignAudit true -Arm64SpeedProfileBodyTimeFilter "82282490,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000
```

Capture paths:

- `scratch/thor-debug/20260524-143436-speed-logcat.txt`
- `scratch/thor-debug/20260524-143436-speed-logcat-filtered.txt`
- `scratch/thor-debug/20260524-143436-meta.txt`
- `scratch/thor-debug/20260524-143436-screenshot.png`
- `scratch/thor-debug/20260524-143436-perf-final.txt`

The final screenshot reached the visible opening sky / dragon-wing route with
the Microsoft Game Studios presentation frame. Fatal-marker search was clean
for AndroidRuntime, fatal exceptions, fatal signals, tombstones, backtraces,
SIGSEGV, asserts, ANRs, device-lost, GPU-hung, and Xenia guest fatal markers.

Metadata confirmed:

- `head=4669170b1`
- `arm64_speed_profile_interval_ms=15000`
- `arm64_blue_dragon_state_carrier_design_audit=true`
- `arm64_speed_profile_body_time_filter=82282490,82287788`
- `arm64_speed_profile_body_time_after_ms=120000`
- `log_level=1`

## Dynamic Counter Result

Final state-carrier row:

```text
f1_seed=258389/1162676
f1_read=773031/3477646
f1_helper_read=387415/1742466
f1_child_read=385616/1735180
f1_child_call=128225/576589
f1_fallback=0/0
fpscr_seed=258389/1162676
fpscr_read=807804/3640919
fpscr_dirty_write=807804/3640919
fpscr_required_writeback=262798/1182090
fpscr_call_kill=650213/2924556
fpscr_fallback=0/0
```

Final body-time rows:

```text
82282490 body_ticks_total=61133316 entries_delta=67228 ticks_per_entry=185 code_size=87224
82281D28 body_ticks_total=13198164 entries_delta=31152 ticks_per_entry=95 code_size=96908
82287788 body_ticks_total=7477399 entries_delta=259190 ticks_per_entry=6 code_size=38084
82282388 body_ticks_total=3832445 entries_delta=5940 ticks_per_entry=145 code_size=88088
82282410 body_ticks_total=3689616 entries_delta=4356 ticks_per_entry=191 code_size=88088
```

Final thread sample remained CPU/JIT plus XMA-heavy:

```text
Main Thread 103%
XMA Decoder 42.3%
GPU Commands 7.6%
```

## Interpretation

The runtime counters validate the offline shape, but they do not justify a
blind behavior patch.

- `f[1]` is dynamically large enough to keep in the design lane:
  `3,477,646` total reads and `0` fallbacks.
- The `f[1]` split is almost even between helper-preserved reads
  (`1,742,466`) and child-spanning reads (`1,735,180`), with `576,589` child
  calls. This matches the prior conclusion that a standalone f1 stack-slot
  carrier is too narrow; any next f1 work should be a parent/callee or
  function-pair design, not another repeat of the old fastpath A/B.
- `fpscr` is larger (`3,640,919` reads and dirty writes), but it is still the
  high-risk lane: `1,182,090` required writebacks and `2,924,556` call kills.
  Do not build an fpscr shortcut until a CFG-aware dirty-state carrier can
  prove exact call, exit, barrier, exception, and reader visibility.
- This capture is audit/profiler evidence, not a quiet FPS verdict.

## Decision

- Keep `arm64_blue_dragon_state_carrier_design_audit` default-off.
- Keep `arm64_blue_dragon_f1_carrier_fastpath`, `arm64_blue_dragon_stvewx_stack_lane_fastpath`,
  `arm64_blue_dragon_mul_add_v128_fastpath`, broad GPR caches, stale vector
  peepholes, CR fusion, and fpscr shortcuts default-off.
- Do not repeat the no-interval or log-level-only captures.
- Next useful slice should be an offline design/tooling pass for a broader
  `82282490 -> 82287788` state-carrier plan, especially a parent/callee
  `f[1]` carrier with explicit helper, child-call, exit, exception, barrier,
  and fallback rules, or a CFG-aware fpscr dirty-carrier audit before any speed
  A/B.
