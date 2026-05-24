# 2026-05-24 06:31 EDT - 82282490 Span Instruction Estimate

## Goal

Turn the `822824F0-82282574` vector/FMA chain audit into a source-reviewed
A64 cost estimate, then compare it against the larger `8228252C-822825C4`
local span before choosing any default-off generated-code experiment.

## Inputs

- Filtered HIR/PPC log:
  `scratch/thor-debug/20260521-170941-speed-logcat.txt`
- Body/call-edge profile log:
  `scratch/thor-debug/20260524-030450-speed-logcat.txt`
- Function: `82282490`
- Prior notes:
  - `docs/research/20260524-054204-82282490-exclusive-candidate-ranking.md`
  - `docs/research/20260524-055108-82282490-top-span-comparison.md`
  - `docs/research/20260524-060827-822824f0-vector-load-provenance.md`
  - `docs/research/20260524-062037-822824f0-cross-span-consumer-audit.md`

## Tool Update

`tools/thor/thor_hir_a64_codegen_audit.ps1` now prints a
`Source-Reviewed A64 Floor Estimate` section. This is a heuristic floor, not
an exact emitted-instruction count. It combines HIR shape counts with the
current A64 lowering source:

- `EXTRACT_I32` dynamic: TBL-control build plus `tbl` and `umov`, counted as
  11 floor instructions.
- `EXTRACT_I32` constant: lane `umov`, counted as 1.
- `SPLAT_I32`: `dup`, counted as 1.
- `LOAD_VECTOR_SHL/SHR`: two `mov`, `fmov`, `ins`, `dup/movi`, and `add/sub`,
  counted as 6.
- `PERMUTE_V128`: table/control copies, endian-control adjustment, and `tbl`,
  counted as 8.
- `MUL_ADD_V128`: source-reviewed no-denormal/no-NaN fast-path floor, counted
  as 10. Slow paths can add VMX FPCR switching, denormal flushes, stack saves,
  and PPC NaN fixup work, so this row is deliberately conservative.

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_a64_codegen_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 822824F0 -EndGuest 82282574 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 16
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_a64_codegen_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 8228252C -EndGuest 822825C4 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 16
```

## Comparison

| Span | Approx Exclusive | Estimate Floor | Exclusive Ticks / Est Instr | Top Estimated Rows |
| --- | ---: | ---: | ---: | --- |
| `822824F0-82282574` | `2021174` | `164` | `12324.23` | `mul_add_v128=30`, `store_context=25`, `permute=24`, `load_context=14`, `add=13`, `load.1=12`, `load_vector_shl=12` |
| `8228252C-822825C4` | `2876500` | `192` | `14981.77` | `extract_dynamic_i32=33`, `mul_add_v128=30`, `store_context=27`, `load_context=14`, `load.1=12`, `and=11`, `add=10` |

`822824F0-82282574` is real local work, but the estimate does not justify a
small local-only vector-load peephole. The cost is spread across the cross-span
vector control chain plus three `MUL_ADD_V128` lowerings. Patching only
`lvlx/lvrx` or only the first `vor` would miss much of the estimated work.

`8228252C-822825C4` remains the better next target by both absolute exclusive
time and estimated density. It has the larger local budget, more dynamic
`EXTRACT_I32` work, the same three `MUL_ADD_V128` lowerings, and more context
store traffic.

## Source Review Notes

- `MUL_ADD_V128` in `src/xenia/cpu/backend/a64/a64_sequences.cc` saves flushed
  sources to scratch stack, reloads `s3`, emits `fmla`, runs PPC NaN fixup, and
  may flush input/output denormals depending on the host feature flag. This is
  a plausible speed target, but a correctness shortcut must be gated and
  audited.
- `PERMUTE_V128` in `src/xenia/cpu/backend/a64/a64_seq_vector.cc` uses `tbl`
  after endian-adjusting the control vector; it is not free, but the current
  candidate span has only three permutes.
- `LOAD_VECTOR_SHL` builds a byte-control vector every time. This is
  structurally expensive, but by itself it is too small to explain the route.
- Dynamic `EXTRACT_I32` is still expensive in `8228252C-822825C4`. The previous
  two-lane `stvewx` fold was route-clean but not speed-proven, so do not simply
  repeat that A/B unchanged.

## Decision

Do not patch local-only `822824F0` vector codegen from the cross-span audit.
The next patch should target the larger `8228252C-822825C4` span or a shared
`MUL_ADD_V128` cost lane with a default-off, function/span-gated audit first.

Best next slice:

1. Add a narrow `MUL_ADD_V128` source/audit lane for `82282490` span PCs
   `82282568`, `8228256C`, and `82282570`, or
2. Improve `8228252C-822825C4` provenance around dynamic `EXTRACT_I32` at
   `82282588` and the surrounding `stvewx`/stack address shape before another
   lane-fold experiment.

Any behavior-changing fastpath should remain default-off, Blue-Dragon/function/
span gated, and backed by a route-clean audit capture before a quiet speed A/B.
