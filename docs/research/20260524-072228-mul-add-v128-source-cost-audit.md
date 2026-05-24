# 2026-05-24 07:22 EDT - MUL_ADD_V128 Source/Cost Audit

## Goal

Move the next `82282490` Blue Dragon speed slice away from narrow `stvewx`
lane folds and into the shared `MUL_ADD_V128` lowering used by the hot
`82282568`, `8228256C`, and `82282570` `vmaddfp` PCs.

This is an offline/source audit only. It does not change generated code and it
does not prove a speed win.

## Inputs

- Filtered HIR/PPC log:
  `scratch/thor-debug/20260521-170941-speed-logcat.txt`
- Block/call-edge profile log:
  `scratch/thor-debug/20260524-030450-speed-logcat.txt`
- Function: `82282490`
- Spans:
  - `822824F0-82282574`
  - `8228252C-822825C4`
- Source:
  - `src/xenia/cpu/backend/a64/a64_sequences.cc`
  - `src/xenia/cpu/backend/a64/a64_seq_util.h`
  - `src/xenia/cpu/backend/x64/x64_sequences.cc`

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_a64_codegen_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 8228252C -EndGuest 822825C4 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 16
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_a64_codegen_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 822824F0 -EndGuest 82282574 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 16
```

## Tool Update

`tools/thor/thor_hir_a64_codegen_audit.ps1` now prints a
`MUL_ADD_V128 Source-Cost Audit` section when the selected slice contains
`mul_add` HIR or `vmaddfp` PPC rows.

The section reports:

- exact `vmaddfp` PCs;
- the A64 source/helper anchors;
- the high-cost lowering shape;
- the denormal/NaN slow-path hazards;
- the x64 semantic warning that host FMA was intentionally disabled there;
- the required next experiment gate before any shortcut.

## Findings

Both body-backed spans hit the same three VMX FMA PCs:

| PC | PPC |
| --- | --- |
| `82282568` | `vmaddfp vr13, vr10, vr13, vr11` |
| `8228256C` | `vmaddfp vr13, vr12, vr9, vr13` |
| `82282570` | `vmaddfp vr0, vr0, vr8, vr13` |

For `8228252C-822825C4`, the span still carries:

- `body_ticks_total=8530471`
- `edge_body_total=5653971`
- `approx_exclusive=2876500`
- `mul_add_v128=3`
- `estimated_floor_total=192`
- `approx_exclusive_ticks_per_estimated_instr=14981.77`

For `822824F0-82282574`, the span carries:

- `body_ticks_total=3501617`
- `edge_body_total=1480443`
- `approx_exclusive=2021174`
- `mul_add_v128=3`
- `estimated_floor_total=164`
- `approx_exclusive_ticks_per_estimated_instr=12324.23`

## Source Review

The current A64 `MUL_ADD_V128` lowering does this:

1. Enter VMX FPCR mode through `EmitWithVmxFpcr`.
2. Copy `src3` to scratch `v3`.
3. Optionally software-flush input denormals if `kA64FZFlushesInputs` is not
   available.
4. Store `q3` to guest scratch.
5. Prepare and optionally flush `src1`/`src2` into `v0`/`v1`.
6. Store `q0` and `q1` to guest scratch.
7. Reload `src3` into `v2`.
8. Emit `fmla`.
9. Run `FixupVmxNan_V128_Fma`.
10. Optionally flush output denormals.
11. Move `v2` to the destination vector register.

The normal fast path is not just one `fmla`. Even with no per-lane NaN repair
and no software denormal flushes, it still pays for FPCR mode handling when
needed, three vector scratch stores, one vector reload, the NaN fast-path test,
and the destination move.

The slow path can be much heavier:

- software denormal flushing when the Thor feature probe does not mark
  `kA64FZFlushesInputs`;
- per-lane PPC NaN repair when any result lane is NaN;
- extra source copies depending on register allocation.

There is also a semantics warning: x64 `MUL_ADD_V128` currently avoids host FMA
because the fused `vfmadd` sequence differed from the `vmulps` plus `vaddps`
tests. A64 currently uses `fmla`. That does not mean a speed shortcut is
impossible, but it does mean the next lane must measure real route cases before
removing fixups or changing arithmetic shape.

## Decision

Do not patch `MUL_ADD_V128` behavior yet.

The next useful slice is a default-off, function/span/PC-gated runtime audit for
the three hot PCs `82282568`, `8228256C`, and `82282570`. The audit should count
how often the route actually needs:

- software denormal flushing;
- NaN fixup entry;
- per-lane NaN repair;
- FPCR mode switches versus already-in-VMX mode;
- source/dest alias copies.

If the audit shows those expensive correctness paths are cold or absent on the
Blue Dragon opening route, then a guarded fastpath can be tested. If they are
hot, the next patch should reduce state/vector traffic around
`8228252C-822825C4` instead of trying to shortcut `vmaddfp`.
