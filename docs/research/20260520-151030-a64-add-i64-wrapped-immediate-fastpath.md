# A64 ADD_I64 Wrapped Immediate Fastpath

Date: 2026-05-20 15:10 America/New_York

## Question

Can we safely take the useful part of the wrapped-immediate idea and avoid
materializing `0xFFFFFFFFFFFFFFFF`, `0xFFFFFFFFFFFFFFF0`, and similar constants
for 64-bit adds on the AYN Thor Blue Dragon route?

## Context

The earlier broad wrapped-immediate pass failed:

- `scratch/thor-debug/20260520-143752-*`: broad I32/I64 `ADD`/`SUB` rewrites
  black-idled the route.
- `scratch/thor-debug/20260520-144309-*`: `ADD_I32`-only also black-idled.
- `scratch/thor-debug/20260520-144638-*`: restored default backend resumed
  healthy entry deltas.

So this pass did not re-enable the broad rewrite. It first added a diagnostic
audit path, then enabled only the audited 64-bit add shape.

## Audit Hook

Added default-off cvars:

- `arm64_add_sub_imm_audit`
- `arm64_add_sub_imm_audit_function`
- `arm64_add_sub_imm_audit_budget`

Also fixed the Android ADB-extra bridge in `EmulatorActivity.java`; without the
whitelist entry the script metadata said the audit was enabled, but native C++
still saw the default value.

Audit proof:

- Capture: `scratch/thor-debug/20260520-150200-*`
- APK SHA-256:
  `1B3F57020D2EF87D219F3919658137AE2F70777FD8F2E3056FC5C80AAF58E5DB`
- Startup log:
  `A64 ADD/SUB immediate audit enabled: function=00000000 budget=128`
- Result: 128 candidate lines emitted immediately.
- Pattern: all useful candidates in the first budget were `ADD_I64` with
  `src2` constants wrapping to a small negative immediate.
- Examples:
  - `82468EF0 ADD_I64 imm 0xFFFFFFFFFFFFFFFF inverse 0x1`
  - `826C0480 ADD_I64 imm 0xFFFFFFFFFFFFFFFC inverse 0x4`
  - `8246A070 ADD_I64 imm 0xFFFFFFFFFFFFFFF0 inverse 0x10`
  - `820C1698 ADD_I64 imm 0xFFFFFFFFFFFFFF90 inverse 0x70`

This supported a narrow implementation: `ADD_I64 reg, reg, -N` can become
`SUB Xd, Xn, #N` when `N <= 4095`.

## Implementation

Added:

- `arm64_add_i64_wrapped_imm_fastpath`
- `arm64_add_i64_wrapped_imm_fastpath_function`

The fastpath checks only `ADD_I64` constants whose two's-complement inverse is a
small AArch64 immediate. It emits `sub` with the inverse and otherwise falls
back to the existing donor lowering. It has an exact guest-function filter for
one-function proofs and a script rollback flag:

```powershell
-Arm64AddI64WrappedImmFastpath false
```

## Thor Proof

Explicit fastpath-on proof:

- Capture: `scratch/thor-debug/20260520-150447-*`
- APK SHA-256:
  `22A39CCF19F56690AFF3A03054C8F45A959E5B4CBE5D97878E844ECB3F99E520`
- Startup log:
  `A64 ADD_I64 wrapped immediate fastpath enabled: function=00000000`
- No searched fatal markers.
- Process stayed in `EmulatorActivity`.
- Guest counters kept moving through 60 seconds.
- `8272A3A4 code_size=12772`.

Default-on proof:

- Capture: `scratch/thor-debug/20260520-150652-*`
- APK SHA-256:
  `E40C7E514C3C0F1E6F4F570DE74677CCE0A3D71EAC347011E744814D78FABBA4`
- Startup log confirmed the fastpath was enabled without an explicit command
  override.
- No searched fatal markers.
- Process stayed in `EmulatorActivity`.
- Guest counters kept moving through 60 seconds.
- First active interval:
  - `entry_delta=3868019`
  - `8272A3A4 code_size=12772`
  - `8272A8E8 code_size=5628`
  - `8246A928 code_size=320`
- Final interval:
  - `funcs=4394`
  - `entry_delta=2035655`
  - `direct=1806757/8552366`
  - `indirect=224842/936787`
  - `extern=444319/1431049`

Compared to the no-fastpath, no-body-time diagnostic run
`scratch/thor-debug/20260520-150004-*`, the hot `8272A3A4` function shrank from
`12852` to `12772` bytes. This is a small but real generic codegen win, not a
frame-rate breakthrough by itself.

## Decision

Keep `ADD_I64` wrapped-small-immediate lowering enabled by default.

Do not re-enable the previous broad I32/I64 `ADD`/`SUB` rewrite. The only
promoted path is the audited 64-bit add case. For rollback, pass:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -Arm64AddI64WrappedImmFastpath false
```

## Next

- Extend the audit to include source offsets or HIR instruction IDs so a future
  rewrite can be tied to exact guest code, not just function starts.
- Use the exact-function filter for `8272A3A4` if future immediate lowering
  changes touch width-sensitive ops.
- Keep `ADD_I32`, `SUB_I32`, and `SUB_I64` behind evidence until a route proof
  says they do not reproduce the black-idle failure.
- Continue looking for similar safe single-op A64 lowering wins, but judge them
  on route survival and speed counters, not code size alone.
