# Lever 2 Step 2b-ii-b result — fast affine-replay cull DROPS GPU frame time ~13% (content-matched), renders correctly

The fast affine matrix-recovery replay (commit 91a85f382) replaces the per-vertex ShaderInterpreter in the
cull. Device end-to-end on Blue Dragon's harbor cinematic, full cull (gpu_cull_compaction=true, no budget).

## Content-matched A/B (SAME build, rendered=1970 avg_vertices=112 - identical frame load)
| config | gpu_frame_us | source |
|---|---|---|
| OFF (gpu_cull_compaction=false) | 605663 | docs/evidence/20260603-024807-fastcull-off.txt |
| ON  (full fast cull)            | 527815 | docs/evidence/20260603-024301-fastcull-on.txt |
| **GPU drop** | **77848 us = ~12.9%** | |

- The cull dropped dropped_tris=66375 (~30% of ~222k triangles) -> ~13% GPU frame-time reduction. Consistent
  with the per-triangle binning being ~half the per-draw cost.
- **Screenshot HOLE-FREE**: the harbor (ship/sail/rigging/terrain/sky) renders correctly with the full fast
  cull - the fast matrix-recovery replay (recover M from interpreter basis, decode position + 4 dp4s/vert) is
  correct end to end. THE CULL REDUCES GPU WORK, VALIDATED.

## But net fps is DOWN (1.25 vs 1.75) - CPU cost, not GPU
cpu_issuedraw_us = 891833 (~865ms in cpu_other) vs OFF ~42701. The fast cull's CPU cost exceeds the ~78ms GPU
saving, making the frame CPU-bound. Two causes:
1. M recovery runs the ShaderInterpreter 16x per qualifying draw (spread basis) - ~250ms+.
2. INTERPRETER FALLBACK: SetupFastAffineReplay only takes the fast path for a single k_32_32_32_FLOAT(/_32x4)
   position vfetch; BD positions that are k_16_16_16_16_FLOAT (compressed half-float, common) fall back to the
   FULL per-vertex interpreter -> still correct but slow. Only draws=431 culled (fast-path engaged); many
   qualifying draws fell back / dropped 0.

## The win is PROVEN on GPU; net fps needs CPU optimization (next)
- Reduce M-recovery basis samples 16 -> 4-6 (4 independent points exactly determine an affine M).
- Handle k_16_16_16_16_FLOAT (half-float) position decode so those draws use the fast path instead of the
  slow interpreter fallback (likely the bulk of BD positions).
- NEON-vectorize the per-vertex decode + 4 dp4s (4 verts/iter).
Target: CPU cull cost << GPU time so it hides behind the GPU-bound frame -> net fps ~= GPU-bound (~528ms ->
~1.9 fps vs OFF 1.75) = the fps WIN. The ~13% GPU reduction is the headline; converting it to fps is CPU work.

## Status
The triangle cull works end to end: correct rendering + ~13% GPU frame-time reduction at content-matched
frames. gpu_cull_compaction stays default-off. Remaining: CPU-cost optimization (recovery samples, half-float
decode, NEON) to make the net frame rate positive.
