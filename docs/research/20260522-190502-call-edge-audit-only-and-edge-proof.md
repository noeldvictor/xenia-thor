# A64 Call-Edge Audit-Only And Edge Proof

Date: 2026-05-22 19:05 EDT

Branch: `master`

Starting HEAD: `721154bb6`

## Question

The previous control sandwich made the call-edge profiler look unsafe: controls
with delayed `82282490` body-time reached the opening route, while the
call-edge-only run black-idled and emitted no dynamic edge rows. This slice
separates three possibilities:

- the edge instrumentation itself is unsafe;
- the `arm64_speed_profile_call_edge_filter` compile/filter path perturbs the
  route even without instrumentation;
- the old edge run missed the same route-stabilizing condition as the controls:
  delayed `arm64_speed_profile_body_time_filter=82282490`.

## Change

Added default-off `arm64_speed_profile_call_edge_audit_only`.

When `arm64_speed_profile_call_edge_filter` is set and audit-only is true, the
A64 backend logs matching function compile-time slot counts but does not emit
generated call-edge counters and does not enable call-edge body-time accounting.

Example log:

```text
A64 call-edge compile audit: fn 82282490 'sub_82282490' blocks=164 direct_call_edges=60 instrumentation=0
```

Android and Thor plumbing:

- Java forwarder: `arm64_speed_profile_call_edge_audit_only`
- Thor flag: `-Arm64SpeedProfileCallEdgeAuditOnly`
- metadata field: `arm64_speed_profile_call_edge_audit_only=...`
- parser section: `## Call-Edge Compile Audit Rows`

## Captures

### Audit-only with delayed body-time filter

Capture: `scratch/thor-debug/20260522-184910-*`

Command shape:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileCallEdgeFilter "82282490" -Arm64SpeedProfileCallEdgeAuditOnly true -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Result:

- Screenshot: `scratch/thor-debug/20260522-184910-screenshot.png`
- Visible route: opening sky/wing route
- APK SHA-256: `7F6A7401F1185138726207D2C5EF5B9D9D2081BD0CA25ACD649B16B9AF9FC0CF`
- No searched fatal markers
- Compile audit: `blocks=164`, `direct_call_edges=60`, `instrumentation=0`
- Body-time rows reached `82282490` at code size `87168`

Late body rows:

```text
18:51:27 body_ticks_delta=3614795 entries_delta=13336 ticks_per_entry=271 code_size=87168
18:51:42 body_ticks_delta=4128079 entries_delta=20004 ticks_per_entry=206 code_size=87168
18:51:57 body_ticks_delta=6914003 entries_delta=52307 ticks_per_entry=132 code_size=87168
18:52:12 body_ticks_delta=10217637 entries_delta=68264 ticks_per_entry=149 code_size=87168
```

Interpretation: compile/filter audit alone is route-safe when paired with the
same delayed body-time filter used by the controls.

### Audit-only without body-time filter

Capture: `scratch/thor-debug/20260522-185308-*`

Command shape:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileCallEdgeFilter "82282490" -Arm64SpeedProfileCallEdgeAuditOnly true -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Result:

- Screenshot: `scratch/thor-debug/20260522-185308-screenshot.png`
- Visible route: black-idle
- APK SHA-256: `7F6A7401F1185138726207D2C5EF5B9D9D2081BD0CA25ACD649B16B9AF9FC0CF`
- No searched fatal markers
- No compile-audit row for `82282490`
- Counters went flat from 18:53:55 onward with `entry_delta=0`

Interpretation: the old no-body edge run was missing route-stabilizing context.
This run did not reach `82282490`, so it cannot prove instrumentation risk.

### Real edge profiler with delayed body-time filter

Capture: `scratch/thor-debug/20260522-185715-*`

Command shape:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileCallEdgeFilter "82282490" -Arm64SpeedProfileCallEdgeAuditOnly false -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Result:

- Screenshot: `scratch/thor-debug/20260522-185715-screenshot.png`
- Visible route: opening sky/wing route
- APK SHA-256: `7F6A7401F1185138726207D2C5EF5B9D9D2081BD0CA25ACD649B16B9AF9FC0CF`
- No searched fatal markers
- Body-time rows reached `82282490` at code size `92056`
- Dynamic edge rows emitted

Dynamic edge rows from `tools/thor/thor_hir_call_path_report.ps1`:

```text
edge=4 block_guest=822825E0 target=0x82282490 calls_total=134001 calls_peak_delta=56745 body_ticks_total=21299726 body_ticks_peak_delta=7315115 ticks_per_call_peak=285
edge=3 block_guest=822825C8 target=0x8227FEE8 calls_total=10527 calls_peak_delta=3548 body_ticks_total=4515376 body_ticks_peak_delta=1933191 ticks_per_call_peak=544
edge=2 block_guest=8228252C target=0x82287788 calls_total=604815 calls_peak_delta=264240 body_ticks_total=2716638 body_ticks_peak_delta=1226274 ticks_per_call_peak=4
edge=1 block_guest=822824F0 target=0x82274DB0 calls_total=604815 calls_peak_delta=264240 body_ticks_total=706394 body_ticks_peak_delta=319560 ticks_per_call_peak=1
```

Interpretation: with the known route-stable body-time filter, real call-edge
instrumentation is usable for target selection. The next bottleneck is not the
stale `822824F0` `stvewx` path. The dynamic wall is the recursive
`822825E0 -> 82282490` edge, followed by `822825C8 -> 8227FEE8`.

## Decision

Keep both call-edge cvars default-off:

- `arm64_speed_profile_call_edge_filter`
- `arm64_speed_profile_call_edge_audit_only`

Do not rerun the old `call-edge only, no body-time filter` capture unchanged.
For future edge captures of this route, include:

```powershell
-Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000
```

Next useful speed slice:

1. Use the working edge rows to focus `8227FEE8` and/or the recursive child
   path under `822825E0 -> 82282490`.
2. Prefer a filtered HIR/body-time dump for `8227FEE8` launched with the same
   delayed `82282490` body-time route stabilizer, or add a direct callee-focused
   report that consumes the new edge rows.
3. Do not start the stale `822824F0` vector/store peephole unless a later
   body-time run makes it the wall again.

## Validation

- `tools/thor/thor_hir_call_path_report.ps1` parses compile-audit rows.
- NativeCore build passed before these captures.
- ApkShell build passed before these captures.
- FullDeploy to AYN Thor `c3ca0370` passed before these captures.
- No full-speed proof marker was written.
