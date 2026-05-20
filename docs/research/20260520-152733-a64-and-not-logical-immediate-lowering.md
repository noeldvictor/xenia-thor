# A64 AND_NOT Logical-Immediate Follow-Up

Date: 2026-05-20 15:27 EDT
Branch: `master`
Base commit: `b0ddddfd1`

## Question

Can the same logical-immediate lowering pattern remove scratch materialization
from `AND_NOT` with a constant second operand?

## Change

File changed:

- `src/xenia/cpu/backend/a64/a64_sequences.cc`

For:

- `AND_NOT_I8 reg, reg, const`
- `AND_NOT_I16 reg, reg, const`
- `AND_NOT_I32 reg, reg, const`

the emitter now lowers `src1 & ~const` through the existing 32-bit
`and_imm` helper. This emits direct A64 logical-immediate `AND` when the
inverted mask is encodable and preserves the old scratch fallback otherwise.

The earlier I64 logical-immediate pass already handled `AND_NOT_I64` through
the local 64-bit logical-immediate recognizer.

## Validation

Build/deploy:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Thor speed capture:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 60 -PerfSampleSeconds "45" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1
```

Capture:

- `scratch/thor-debug/20260520-152601-*`
- APK SHA-256:
  `4663F004F97C5314813479ABE9B483E5762BA3CB990C1BA0CF9B622F2DEA8FA7`
- No searched fatal markers.
- Process stayed in `EmulatorActivity`.
- Guest counters stayed active through 60 seconds.
- Final interval: `funcs=4393`, `entry_delta=2057688`.
- Final `top -H`: Main Thread about 76.9%, XMA Decoder about 42.3%, GPU
  Commands about 15.3%.

## Result

The route stayed healthy, but the current hottest code sizes did not improve
beyond the earlier I64 logical-immediate pass:

- `8272A3A4`: stayed at `code_size=12652`.
- `8272A8E8`: stayed at `code_size=5600`.

Decision: keep this as a generic codegen cleanup, not as a measured Blue Dragon
speed win. The next meaningful pass should target hot functions or add an audit
that counts which immediate-lowering helpers fire during translation.
