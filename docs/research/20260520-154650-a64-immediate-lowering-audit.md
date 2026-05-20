# A64 Immediate-Lowering Audit

Date: 2026-05-20 15:46 EDT
Branch: `master`
Base commit during capture: `fdefa0de5`

## Question

After the I64 logical-immediate and `AND_NOT` cleanup, does Blue Dragon hot
function `8272A3A4` still contain logical-immediate constants that fall back to
`mov` plus register logical ops?

## Hypothesis

If remaining logical-constant misses exist in `8272A3A4`, an audit filtered to
that function should emit `action mov+reg` rows. Those rows would name the next
safe codegen cleanup target.

## Change

Files changed:

- `src/xenia/cpu/backend/a64/a64_backend.cc`
- `src/xenia/cpu/backend/a64/a64_sequences.cc`
- `android/android_studio_project/app/src/main/java/jp/xenia/emulator/EmulatorActivity.java`
- `tools/thor/thor_xenia_debug.ps1`

Added cvars:

- `arm64_immediate_lowering_audit`
- `arm64_immediate_lowering_audit_function`
- `arm64_immediate_lowering_audit_budget`

The A64 logical-immediate helpers now optionally log function, opcode, width,
constant side, raw immediate, masked immediate, whether it is A64 logical
immediate encodable, and the action (`logical-imm` or `mov+reg`). With the audit
disabled, generated code should match the previous default path.

The Thor debug launcher also now forwards these cvars and converts large guest
addresses such as `0x8272A3A4` into signed 32-bit text for Android `adb shell am
start --ei`, because Android rejects unsigned decimal values above
`2147483647` for integer extras.

## Validation

Build/deploy:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Thor audit capture:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 45 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64ImmediateLoweringAudit true -Arm64ImmediateLoweringAuditFunction 0x8272A3A4 -Arm64ImmediateLoweringAuditBudget 256
```

Capture:

- `scratch/thor-debug/20260520-154135-*`
- APK SHA-256:
  `CF97908B37B50BBBC46DCD7F08B5F2B984055C24C48E8AFA92740E36B2C08FB3`
- Metadata cvars:
  - `arm64_immediate_lowering_audit=true`
  - `arm64_immediate_lowering_audit_function=0x8272A3A4`
  - `arm64_immediate_lowering_audit_budget=256`
- Startup log confirmed:
  `A64 immediate lowering audit enabled: function=8272A3A4 budget=256`
- No searched fatal markers.
- Activity stayed in `EmulatorActivity`.

The capture metadata reports `head=fdefa0de5` because the audit patch itself was
uncommitted during the run. Treat the APK SHA and this research note as the
identity for this diagnostic run.

## Result

The filtered hot function emitted 68 audit rows:

- `AND_I8`: 38 rows.
- `AND_I64`: 30 rows.
- `logical_imm yes action logical-imm`: 68 rows.
- `action mov+reg`: 0 rows.

Constants observed:

- `0x000000000000000F`
- `0x0000000000000020`
- `0x000000000000003F`
- `0x0000000000000040`
- `0x0000000000000080`
- `0x00000000000000F0`
- `0x00000000000000FF`
- `0x000000000000FF00`
- `0x000000000000FFFF`
- `0x00000000FFFF0000`
- `0x00000000FFFFFF00`
- `0x00000000FFFFFFF0`
- `0x00000000FFFFFFFC`

First active interval:

- `funcs=3604`
- `entry_delta=3874470`
- `8272A3A4 code_size=12652`
- `8272A8E8 code_size=5600`

The final interval went idle (`entry_delta=0`) with the process still alive and
threads sleeping. Because this run had extra audit logging, use it as a
translation diagnostic, not as a speed comparison.

## Decision

The current `8272A3A4` logical-constant path is already clean for the audited
shapes. Do not spend the next speed pass on more logical-immediate work in this
function unless a new audit names a `mov+reg` miss.

Next useful probes:

- Run the same audit for `8272A8E8`, or all functions with a small budget, to
  find real `mov+reg` logical-constant misses outside `8272A3A4`.
- Add source/HIR identity to immediate-lowering audit rows so future misses can
  map directly to PPC/HIR locations.
- Start a different lowering family audit for shifts, rotates, selects, or
  load/store address formation if logical constants stay clean.
