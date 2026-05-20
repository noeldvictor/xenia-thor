# A64 Compare-To-Context Store Peephole

Date: 2026-05-20 16:34 America/New_York

## Question

Can the A64 backend remove more Blue Dragon CR flag churn by fusing an unsigned
zero-bound compare whose only use is the immediately following
`store_context`?

## Why This Target

The current hot Blue Dragon guest function `8272A3A4` still spends time in the
byte/decompress state machine around `8272A8B4`. Its HIR contains repeated CR
flag setup like:

- `compare_ult value, 0`
- `store_context cr_flag, compare_result`

The previous unsigned zero-compare fold proved that `compare_ult value, 0` is
always false, but that fold still emitted a materialized boolean register before
the next `store_context`. That left a common two-instruction shape:

- `mov dest, 0`
- `strb dest, [context + cr_offset]`

## Change

Two related A64 emitter cleanups landed in this pass:

- `STORE_CONTEXT_I8/I16` and `STORE_LOCAL_I8/I16` now use `wzr` directly for
  masked zero constants.
- `SelectSequence` now has a narrow peephole for unsigned zero-bound compares
  that are always false and whose result has exactly one use: the immediately
  following `OPCODE_STORE_CONTEXT`. In that case it emits `strb/strh/str wzr`
  or `str xzr` directly and skips both HIR instructions.

The peephole only fires for:

- `COMPARE_ULT x, 0 -> false`
- `COMPARE_UGT 0, x -> false`

It does not rewrite true results, floating-point compares, branches, or compare
values with multiple uses.

## Baseline

Baseline after the unsigned zero-compare fold:

- Capture: `scratch/thor-debug/20260520-161344-*`
- APK SHA-256:
  `F4934E774C4CA14709F0FE228FF52CE503ED61E812722F6007E87132D9A225FB`
- Route stayed active through 70 seconds.
- Later interval: `funcs=4687`, `entry_delta=5421420`.
- `8272A3A4 code_size=12432`.
- `8272A8E8 code_size=5552`.

Intermediate zero-store-only proof:

- Capture: `scratch/thor-debug/20260520-162727-*`
- APK SHA-256:
  `A430D4C2E14701987CF21E87002AF4218002CF919CA49B684F6434784110ADF9`
- No searched native fatal markers.
- Process stayed in `EmulatorActivity`.
- Screenshot reached the Blue Dragon `Voice Language` menu.
- `8272A3A4 code_size=12432`.
- `8272A8E8 code_size=5552`.

The zero-store-only cleanup was safe, but it did not shrink the hottest known
functions because the compare false result was not a HIR constant at the
following store.

## Build

Commands:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both commands completed successfully after the peephole.

## Validation

Capture:

- `scratch/thor-debug/20260520-163134-*`
- APK SHA-256:
  `D04FA8B9BEA65A1008BAFC3C59031415E3A68CC9BC8A140DA83DBCF61EFAEA6D`
- Metadata head: `5331d9f3b` because the proof ran before this change was
  committed.
- No searched native fatal markers.
- Process stayed in `EmulatorActivity`.
- Screenshot reached the Blue Dragon `Voice Language` menu.
- First active interval: `funcs=3651`, `entry_delta=3872135`.
- Later interval: `funcs=4393`, `entry_delta=2043246`.
- Final `top -H`: Main Thread about 84.6%, XMA Decoder about 38.4%, GPU
  Commands about 15.3%.
- Thermal status: 0.

Measured code-size deltas versus the unsigned zero-compare baseline:

- `8272A3A4`: `12432 -> 12332`, saving 100 bytes.
- `8272A8E8`: `5552 -> 5520`, saving 32 bytes.

Since the pre-peephole zero-store-only capture reached the same visible menu and
kept identical hot function sizes, the measured shrink belongs to the
compare-to-context-store peephole.

## Result

Keep the change. It is a narrow, semantics-trivial generic A64 lowering cleanup,
route-proven on the Thor Max, and it directly hits the current Blue Dragon CR
context-store pattern.

This is still not a 30/60 FPS result. The final profile remains CPU/A64-heavy,
with XMA also expensive, and the next real wall likely requires either a
register-cached lowering path for tight PPC loops or a fully proven native
region fastpath for the `8272A3A4` state machine.

## Next

- Run another body-time capture after this commit if the next patch needs a
  fresh hotspot ranking.
- Do not revive the partial `8272A8B4` byte-copy splice. If we fastpath that
  region, prove the complete `8272A3A4` state machine and its resume state.
- Investigate register-caching repeated PPC GPR/CR context fields in hot loops;
  this peephole confirms context-store churn is a real cost.
