# Lever 2 Step 2b-i result — position-slice op-list extraction works (all qualifying draws replayable; ~31 ops/slice)

Device-confirmed the position-export-slice extraction (commit 59a739ed3) on Blue Dragon's heavy harbor
cinematic (screenshot verified: ships/terrain, "Microsoft Game Studios Presents", rendered correctly with
gpu_cull_max_per_frame=20).

## Result (device cull funnel)
```
cull[branch=1864 skip_dyntop=0 skip_qual=225 draws=20 ... slice_ops_sum=51155 slice_replayable=1639]
```
- ~1639 qualifying (position-affine-MVP) draws in the heavy scene.
- **slice_replayable=1639 = 100%**: every qualifying draw's position slice passed the replayable check
  (non-empty, <=64 ops, all ops affine-safe, straight-line). The extraction is sound across the whole scene.
- slice_ops_sum=51155 / 1639 = **~31 ops/slice average**. Small enough for a fast replay (~30x fewer than
  the full shader the ShaderInterpreter runs), though higher than an ideal 4-dp4 MVP - the backward slice is
  over-inclusive (it follows BOTH the vector and scalar pipes of each dual-issue op even when only one result
  is needed). Tightening that is a 2b-ii refinement, not a blocker.

## Implication for the 2b-ii replay
At ~31 ops/slice, two replay options:
- A (replay the slice ops per vertex): general, no degeneracy/solve; ~31 tight ops/vert (NEON across 4 verts).
- B (affine matrix recovery): position is affine, so recover the 4x4 M from ~5 interpreter runs/draw, then
  4 dp4s/vert - ~8x less per-vert work than A given 31-op slices, but needs the input-position vfetch
  identified + decoded + a small linear solve. The slice's LEAF registers (read-but-not-written-in-slice =
  vfetch outputs) identify the input for BOTH.
Lean A for robustness; reconsider B if the per-vert replay proves too slow.

## Both need: vfetch decode for the leaf inputs
The replay must populate the slice's leaf registers (vfetch outputs) per vertex from the vertex buffer,
using the vertex_bindings (fetch constant / format / offset / stride) - the one remaining decode piece.

## Status
2b-i DONE + validated: slices extracted, 100% replayable, ~31 ops avg. The cull also rendered the harbor
correctly at budget 20 (another no-holes confirmation). Next: 2b-ii replay kernel (approach A) + vfetch
decode, validated vs the ShaderInterpreter, then the end-to-end fps measurement.
