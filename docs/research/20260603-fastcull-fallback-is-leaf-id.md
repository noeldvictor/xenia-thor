# Lever 2 fast cull — the fallback is LEAF/VFETCH-ID failure (not position format)

Added a fallback-format histogram (commit eceaa132a) and measured on Blue Dragon's harbor.

## Result (device)
```
fastrep[engaged=566 fb_total=938 fb_mode_fmt=0 fb_mode_n=932]
```
- 566 draws used the fast affine replay; 938 fell back to the slow interpreter.
- fb_mode_fmt = **0 = VertexFormat::kUndefined** for 932 of 938 fallbacks. setup_leaf_format_ stays kUndefined
  ONLY when SetupFastAffineReplay returns before identifying a vfetch - i.e. the position slice's register
  leaf set was 0 or >1, or the single leaf didn't match any vertex_bindings vfetch result register.
- So the fallback is **leaf/vfetch identification**, NOT an unhandled position format. The format-coverage
  plan was the wrong lever. (Harbor still HOLE-FREE - the 566 fast draws render correctly.)

## Most likely cause: the OVER-INCLUSIVE backward slice -> multiple register leaves
ComputePositionSlice's backward walk, when an op contributes, adds the reads of BOTH the vector AND scalar
result pipes unconditionally - even when only one result feeds gl_Position. That pulls scalar-pipe inputs
(often OTHER vfetch'd attributes / temps) into the slice, so read-minus-written yields >1 register leaf, and
the single-leaf fast-path setup bails. This matches the earlier ~31-ops/slice over-inclusion note.

## Fix (next): tighten the slice to follow only the needed pipe
In ComputePositionSlice, when an op is pulled in, add the reads of ONLY the result(s) actually needed: if a
needed component is in the VECTOR result, add the vector operands; if in the SCALAR result, add the scalar
operands - not both unconditionally. This shrinks the slice to the true position cone, so read-minus-written
collapses to the single vfetch'd position leaf -> the fast path engages for the bulk -> CPU drops below the
GPU-bound frame -> net fps win. (Confirm first with a finer leaf-fail breakdown: no-leaf vs multi-leaf vs
no-vfetch-match, to be sure it's multi-leaf from over-inclusion vs a vfetch-result-register mismatch.)

## Status
The cull is correct + a ~13% GPU lever; the net-fps blocker is now precisely located: the fast path bails on
leaf/vfetch ID for ~62% of draws, almost certainly because the backward slice over-includes the scalar pipe
and yields multiple leaves. Tightening the slice is the fix.
