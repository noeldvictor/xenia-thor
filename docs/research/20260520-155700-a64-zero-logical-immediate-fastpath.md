# A64 Zero Logical-Immediate Fastpath

Date: 2026-05-20 15:57 EDT
Branch: `master`
Base commit during captures: `e7197840e`

## Question

Does the new immediate-lowering audit reveal safe logical-constant cases that
still fall back to materializing a constant and doing a register operation?

## Evidence

Global audit command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 45 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64ImmediateLoweringAudit true -Arm64ImmediateLoweringAuditBudget 256
```

Before the change, capture `scratch/thor-debug/20260520-154815-*` showed:

- 256 audited logical-immediate rows.
- 210 `logical-imm` rows.
- 46 `mov+reg` rows.
- 44 of the `mov+reg` rows were `OR_I32` with immediate `0`.
- The remaining two misses were `AND_I64` masks:
  - `0x000000006001007D`
  - `0x00000000000000EF`

This made the next safe target narrow: `OR/XOR x, 0` is identity, and
`AND x, 0` is zero. These cases do not need a scratch constant register.

## Change

File changed:

- `src/xenia/cpu/backend/a64/a64_sequences.cc`

The shared logical immediate helpers now special-case zero constants before the
logical-immediate encoding test:

- `AND reg, 0` -> `mov dest, 0`
- `OR reg, 0` -> `mov dest, src`
- `XOR reg, 0` -> `mov dest, src`

The audit action field now distinguishes:

- `logical-imm`
- `mov+reg`
- `identity`
- `zero`

## Validation

Build/deploy:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Post-change audit capture:

- `scratch/thor-debug/20260520-155154-*`
- APK SHA-256:
  `8C74D16929429E4BCD6DAF732A5C81DABE266F333D5081EA5F5B7A38B3A204F8`
- Audit result:
  - 256 audited rows.
  - 210 `logical-imm` rows.
  - 44 `identity` rows, all `OR_I32`.
  - 2 `mov+reg` rows, both the earlier non-encodable `AND_I64` masks.

Quiet post-change speed capture:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 60 -PerfSampleSeconds "45" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1
```

Capture:

- `scratch/thor-debug/20260520-155321-*`
- APK SHA-256:
  `8C74D16929429E4BCD6DAF732A5C81DABE266F333D5081EA5F5B7A38B3A204F8`
- No searched fatal markers.
- Process stayed in `EmulatorActivity`.
- Final interval stayed active:
  - `funcs=4393`
  - `entry_delta=2032022`
- Final `top -H`:
  - Main Thread about 76.9%.
  - XMA Decoder about 42.3%.
  - GPU Commands about 19.2%.

The two audit captures report `head=e7197840e` because this zero-fastpath patch
was uncommitted during validation. Use the APK SHA and this research note as the
identity for the tested build.

## Result

The patch removed the audited zero-OR scratch path: the 44 `OR_I32 imm 0`
fallback rows now lower as `identity`. The quiet speed lane remained route-clean
through 60 seconds.

This is a small generic codegen cleanup, not a measured FPS fix. The remaining
logical-constant misses in the first audit budget are two `AND_I64` masks that
need separate proof before changing.

## Next

- Investigate whether `AND_I64` mask `0xEF` can be lowered as an inverted-mask
  clear, such as a `BIC`-style immediate, without touching correctness.
- Add source/HIR identity to audit rows so any future immediate miss maps back
  to one PPC/HIR instruction.
- Move to the next lowering family if logical constants stop producing
  meaningful hot misses.
