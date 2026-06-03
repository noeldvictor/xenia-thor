# Lever 2 Step 0 result — whole-shader affine-MVP classifier under-counts uselessly on BD (need the position-slice classifier)

Device-measured the read-only affine-MVP cull-feasibility classifier (commit 35c536c7a) on Blue
Dragon's heavy 3D opening cinematic (screenshot verified: "Microsoft Game Studios Presents" over the
ship/terrain scene, on-screen 2.0 FPS, GPU 99% @ 680MHz — a genuine GPU-bound frame, not a menu).

- Evidence: `docs/evidence/20260602-211312-affine-mvp-classifier.txt` (+ .png).
- Content-matched frame: `rendered=2247 total_vertices=290286 avg_vertices=129 gpu_frame_us=704820 guest_ms=147192`.

## Result
| metric | affine-MVP qualifying | total | fraction |
|---|---|---|---|
| draws | 703 | 2247 | **31.3 %** |
| verts | 656 | 290286 | **0.23 %** |

The qualifying draws average **0.93 host verts each** — they are the trivial UI / clear / 1-vert draws.
BD's *heavy* geometry (the 129-avg-vert draws that own the per-triangle binning cost the cull targets)
is **rejected wholesale** by this classifier.

## Why — the whole-shader test is too coarse (this is the documented under-count, confirmed)
`Shader::is_affine_mvp_candidate()` rejects a vertex shader if **any** op in the *whole* shader is a
transcendental scalar (rcp/rsq/exp/log), touches a0/predicate, is a control-flow loop, or uses texture
fetch. BD's main-geometry VS almost certainly compute lighting (`rsq` normalize) and/or fog (`rcp`) in
the **color/texcoord** path of the same shader that outputs position. The whole-shader test sees those
transcendentals and disqualifies the shader — even though its **position export** is still a plain
constant-matrix MVP that a NEON micro-kernel could reproduce bit-exactly.

So the 0.23 %-of-verts number is NOT "the cull can't help BD." It is "the whole-shader gate is the wrong
instrument." It conflates the position slice with the color/UV slice; only shaders that are trivial
*everywhere* survive, and those have no vertices worth culling.

## Decision — build the position-export-slice classifier (plan §2), not the cull yet
The cull's NEON transform only needs the backward dataflow slice of the **position export** to be
affine-MVP; the color/UV math is irrelevant to it (the cull replays position only, then backface/frustum-
tests). The next read-only step is to classify that slice specifically:
1. Find the position export (`eA`/`eM` writing oPos, i.e. the export to `kExportData` position reg).
2. Walk its backward dependency slice (which temp regs feed it, transitively).
3. Qualify iff every op in *that slice* is vfetch of position + only {kDp4,kDp3,kMad,kMul,kAdd} against
   float constants (the MVP shape), with no loop/pred/a0/texfetch/transcendental **in the slice**.
4. Count qualifying draws/verts the same way; device-measure on BD.

If the position-slice fraction of **verts** is non-trivial → the cull is viable (build the steps in
docs/research/20260602-neon-cull-impl-plan.md §3, NO-OP cull first). If it is still ~0 → BD's position
itself is non-affine (skinning via a0 palette, or vertex-texture displacement) → cull is NO-GO for BD,
report honestly and pivot.

## Kept
Whole-shader classifier stays committed (default-off, read-only, bit-identical off-path). It is a valid
*lower* bound and a cheap sanity counter; the position-slice classifier supersedes it for the cull
decision.
