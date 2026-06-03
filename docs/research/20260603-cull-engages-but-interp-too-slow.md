# Lever 2 cull — IT ENGAGES (restart fix): drops ~51% of triangles. But the interpreter is too slow to validate on a rich frame.

Fixed the bail (commit d8a6f96c7: handle primitive restart instead of bailing) and ran gpu_cull_compaction=true
on Blue Dragon.

## The cull now engages (device funnel)
```
cull[branch=404 skip_dyntop=0 skip_qual=74 draws=274 dropped_tris=61515 bail(...restart=0...zerodrop=56)]
total_vertices=119930  cpu_issuedraw_us=1089554  rendered=648
```
- **restart=0**: the restart-handling fix worked (was 1604).
- **draws=274, dropped_tris=61515**: the cull ran and removed ~61.5k of ~120k triangles = **~51%**, right in
  line with the ~56% cullable estimate. This is the FIRST real triangle cull on BD.
- **zerodrop=56**: 56 draws had nothing to cull (drew verbatim) - expected.
- Evidence: `docs/evidence/20260603-005254-cull-engage.txt` (+ .png).

## But it can't be validated/measured yet - the interpreter is too slow
cpu_issuedraw_us = **1.09 SECONDS/frame** (the ShaderInterpreter replays ~120k vertex positions at ~9.5us
each). The guest clock is render-coupled, so at >1s/frame the guest never reaches the heavy harbor cinematic
in a 130s boot wait - the captured frame is an early DARK intro (rendered=648, orbs on black). Consequences:
- **Visual correctness (holes) UNVERIFIED**: the dark frame shows clean orbs and no obvious scatter/corruption,
  but it has no geometry-rich surface to confirm the winding/det sign is right. Inconclusive.
- **Perf (gpu_frame_us drop) UNMEASURED**: OFF (fast) reaches the harbor at 130s while ON (slow) is stuck in
  the early intro - the runs are content-mismatched, so no gpu_frame_us comparison is possible.

## Verdict: the cull mechanism is PROVEN; speed is now the gate (Step 2b)
The whole 2a "correctness-first with the slow interpreter" plan is blocked: the interpreter is so slow it
prevents reaching a validatable scene. The fix is Step 2b - replace the per-vertex ShaderInterpreter with a
NEON position-slice micro-interpreter (~0.02-0.1us/vert, 100-500x faster). That makes the cull fast enough to
(a) reach + render the heavy scene in real time, (b) let a hole-free screenshot confirm winding correctness,
and (c) measure the gpu_frame_us drop at content-matched frames. It is also the whole point (a net fps win).

## Next (Step 2b)
- Extract the position-export-slice op list during AnalyzeUcode (the ops feeding gl_Position: vfetch of
  position + the {dp4,dp3,mad,mul,add,...} MVP ops against float consts).
- A tiny NEON interpreter over that op list (read the vfetch'd position + constant registers -> clip xyzw),
  replacing the ShaderInterpreter call inside BuildCulledIndexList. Validate the NEON output vs the
  ShaderInterpreter on a few draws (offline/scalar reference) for bit-agreement before trusting it.
- Then device-validate the cull END TO END: heavy scene reached, screenshot hole-free (flip det sign if
  faces vanish), gpu_frame_us DROPS at content-matched rendered/avg_vertices.

## Status
gpu_cull_compaction stays default-off (verbatim NO-OP when off). The cull engages and drops the expected
triangle fraction; correctness + perf validation are gated on the NEON speedup.
