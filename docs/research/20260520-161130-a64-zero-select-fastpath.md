# A64 Zero-Select Fastpath

Date: 2026-05-20 16:11 America/New_York

## Question

Can the A64 backend remove a cheap but repeated instruction from Blue Dragon's
main shift-heavy hot function by selecting from the AArch64 zero register
instead of materializing integer zero into a scratch register before `csel`?

## Why This Target

The existing `8272A3A4` HIR dump shows many PPC variable shift patterns like:

- `shl/shr value, masked_shift`
- `select shift_too_large, 0, shifted_value`

This exists because PPC `slw` / `srw` zero the result when the shift amount is
outside the 0..31 range, while AArch64 variable 32-bit shifts naturally mask the
amount. The HIR select is therefore correct and hot.

Before this pass, every integer `SELECT` with a zero constant loaded that zero
into `w1`, `w2`, `x1`, or `x2`, then used `csel`. AArch64 can use `wzr` / `xzr`
directly as `csel` operands.

## Change

Updated integer `SELECT_I8`, `SELECT_I16`, `SELECT_I32`, and `SELECT_I64`:

- detect zero constants on either value side;
- skip the scratch-register `mov #0`;
- use `wzr` or `xzr` directly in `csel`.

No floating-point or vector select lowering was changed.

## Baseline

Baseline quiet proof after the logical-immediate zero cleanup:

- Capture: `scratch/thor-debug/20260520-155321-*`
- APK SHA-256:
  `8C74D16929429E4BCD6DAF732A5C81DABE266F333D5081EA5F5B7A38B3A204F8`
- Metadata head: `e7197840e` because the proof ran before that cleanup was
  committed.
- Route stayed active through 60 seconds.
- Final interval: `funcs=4393`, `entry_delta=2032022`.
- `8272A3A4 code_size=12652`.
- `8272A8E8 code_size=5600`.

## Build

Commands:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both commands completed successfully.

## Validation

First proof attempt:

- Capture: `scratch/thor-debug/20260520-160530-*`
- APK SHA-256:
  `9E56D859F4717D3F8D8FB13B6CF4407C41F4ACCC91C885632CF1778086ADD14B`
- Metadata head: `0eba72c66` because the proof ran before this change was
  committed.
- No searched native fatal markers.
- `8272A3A4 code_size=12540`.
- `8272A8E8 code_size=5592`.
- Bad route signature: after an early active burst and one small interval, guest
  counters went flat. Treat this capture as timing-sensitive negative evidence,
  not as the final proof.

Repeat proof:

- Capture: `scratch/thor-debug/20260520-160757-*`
- APK SHA-256:
  `9E56D859F4717D3F8D8FB13B6CF4407C41F4ACCC91C885632CF1778086ADD14B`
- Metadata head: `0eba72c66` because the proof ran before this change was
  committed.
- No searched native fatal markers.
- Process stayed in `EmulatorActivity`.
- Route stayed active through the 70-second capture.
- First interval: `funcs=3570`, `entry_delta=3820069`.
- Later interval: `funcs=4704`, `entry_delta=5447909`.
- `8272A3A4 code_size=12540`.
- `8272A8E8 code_size=5592`.
- Screenshot was still black, so this is a CPU route/codegen proof, not visible
  progress proof.

## Result

Keep the change. It is a small generic lowering cleanup with direct impact on
the repeated Blue Dragon shift/select pattern.

Measured code-size deltas:

- `8272A3A4`: `12652 -> 12540`, saving 112 bytes.
- `8272A8E8`: `5600 -> 5592`, saving 8 bytes.

The first proof attempt idled, so this is not yet a strong FPS improvement
claim. The repeat proof stayed active and showed the route can survive the new
lowering. Use future captures to judge runtime speed.

## Next

- Use block or body-time profiling on `8272A3A4` again after this cleanup.
- The shift/select HIR pattern remains a bigger opportunity, but fusing it
  requires a real HIR peephole or sequence lookahead, not another blind generic
  rewrite.
- Keep the "route proof beats code-size shrink" rule from the wrapped-addi
  failure.
