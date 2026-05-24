# Blue Dragon Call-Boundary Store Suppression Probe

Date: 2026-05-24 09:30 EDT

## Hypothesis

If the A64 backend skips only the `13` route-proven candidate-dead
`store_context` sites before Blue Dragon `82282490` call `82282598 -> 82287788`,
then `82282490` generated-code size or route speed may improve because those
stores are dynamically hot and not live into the direct child by the current
linear HIR audit.

## Patch

Added default-off cvar:

```text
arm64_blue_dragon_call_boundary_state_suppress_dead_stores=false
```

The cvar is title/function/PC/offset gated to Blue Dragon function `82282490`.
It suppresses only the `13` candidate-dead sites already proven by the static
and runtime audits:

- VMX: `82282534`, `8228253C`, `82282540`, `82282554`, `82282574`,
  `82282578`, `8228257C`;
- GPR: `8228252C`, `82282544`, `82282548`, `82282558`;
- FPR: `8228258C`, `82282590`.

It does not suppress the `4` live-in stores: `r[3]`, `f[1]`, `fpscr`, and
`lr`. The audit cvar can be enabled together with suppression to count
suppressed sites, but suppression-only does not emit the audit counters or
audit log rows.

## Validation

Starting committed HEAD before this patch: `087cbdc77`

Commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64BlueDragonCallBoundaryStateAudit true -Arm64BlueDragonCallBoundaryStateSuppressDeadStores true -StopAppAfterCapture true
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64BlueDragonCallBoundaryStateAudit false -Arm64BlueDragonCallBoundaryStateSuppressDeadStores false -StopAppAfterCapture true
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64BlueDragonCallBoundaryStateAudit false -Arm64BlueDragonCallBoundaryStateSuppressDeadStores true -StopAppAfterCapture true
```

Build/deploy result:

- `NativeCore` passed.
- `FullDeploy` passed and installed on `c3ca0370`.

Tested APK SHA-256:

```text
9551FA105672594452461EB4E42DE6F13FCEEC8940F064CB7B40F6AF427351FF
```

## Route-Safety Proof

Route-safety capture:

- Prefix: `scratch/thor-debug/20260524-091813-*`
- Cvars: audit `true`, suppress `true`, delayed `82282490` body-time on
- Route: visible opening sky/dragon-wing frame
- Fatal-marker search: clean, no matches

Final audit row:

```text
dead=3053440/9007635 dead_vmx=1644160/4850265
dead_gpr=939520/2771580 dead_fpr=469760/1385790 live=939520/2771580
suppressed=3053440/9007635 suppressed_vmx=1644160/4850265
suppressed_gpr=939520/2771580 suppressed_fpr=469760/1385790
```

The suppressed totals exactly equal the candidate-dead totals. Live-in stores
were still counted separately and not suppressed.

## Quiet Same-APK A/B

Both quiet captures used the same APK SHA, disabled audit/body-time/block
profiling/disassembly, reached visible opening sky/dragon-wing frames, and had
clean fatal-marker searches.

Control:

- Prefix: `scratch/thor-debug/20260524-092153-*`
- `arm64_blue_dragon_call_boundary_state_suppress_dead_stores=false`
- Final `82282490` rows:
  - `delta=69748 total=107631 code_size=86828`
  - `delta=62328 total=169959 code_size=86828`
- Final thread sample: Main Thread `92.3%`, GPU Commands `11.5%`,
  XMA Decoder `3.8%`, Draw Thread `3.8%`.

Suppression on:

- Prefix: `scratch/thor-debug/20260524-092523-*`
- `arm64_blue_dragon_call_boundary_state_suppress_dead_stores=true`
- Final `82282490` rows:
  - `delta=77168 total=116718 code_size=86768`
  - `delta=56392 total=173110 code_size=86768`
- Final thread sample: Main Thread `96.1%`, GPU Commands `15.3%`,
  XMA Decoder `7.6%`, Draw Thread `0.0%`.

The first attempted quiet pair (`20260524-090930-*` / `20260524-091252-*`) is
discarded for speed judgment because suppression still emitted atomic
suppression counters in that build. The final quiet pair above fixed that:
both final logs have zero `A64 Blue Dragon call-boundary state audit` rows.

## Decision

The store-suppression probe is route-clean and slightly shrinks generated code:
`82282490` code size `86828 -> 86768`. It does not prove a route-wide speed
win. Final entry deltas and thread samples are mixed, and the gain is too small
to justify enabling the cvar in presets.

Keep `arm64_blue_dragon_call_boundary_state_suppress_dead_stores` default-off.
Do not spend the next immediate sprint slice repeating this exact A/B. The next
useful direction is broader state/vector/FPR reduction inside
`8228252C-822825C4` or a larger state-cache/lowering change justified by fresh
offline evidence.
