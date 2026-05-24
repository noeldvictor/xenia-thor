# 822824F0 Cross-Span Consumer Audit

Date: 2026-05-24 06:20 EDT

## Goal

Follow up `docs/research/20260524-060827-822824f0-vector-load-provenance.md`
by auditing the cross-span consumer chain after the `822824F0-82282528`
`lvlx/lvrx` pair. The question is whether the larger
`lvlx/lvrx -> vor -> extract/splat -> vmaddfp` shape is safe enough for a
default-off function/span-gated A64 codegen experiment.

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_vector_load_join_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 822824F0 -EndGuest 82282574 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 16

powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_a64_codegen_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 822824F0 -EndGuest 82282574 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 16
```

## Result

The updated `tools/thor/thor_hir_vector_load_join_audit.ps1` now prints vector
consumer rows in addition to vector load and join rows.

For `822824F0-82282574`, the body-backed profile stays:

- `body_ticks_total=3501617`
- `child edge 82274DB0=1480443`
- `approx_exclusive_after_edges=2021174`
- `ppc_rows=34`
- `hir_rows=106`
- HIR shape: `store_context=26`, `load_context=14`, `add=13`, `and=10`,
  `load.1=6`, `extract=4`, `splat=4`, `mul_add=3`, `permute=3`,
  `load_vector_shl=2`, `or=2`

The consumer chain is now explicit:

- `82282520 lvlx vr13,r0,r11`: `ea=r30+0x14`, stores `v[13]`
- `82282528 lvrx vr0,r11,r10`: `ea=r30+0x20`, stores `v[0]`, has zero path
- `82282530 vsldoi vr0,vr0,vr0`: reloads `v[0]`, emits one `permute`
- `82282534 lvx128 vr11,r0,r9`: stack-ish vector load to `v[11]`
- `8228253C lvx128 vr9,r0,r8`: stack-ish vector load to `v[9]`
- `82282540 lvx128 vr8,r0,r7`: stack-ish vector load to `v[8]`
- `8228254C vor vr0,vr13,vr0`: joins `v[13]` and shifted `v[0]`
- `82282554 lvx128 vr10,r0,r9`: stack-ish vector load to `v[10]`
- `8228255C/82282560/82282564 vspltw`: three constant-lane
  `extract + splat` operations from the joined `v[0]`
- `82282568/8228256C/82282570 vmaddfp`: three chained `mul_add` operations
- lookahead `82282574/82282578/8228257C vspltw` and
  `82282580/82282584/82282588 stvewx` feed the already-tested stack-lane store
  region

The A64 codegen audit confirms the likely expense is not a single obvious
operation:

- `load_vector_shl=2` plus dynamic `permute=2` for `lvlx/lvrx`
- `load.1=6` for the vector loads
- three constant `extract` plus three `splat`
- three `mul_add_v128`
- the `mul_add_v128` lowering is heavy because it enters VMX FPCR handling,
  saves scratch vector inputs to the guest scratch stack, emits `fmla`, applies
  PPC NaN fixup, and flushes denormals when required

## Decision

Do not implement a quick local-only `822824F0` vector peephole.

The full `822824F0-82282574` consumer chain is self-contained enough for source
review, but it is not a narrow patch. A worthwhile speed experiment would need
to be a larger function/span-gated vector-chain lowering that preserves:

- the `lvrx` zero path;
- PPC byte-order lane semantics from `lvlx/lvrx`;
- the `vsldoi` transform before the `vor`;
- four stack-ish `lvx128` loads;
- three constant-lane splats from the joined vector;
- three VMX/PPC NaN-compatible `vmaddfp` operations;
- the downstream `stvewx` lane-store users already shown to be sensitive.

That is too broad to patch from this offline evidence alone.

## Next Step

Prefer a smaller source-review/audit before behavior changes:

1. Add a deterministic A64 generated-instruction estimate for this exact chain,
   especially `MUL_ADD_V128`, `PERMUTE_V128`, `LOAD_VECTOR_SHL`, and
   `EXTRACT/SPLAT`.
2. Compare that estimate against the larger `8228252C-822825C4` target.
3. Only then consider a default-off function/span-gated experiment, likely aimed
   at reducing `MUL_ADD_V128` overhead or carrying joined vector values across
   the local span, not at a standalone `lvlx/lvrx` peephole.

Keep these default-off outside targeted captures: local-only `822824F0`
peepholes, `arm64_blue_dragon_stvewx_stack_lane_fastpath`,
`arm64_blue_dragon_vmx_copy_loop_fastpath`, `arm64_context_promotion_gpr_livein_r1`,
broad GPR caches, CR compare/barrier fusion, CR-store elision, and broad VMX-dot.
