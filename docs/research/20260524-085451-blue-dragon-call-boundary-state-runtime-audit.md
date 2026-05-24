# Blue Dragon Call-Boundary State Runtime Audit

Date: 2026-05-24 08:54 EDT

## Question

The offline call-boundary state audit for Blue Dragon `82282490` span
`8228252C-822825C4` said the direct child edge `82282598 -> 82287788` has
`17` pre-call `store_context` rows, only `4` of which are live into the child.
This run asks whether those exact static candidate sites are dynamically hot on
the Thor route before attempting any store-suppression behavior change.

## Patch

Added default-off cvar:

```text
arm64_blue_dragon_call_boundary_state_audit=false
```

When enabled, the A64 backend counts only exact Blue Dragon function
`82282490` PC/offset sites:

- candidate-dead VMX stores: `82282534`, `8228253C`, `82282540`, `82282554`,
  `82282574`, `82282578`, `8228257C`;
- candidate-dead GPR stores: `8228252C`, `82282544`, `82282548`, `82282558`;
- candidate-dead FPR stores: `8228258C`, `82282590`;
- live-in sanity stores: `82282550`, `82282594` (`f[1]` and `fpscr`),
  `82282598`.

No generated stores are suppressed in this patch. This is counter-only.

## Validation

Starting committed HEAD before this patch: `2a6dadb6e`

Commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64BlueDragonCallBoundaryStateAudit true -StopAppAfterCapture true
rg -n "Fatal signal|AndroidRuntime|VK_ERROR_DEVICE_LOST|GPU is hung|SIGSEGV|SIGABRT" scratch\thor-debug\20260524-084805-speed-logcat.txt
```

Build/deploy result:

- `NativeCore` passed.
- `FullDeploy` passed and installed on `c3ca0370`.

Capture:

- Prefix: `scratch/thor-debug/20260524-084805-*`
- Logcat: `scratch/thor-debug/20260524-084805-speed-logcat.txt`
- Filtered logcat: `scratch/thor-debug/20260524-084805-speed-logcat-filtered.txt`
- Metadata: `scratch/thor-debug/20260524-084805-meta.txt`
- Screenshot: `scratch/thor-debug/20260524-084805-screenshot.png`
- Final perf: `scratch/thor-debug/20260524-084805-perf-final.txt`
- APK SHA-256:
  `4E2A9728C1DDDCB4FD562843CB94CA4711EC96118992CDD44B79BCB49A40C36C`
- Route: visible opening sky/dragon-wing frame.
- Fatal-marker search: clean, no matches.

## Runtime Counters

Final audit row:

```text
05-24 08:51:07.003 A64 Blue Dragon call-boundary state audit:
dead=3206112/8778627 dead_vmx=1726368/4726953
dead_gpr=986496/2701116 dead_fpr=493248/1350558 live=986496/2701116
```

The final totals exactly match the static model:

- `dead_total=8778627 = 13 * 675279`
- `dead_vmx=4726953 = 7 * 675279`
- `dead_gpr=2701116 = 4 * 675279`
- `dead_fpr=1350558 = 2 * 675279`
- `live=2701116 = 4 * 675279`

So the route is repeatedly executing the exact call-boundary shape the offline
audit found: `13` candidate-dead stores and `4` live-in stores per dynamic
boundary hit.

Final delayed body-time rows still show the known recursive CPU/JIT wall:

```text
82282490 body_ticks_delta=10709347 body_ticks_total=32598747 entries_delta=62328 ticks_per_entry=171 code_size=88008
82281D28 body_ticks_delta=2857398 body_ticks_total=7612379 entries_delta=29736 ticks_per_entry=96 code_size=96908
82282388 body_ticks_delta=830445 body_ticks_total=2209652 entries_delta=5670 ticks_per_entry=146 code_size=88088
82282410 body_ticks_delta=799512 body_ticks_total=2126527 entries_delta=4158 ticks_per_entry=192 code_size=88088
```

Final thread sample remained CPU/JIT-heavy:

```text
Main Thread 96.1%
XMA Decoder 42.3%
GPU Commands 7.6%
Draw Thread 0.0%
```

## Decision

This is route-clean runtime proof for the audit counters, not speed proof. The
audit instrumentation itself expands generated code (`82282490` was `88008`
bytes here versus prior quiet runs around `86828`), so do not infer FPS from
this capture.

Next useful experiment is a separate default-off, Blue-Dragon/function/PC-gated
store-suppression probe for only the `13` candidate-dead sites, with counters
for suppressed stores and a route-clean proof first. Do not skip the `4`
live-in stores (`r[3]`, `f[1]`, `fpscr`, `lr`). Keep the new audit cvar
default-off and out of presets.
