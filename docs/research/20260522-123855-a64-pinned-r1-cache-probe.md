# A64 Pinned r[1] Cache Probe

Date: 2026-05-22 12:38 EDT

Branch: `master`

Starting HEAD: `bfac70461`

## Question

Can `82282490` get a lower-overhead PPC `r[1]` state cache by pinning it in a
non-allocator AArch64 register, instead of using the heavier HIR local-slot
bridge that inserted `103` stores for only `3` replaced loads?

## Implementation

Added default-off cvars:

- `arm64_context_pinned_gpr_r1`
- `arm64_context_pinned_gpr_r1_fallthrough`
- `arm64_context_pinned_gpr_r1_function`
- `arm64_context_pinned_gpr_r1_audit`

The probe uses host `x29` as a research-only pinned register for PPC `r[1]`
inside selected generated functions. The current safe shape is load-seeded:

- `LOAD_CONTEXT r[1]` loads context into `x29`, then moves to the requested
  HIR destination register.
- Later `LOAD_CONTEXT r[1]` instructions reuse `x29` only while the pin is
  still valid.
- Overlapping `STORE_CONTEXT` to `r[1]`, volatile operations, branches, and
  block boundaries invalidate the pin.
- Optional fallthrough preservation tries to keep the pin across
  single-predecessor conditional fallthrough blocks.

Android launch extras and `tools/thor/thor_xenia_debug.ps1` can route every
cvar. All new modes are default-off.

## Validation Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370

powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 130 -PerfSampleSeconds "75,115,125" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPinnedGprR1 true -Arm64ContextPinnedGprR1Fallthrough false -Arm64ContextPinnedGprR1Function 0x82282490 -Arm64ContextPinnedGprR1Audit true -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 100000 -StopAppAfterCapture true

powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 130 -PerfSampleSeconds "75,115,125" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPinnedGprR1 true -Arm64ContextPinnedGprR1Fallthrough true -Arm64ContextPinnedGprR1Function 0x82282490 -Arm64ContextPinnedGprR1Audit true -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 100000 -StopAppAfterCapture true

powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 130 -PerfSampleSeconds "75,115,125" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 100000 -StopAppAfterCapture true
```

## Evidence

Native core build passed, full deploy passed, and install succeeded on
`c3ca0370`.

Default-off control on the final APK:

- Capture: `scratch/thor-debug/20260522-123536-*`
- APK SHA-256:
  `DBB4CAD3B0AC612CFECFC46CBFEDA93A45FB55B987270F7C7F2981FF70AAB1EA`
- Cvars: `arm64_context_pinned_gpr_r1=false`,
  `arm64_context_pinned_gpr_r1_fallthrough=false`
- Screenshot: loading spinner visible.
- Fatal marker search: no matches.
- `82282490` body-time samples:
  - `entries_delta=6392`, `ticks_per_entry=3155`, `code_size=87168`
  - `entries_delta=18337`, `ticks_per_entry=258`, `code_size=87168`

Pinned `r[1]`, no fallthrough:

- Capture: `scratch/thor-debug/20260522-123918-*`
- APK SHA-256:
  `DBB4CAD3B0AC612CFECFC46CBFEDA93A45FB55B987270F7C7F2981FF70AAB1EA`
- Cvars: `arm64_context_pinned_gpr_r1=true`,
  `arm64_context_pinned_gpr_r1_fallthrough=false`,
  `arm64_context_pinned_gpr_r1_function=0x82282490`,
  `arm64_context_pinned_gpr_r1_audit=true`
- Screenshot: loading spinner visible.
- Fatal marker search: no matches.
- Audit:
  `loads/hits=107/0 pin_loads=107 pin_stores=0 resets block/branch/volatile/overlap=27/69/0/11 barrier_preserves=54 fallthrough_preserves=0`
- `82282490` body-time samples:
  - `entries_delta=4725`, `ticks_per_entry=4321`, `code_size=87596`
  - `entries_delta=15003`, `ticks_per_entry=254`, `code_size=87596`

Pinned `r[1]`, fallthrough enabled:

- Capture: `scratch/thor-debug/20260522-123232-*`
- APK SHA-256:
  `DBB4CAD3B0AC612CFECFC46CBFEDA93A45FB55B987270F7C7F2981FF70AAB1EA`
- Cvars: `arm64_context_pinned_gpr_r1=true`,
  `arm64_context_pinned_gpr_r1_fallthrough=true`,
  `arm64_context_pinned_gpr_r1_function=0x82282490`,
  `arm64_context_pinned_gpr_r1_audit=true`
- Screenshot: black screen.
- Fatal marker search: no matches.
- Guest counters went flat before `82282490` emitted, so this is not a valid
  speed proof.

Earlier same-day captures showed one black pre-route stall with pinned enabled
and one clean pinned rerun. Treat that as a route-determinism warning; judge
this probe by the clean pinned/no-fallthrough run plus the final default-off
control.

## Decision

Do not enable either pinned `r[1]` mode in Blue Dragon presets.

The load-seeded pinned register is route-clean when fallthrough is off, but it
gets `0` hits. It adds code (`87596` bytes versus the default `87168`) and does
not reduce the measured `82282490` wall.

The fallthrough-preserve variant is rejected for now because its first final-APK
capture black-stalled before reaching `82282490`.

Keep the cvars as default-off audit scaffolding only. The result is useful
because it proves emit-time pinning after A64 register allocation is still the
wrong layer for this `r[1]` pattern: every candidate load is effectively a
fresh pin after block/branch/alias resets.

## Next Action

Do not tune `x29` emit-time pinning next. Before another runtime state-cache
patch, add or run an offline report that classifies the `107` `r[1]` loads by
block, predecessor shape, branch boundary, and aliasing store. The next useful
runtime design likely needs a pre-register-allocation GPR live-in/cache model or
a narrower generated-code superpath for one proven `82282490` block cluster,
not another post-allocation emit cache.
