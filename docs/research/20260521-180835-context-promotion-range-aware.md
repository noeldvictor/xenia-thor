# Range-Aware Context Promotion Foundation

Date: 2026-05-21 18:08 EDT

Branch: `master`

## Question

Can the compiler's existing `ContextPromotionPass` be made safer and more
useful for upcoming PPC state-cache work by tracking full context value ranges
instead of only the first byte offset?

## Background

The old pass treated a context value as if its start offset were enough to
describe it. That is too coarse for Xenia's Android ARM64 speed lane because
Blue Dragon hot paths mix GPR, CR, scalar, and VMX context traffic. A future
state cache needs correct byte-range knowledge before it can keep PPC state
live across A64 blocks or helpers.

The runtime context-traffic audit is not a good speed-route probe for
`82282490` right now. Capture `scratch/thor-debug/20260521-175626-*` enabled:

```powershell
-Arm64ContextTrafficAudit true -Arm64ContextTrafficAuditFunction 0x82282490 -Arm64ContextTrafficAuditBudget 4
```

It logged only the audit enable line, then black-idled before `82282490`
compiled or delayed body-time activated. `entry_delta=0` repeated from
17:57:14 through the end of the run, and the final screenshot was black.

## Change

Updated `src/xenia/cpu/compiler/passes/context_promotion_pass.cc` and
`src/xenia/cpu/compiler/passes/context_promotion_pass.h` so context promotion
and block-local context-store DSE are byte-range aware:

- `LOAD_CONTEXT` reuse now requires every byte in the requested range to be
  valid, to point at the same HIR value, and to match the requested load type.
- `STORE_CONTEXT` now records the full stored value range, not only the start
  offset.
- Backward store elimination now removes a store only when every byte it writes
  has already been overwritten later in the same block.
- Volatile and branch barriers still reset the per-block view.

This is intentionally conservative. It does not try to cache values across
blocks or helpers yet.

## Validation

Build and deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both passed. APK SHA:
`3A9C1EF2FB39F2DA4ACFA1B8C969A06D106439DBBFF1556D3084D1AA7A3CDCF3`.

Thor speed capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Capture `scratch/thor-debug/20260521-180212-*` reached the opening
sky/dragon-wing scene with no searched fatal markers. Final screenshot:
`scratch/thor-debug/20260521-180212-screenshot.png`.

`82282490` code size stayed `87168`, so this did not add a direct generated-code
shrink on top of the `PERMUTE_I32` zip fastpath. Delayed body-time stayed
centered on `82282490`:

- 18:04:30: `82282490 body_ticks_delta=3948868`,
  `entries_delta=18337`, `ticks_per_entry=215`, `code_size=87168`.
- 18:04:45: `82282490 body_ticks_delta=3967268`,
  `entries_delta=20757`, `ticks_per_entry=191`, `code_size=87168`.
- 18:05:00: `82282490 body_ticks_delta=10912288`,
  `entries_delta=83104`, `ticks_per_entry=131`, `code_size=87168`.
- 18:05:15: `82282490 body_ticks_delta=7246652`,
  `entries_delta=53424`, `ticks_per_entry=135`, `code_size=87168`.

Final thread sample remained CPU/A64-bound: main thread about `88.4%`, GPU
Commands about `15.3%`, and XMA Decoder about `11.5%`.

## Decision

Keep the range-aware pass as foundation. It is route-clean and makes the
existing optimizer less naive about overlapping PPC context fields, but it is
not a speed win by itself.

Next useful work should be either:

1. a compile-time, low-noise `82282490` context/VMX traffic audit that does not
   perturb the route like the runtime audit did, or
2. a real cross-block PPC state cache with explicit helper, branch, exception,
   and aliasing flush rules.

Do not use the `20260521-175626-*` runtime context-audit capture as speed
evidence. Use it only as proof that trace-heavy audit mode can perturb this
Blue Dragon route.
