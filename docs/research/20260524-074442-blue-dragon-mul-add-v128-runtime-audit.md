# Blue Dragon MUL_ADD_V128 Runtime Audit

Date: 2026-05-24 07:44 EDT

## Goal

Turn the offline `MUL_ADD_V128` source/cost finding into a default-off runtime
audit before attempting any generated-code shortcut for Blue Dragon hot PCs
`82282568`, `8228256C`, and `82282570` inside guest function `82282490`.

## Context

`docs/research/20260524-072228-mul-add-v128-source-cost-audit.md` showed that
the body-backed `8228252C-822825C4` slice hits all three `vmaddfp` PCs and is
still the larger local target:

- `approx_exclusive=2876500`
- `mul_add_v128=3`
- `approx_exclusive_ticks_per_estimated_instr=14981.77`

The offline audit also warned not to shortcut `MUL_ADD_V128` blindly. The A64
lowering does more than `fmla`: it enters VMX FPCR mode, may flush denormals in
software, saves sources for PPC NaN repair, runs a NaN fast path / per-lane
repair path, may flush output denormals, and copies the destination.

## Patch

Added default-off cvar:

```text
arm64_blue_dragon_mul_add_v128_audit
```

When enabled, the A64 `MUL_ADD_V128` emitter instruments only:

- `current_guest_function() == 0x82282490`
- guest PCs `82282568`, `8228256C`, and `82282570`

Counters emitted into the speed-profile log:

- total audited `MUL_ADD_V128`
- per-PC counts for `82282568`, `8228256C`, `82282570`
- FPCR mode switches
- software denormal-flush path eligibility
- input denormal sightings before flush
- output denormal sightings before flush
- PPC NaN-fixup entries
- PPC NaN-fixup repaired lanes
- source vector copies into scratch registers
- destination vector copies

The audit is exposed through `tools/thor/thor_xenia_debug.ps1` as:

```powershell
-Arm64BlueDragonMulAddV128Audit true
```

The cvar stays default-off in Blue Dragon title and speed presets.

## Validation

```powershell
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
```

Results:

- `git diff --check` passed.
- `thor_xenia_debug.ps1` parsed successfully.
- `NativeCore` passed for Android `arm64-v8a` and `x86_64`.

## Next Capture

Run a route-stabilized audit capture before any shortcut:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64BlueDragonMulAddV128Audit true -StopAppAfterCapture true
```

Decision gate:

- If denormal, NaN repair, FPCR switch, and copy counters are cold, design a
  default-off function/span/PC-gated fastpath.
- If any expensive path is hot, avoid a narrow FMA shortcut and return to
  broader `8228252C-822825C4` state/vector/FPR traffic reduction.
