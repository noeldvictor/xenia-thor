# Lever 2 Step 0b result — position-slice classifier: 22.5% of BD verts are affine-MVP (cull viable, modest ceiling; bulk is non-affine)

Device-measured the position-export-slice affine-MVP classifier (commit 4fa3e2a55) on Blue Dragon's
heavy 3D opening cinematic (screenshot verified: harbor/ships, "Microsoft Game Studios Presents",
on-screen 2.0 FPS, GPU 99% @ 680MHz — genuine GPU-bound frame).

- Evidence: `docs/evidence/20260602-213957-posslice.txt` (+ .png).
- Content-matched frame: `rendered=2194 total_vertices=289867 avg_vertices=132 gpu_frame_us=710100 guest_ms=147902` (fps≈1.73).
- NOTE: xenia chunks this very long log line into two logcat entries; the harness's single-line grep
  captured only chunk 1 (ends at `affine_mvp_verts`). The position-slice fields are in chunk 2, read
  directly via `adb logcat -d | grep affine_mvp_pos`.

## Result — the position slice qualifies 10x more verts than the whole-shader gate
| classifier | draws | % of 2194 | verts | % of 289867 |
|---|---|---|---|---|
| 0a whole-shader (`is_affine_mvp_candidate`) | 630 | 28.7 % | 6 519 | **2.25 %** |
| 0b position-slice (`is_position_affine_mvp_candidate`) | ~632 | 28.8 % | **~65 200** | **22.5 %** |

Both qualify ~630 draws, but the position-slice draws average **~103 verts** (near the frame's 132 avg)
versus the whole-shader's 10 — i.e. the position-slice classifier captures the **heavy geometry** whose
position is a clean constant-matrix transform even though the same shader's lighting/fog path uses
rsq/rcp. This is exactly what Step 0b was built to reveal, and it confirms the Step 0a verdict (the
whole-shader gate under-counts ~10x by conflating the position and color slices).

## Verdict: the CPU/NEON cull is VIABLE on BD, but its simple-MVP reach is MODEST (~22.5% of verts)
22.5 % of vertices live in draws the cheap NEON affine-MVP micro-kernel could transform bit-exactly.
Rough ceiling: cull ~56 % of those triangles (the cullable-tri oracle's earlier figure) × 22.5 %
addressable × the per-triangle share of the frame (~50 % of per-draw, the linear-fit half) ≈ **~6 % of
frame time** (fps ~1.73 -> ~1.84). Real but small for a complex, bit-exact-or-geometry-vanishes feature.

**The decisive open question: what is the other 77.5 % of verts?** The position slice is disqualified by
a0 / dynamic addressing (skinning bone-palette indexing), control-flow loop/jump/subroutine, or a
texture-fetch feeding position (vertex-texture displacement). If the bulk is **skinning** (a0 bone
palette), it is still affine *per bone* — a skinning-aware NEON kernel (Step 4) could lift the
addressable fraction toward ~80 %+, turning the cull into a ~20 %+ lever and making the whole effort
worth it. If the bulk is genuinely non-affine, the cull stays a ~6 % feature on BD.

## Decision — measure the disqualification breakdown BEFORE committing to Step 1 plumbing
The smart, cheap next step (read-only, one more classifier counter) is to bucket each non-qualifying
vertex shader by WHY its position slice failed:
`pos_disq_a0` (dynamic/skinning) | `pos_disq_loop` | `pos_disq_jump_call` | `pos_disq_texfetch` |
`pos_disq_other`. That single measurement decides the cull's true ceiling on BD:
- bulk = a0/skinning -> build the cull with a skinning-aware kernel (high payoff), or
- bulk = loops/other -> the cull is a ~6 % feature; weigh Step 1 against pivoting.

Only after that do we commit to Step 1 (compaction plumbing + VERBATIM raw-guest-index NO-OP cull, plan
docs/research/20260602-neon-cull-impl-plan.md §3). Building the big bit-exact pipeline for a possibly-6 %
ceiling without first knowing the bulk's nature would be poor ROI on the "get fast" push.

## Kept
Both classifiers stay committed (default-off, read-only, off-path bit-identical). They are the
instruments that size the cull; the disqualification-breakdown counter extends them.
