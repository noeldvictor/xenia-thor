# Lever 2 Step 0d result — forward-jump fix: BD affine-MVP fraction LEAPS 22.4% -> 76.4% (cull is a real ~21-28% lever; GO for Step 1)

Device-measured the split jump/call classifier that stops bailing on forward jumps (commit b1e8b79fd) on
Blue Dragon's heavy 3D opening cinematic (screenshot verified: harbor/ships, "Microsoft Game Studios
Presents", 2.0 FPS, GPU 99% @ 680MHz).

- Evidence: `docs/evidence/20260602-222435-posfwdjmp.txt` (+ .png). Breakdown via direct logcat grep
  (the line chunks across two logcat entries).
- Content-matched frame: `rendered=2187 total_vertices=291441`.

## Result — qualifying fraction LEAPS, and the bail was the entire story
| reason | verts | % of total | draws |
|---|---|---|---|
| **qualifies** (position slice is affine-MVP) | 222 650 | **76.4 %** | 1963 (89.8 %) |
| a0 / dynamic addressing (**skinning**) | 68 670 | 23.6 % | ~224 |
| backward jump | **0** | 0 % | |
| subroutine call | **0** | 0 % | |
| loop / texture-fetch | 0 | 0 % | |
| other (transcendental/predicate) | 184 | 0.06 % | |

(Sum 222650 + 68670 + 184 = 291504 ~ total.)

## What this proves
- The entire previous "subroutine/jump = 77.5%" (Step 0c) was **forward jumps**: backjump = 0, call = 0.
  BD's vertex shaders contain NO backward jumps and NO subroutine calls. The 77.5% was a pure
  analysis-bail artifact, exactly as hypothesized. Now those shaders are analyzed and their position IS a
  clean constant-matrix MVP.
- A truthful **a0 = 23.6%** now surfaces (it was hidden behind the jump bail in 0c, which checked control
  flow before the slice reason). That is the real **skinned** geometry: the disqualified a0 draws average
  ~306 verts (big character meshes), while the qualifying draws average ~113 verts (the static
  environment - ships, terrain, props).

## Verdict: GO for the cull (Step 1). It is a real lever now, not ~6%.
- Simple-MVP NEON kernel reaches **76.4% of verts** -> cull ~56% of their triangles -> ~0.764 x 0.56 x
  ~0.50 (per-triangle share of frame) ~ **21% frame-time saving** (fps ~1.75 -> ~2.1-2.2). Real and worth
  the bit-exact work.
- A later skinning-aware kernel adds the 23.6% a0 geometry -> toward the original **~28%** ceiling.

## Caveat to carry into Step 1 (kernel correctness, not classifier soundness)
The classifier is SOUND (forward jumps cause over-taint only, never under-taint, so 76.4% is a safe lower
bound on "position is affine-MVP-computable"). But "qualifies" now includes shaders whose position math
sits inside a forward-conditional region. The actual cull NEON kernel must reproduce the GPU's TAKEN path
bit-for-bit. Two safe options for Step 1: (a) restrict the kernel to draws whose position slice is
unconditional (add an "unconditional position" sub-flag), or (b) have the kernel evaluate the same
predicate. Start the NO-OP cull (verbatim raw guest indices, byte-identical) which is control-flow-agnostic,
then layer the transform on the unconditional subset first.

## Next — Step 1: NO-OP cull plumbing (plan docs/research/20260602-neon-cull-impl-plan.md §3)
Compaction buffer + index-rewrite path that, with the cull disabled/identity, emits the VERBATIM raw guest
index bytes so the frame is byte-identical (gated default-off). Device A/B must be screenshot-IDENTICAL.
Then Step 2 layers the affine-MVP NEON backface test on the qualifying, unconditional-position subset.

## Kept
All classifiers stay committed (default-off, read-only, off-path bit-identical).
