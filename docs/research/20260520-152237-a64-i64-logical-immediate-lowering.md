# A64 I64 Logical-Immediate Lowering

Date: 2026-05-20 15:22 EDT
Branch: `master`
Base commit: `dc2a0abc2`

## Question

Can we pull more useful aX360e/Edge A64 backend behavior into the Thor speed
lane by tightening generic 64-bit logical constant lowering?

## Donor Comparison

The current Thor A64 backend is already the aX360e/Edge-style donor backend plus
local Thor instrumentation and speed probes. A direct donor diff showed no
missing mature replacement for the wrapped `ADD`/`SUB` path, but it did expose
another same-family codegen gap:

- 32-bit `AND` / `OR` / `XOR` constants use helper-style lowering that emits an
  A64 logical immediate when the mask is encodable and falls back to a scratch
  register otherwise.
- 64-bit `AND` / `OR` / `XOR` / `AND_NOT` constants still always materialized
  the constant into `x0` and used a register logical op.

This is not a wholesale donor file swap. It is a small generic completion of
the donor pattern in our main backend code, avoiding local edits to the
`third_party/xbyak_aarch64` submodule.

## Change

File changed:

- `src/xenia/cpu/backend/a64/a64_sequences.cc`

Added a local A64 logical-immediate recognizer for 64-bit masks and routed:

- `AND_I64 reg, reg, const`
- `AND_NOT_I64 reg, reg, const` as `AND reg, reg, ~const`
- `OR_I64 reg, reg, const`
- `XOR_I64 reg, reg, const`

through direct A64 logical-immediate emission when the constant is encodable.
Non-encodable constants preserve the old materialize-to-`x0` fallback.

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

- `scratch/thor-debug/20260520-152048-*`
- APK SHA-256:
  `444BDC1AFDED5EEA933D8FA988FA7FB8DE77B5DB045FB28E7DCCF44E35EFDC03`
- No searched fatal markers.
- Process stayed in `EmulatorActivity`.
- Guest counters stayed active through 60 seconds.
- Final interval: `funcs=4390`, `entry_delta=2056735`.
- Final `top -H`: Main Thread about 84.6%, XMA Decoder about 46.1%, GPU
  Commands about 15.3%.

## Code-Size Effect

Compared with the previous default-on ADD_I64 wrapped-immediate proof
`scratch/thor-debug/20260520-150652-*`:

- `8272A3A4`: `12772` -> `12652` bytes.
- `8272A8E8`: `5628` -> `5600` bytes.

The route and active counters survived, so this is kept as a generic A64
lowering improvement.

## Decision

Keep the I64 logical-immediate lowering. This is a small runtime-code-size win,
not a Blue Dragon speed breakthrough by itself.

Next useful donor-style pass: keep scanning high-frequency generated functions
for generic lowering gaps that remove scratch materialization, helper calls, or
guest-to-host transitions without title-specific HIR splices.
