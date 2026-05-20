# A64 Unsigned Zero-Compare Fold

Date: 2026-05-20 16:16 America/New_York

## Question

Can the A64 backend remove repeated CR compare work from Blue Dragon by folding
unsigned integer comparisons whose result is forced by a zero bound?

## Why This Target

The existing Blue Dragon HIR dump contains many integer rows like:

- `compare_ult value, 0`
- `compare_ugt 0, value`

For unsigned integers:

- `value < 0` is always false;
- `value >= 0` is always true;
- `0 > value` is always false;
- `0 <= value` is always true.

These appear in CR flag setup around PPC compare lowering, including the current
hot `8272A3A4` and `8272A8E8` functions. Before this pass, the A64 backend
still emitted a `cmp` plus `cset` for these constant results.

## Change

Added a narrow integer compare helper used by the existing unsigned compare
sequence macro:

- `COMPARE_ULT x, 0` emits `0`;
- `COMPARE_UGE x, 0` emits `1`;
- `COMPARE_UGT 0, x` emits `0`;
- `COMPARE_ULE 0, x` emits `1`.

Signed, equality, floating-point, and vector compares were not changed.

## Baseline

Baseline after the zero-select pass:

- Capture: `scratch/thor-debug/20260520-160757-*`
- APK SHA-256:
  `9E56D859F4717D3F8D8FB13B6CF4407C41F4ACCC91C885632CF1778086ADD14B`
- Route stayed active through 70 seconds.
- Later interval: `funcs=4704`, `entry_delta=5447909`.
- `8272A3A4 code_size=12540`.
- `8272A8E8 code_size=5592`.

## Build

Commands:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both commands completed successfully.

## Validation

Capture:

- `scratch/thor-debug/20260520-161344-*`
- APK SHA-256:
  `F4934E774C4CA14709F0FE228FF52CE503ED61E812722F6007E87132D9A225FB`
- Metadata head: `866f050b8` because the proof ran before this change was
  committed.
- No searched native fatal markers.
- Process stayed in `EmulatorActivity`.
- Route stayed active through 70 seconds.
- First interval: `funcs=3700`, `entry_delta=3873890`.
- Later interval: `funcs=4687`, `entry_delta=5421420`.
- Final `top -H`: Main Thread about 30.7%, XMA Decoder about 3.8%, GPU
  Commands sampled at 0.0%.
- Screenshot was still black, so this remains a CPU/codegen proof.

Measured code-size deltas versus the zero-select baseline:

- `8272A3A4`: `12540 -> 12432`, saving 108 bytes.
- `8272A8E8`: `5592 -> 5552`, saving 40 bytes.

Combined with the zero-select pass, `8272A3A4` is now `12652 -> 12432`, saving
220 bytes versus the pre-select baseline.

## Result

Keep the change. This is a semantics-trivial unsigned compare fold, it hits the
known Blue Dragon hot functions, and the Thor route stayed active in the proof
capture.

## Next

- Run body-time profiling after this pair of cleanups to see whether
  `8272A3A4` still dominates wall time or whether the route has moved to
  critical-section / scheduler overhead.
- Consider adding a compile-time compare-fold audit only if future hot dumps
  show non-zero constant compare patterns worth folding.
