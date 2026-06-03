# Lever 2 Step 0c result — the 77.5% is NOT skinning, it's JUMPS/CALLS (a conservative bail, not a real ceiling)

Device-measured the position-slice disqualification-reason breakdown (commit a7858e4c7) on Blue Dragon's
heavy 3D opening cinematic (screenshot verified: harbor/ships, "Microsoft Game Studios Presents", 2.0 FPS,
GPU 99% @ 680MHz).

- Evidence: `docs/evidence/20260602-220429-posdisq.txt` (+ .png). Breakdown from direct logcat grep (the
  line chunks): the pos fields are in logcat chunk 2.
- Content-matched frame: `rendered=2187 total_vertices=291441`.

## Result — the breakdown sums EXACTLY to total (65356 + 225901 + 184 = 291441), so it is exhaustive
| reason | verts | % of total |
|---|---|---|
| **qualifies** (position slice is affine-MVP) | 65 356 | 22.4 % |
| a0 / dynamic addressing (**skinning**) | **0** | **0 %** |
| control-flow loop | **0** | 0 % |
| **subroutine / jump** | **225 901** | **77.5 %** |
| texture-fetch feeds position (displacement) | 0 | 0 % |
| other (transcendental / predicate) | 184 | 0.06 % |

## This OVERTURNS the Step 0b verdict — the cull's ceiling is NOT 22.5 %
The non-qualifying 77.5 % is disqualified **entirely by control-flow jumps/calls** (`uses_subroutine_or_jump_`),
NOT by non-affine math: skinning a0 = 0, vertex-texture displacement = 0, transcendental/other ≈ 0.06 %.
The classifier **bailed** on those shaders because the linear taint pass is unsound under arbitrary control
transfer — it did NOT find their position to be non-affine. **22.5 % is an artifact of that bail, not a
real geometry property.** BD's heavy geometry is neither skinned nor displaced; its position is very likely
a clean MVP hidden behind a `jmp`/`call` the analysis refused to walk.

Strategic consequence: if the analysis handles jumps/calls, the affine-MVP fraction could leap toward
~99 %, restoring the cull to a real **~28 %** lever (0.99 x ~56 % cullable x ~50 % per-triangle share),
fps ~1.75 -> ~2.4 — meaningful, and it stacks with other levers. The "modest ~6 %" read from Step 0b is
retracted; it was gated by the bail, not by the hardware.

## Next — Step 0d: split jump vs call, and stop bailing on the analyzable cases
The merged `uses_subroutine_or_jump_` flag hides which it is. Split it and handle:
1. **Split** `uses_subroutine_or_jump_` -> `uses_backward_jump_` (loop-like, keep bailing) + `uses_forward_jump_`
   + `uses_subroutine_call_`. A `kCondJmp` is forward iff its target cf-index > the jump's cf-index.
2. **Stop bailing on FORWARD jumps.** The linear program-order taint pass is already conservative-safe for
   them: a forward jump only means some visited instructions may be skipped at runtime, so counting their
   taint OVER-taints (never under-taints) - safe. Only BACKWARD jumps (re-execution) and CALLS (out-of-order
   subroutine body) break soundness.
3. **Re-measure.** If the bulk is forward jumps -> the qualifying fraction leaps immediately (free win, the
   cull prize is ~28 %). If it is CALLS -> handle subroutines next (inline the call: walk the callee body at
   the call site during the taint pass, or a 2-pass fixpoint).

Only after the fraction is known with jumps/calls handled do we commit to Step 1 (the NO-OP cull plumbing).
This single split tells us whether the cull is a ~28 % prize (very likely now) or still gated.

## Kept
All three classifiers stay committed (default-off, read-only, off-path bit-identical).
