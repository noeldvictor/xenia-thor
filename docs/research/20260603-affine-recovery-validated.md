# Lever 2 Step 2b-ii(a) result — affine position recovery reproduces the interpreter EXACTLY (maxerr 0)

Validated the matrix-recovery replay foundation (commit 6786fd11a) on Blue Dragon's harbor cinematic
(screenshot verified; gpu_cull_compaction + gpu_cull_replay_validate + gpu_cull_max_per_frame=20).

## Result (device)
```
replay[affine=20 nonaffine=0 unsup=0 maxerr_milli=0]
```
- All 20 sampled qualifying draws: the recovered 4x4 affine matrix M (clip = M*[leaf_input,1], fit by
  least squares from <=16 ShaderInterpreter basis samples) reproduces the interpreter's clip positions.
- nonaffine=0, unsupported=0; maxerr_milli=0 = max relative error < 0.001 (effectively EXACT).

## What this proves
- The position slice's single REGISTER leaf is the vfetch'd input, and reading it from temp_registers()
  AFTER the full interpreter Execute gives the correct value (the color path does NOT overwrite it here -
  the earlier worry was unfounded for these draws).
- Position is EXACTLY affine in that leaf input (confirms is_position_affine_mvp_candidate operationally).
- The recovery math (regularized 4x4 normal equations + Gauss-Jordan inverse) is correct.
- Decisive: because M reproduces the interpreter to ~0 error, a cull driven by M makes the SAME backface/
  frustum decisions as the interpreter-based cull (already device-proven hole-free). So approach B is
  correct - no det-sign or precision surprise expected.

## Caveat / remaining risk for 2b-ii(b)
The validation read the leaf input from the interpreter for BOTH the basis fit and the held-out check, so it
does NOT yet test a standalone vfetch DECODE. The fast replay (2b-ii-b) recovers M from ~4-6 interpreter
basis runs (validated) but must then apply M to ALL vertices using a FAST input decode (not the interpreter)
- decoding the position attribute from the vertex buffer. For the common k_32_32_32_FLOAT position that decode
is just read-3-floats + endian swap (low risk); other formats fall back. The decode is the one remaining
unverified piece; validate it by screenshot (holes) + a held-out decode-vs-interpreter check.

## Next (Step 2b-ii-b) - the actual speed win
1. Identify the leaf register's vfetch (match the leaf reg to a vertex_bindings attribute -> fetch constant
   address/stride/offset + format).
2. Recover M per draw from ~5 interpreter basis runs (validated exact).
3. Per vertex: fast-decode the input position (float32x3 first; fallback others) + clip = M*[p,1] (4 dp4s),
   replacing the per-vertex ShaderInterpreter in BuildCulledIndexList.
4. Device end to end: full cull now cheap -> heavy scene renders, screenshot hole-free, gpu_frame_us DROPS at
   content-matched frames = THE FPS WIN. Then 2b-iii NEON-vectorize (4 verts/iter).

## Status
Approach B (affine matrix recovery) is correctness-proven (maxerr 0). Only the fast input decode + apply +
end-to-end perf measurement remain.
