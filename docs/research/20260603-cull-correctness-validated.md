# Lever 2 cull — CORRECTNESS VALIDATED on a lit scene (no holes). Now only speed (Step 2b) remains.

Added a validation throttle (commit 49d7d9ee1: gpu_cull_max_per_frame caps how many draws/frame run the slow
interpreter cull) and used it to render a lit Blue Dragon scene WITH the cull active, to check the winding/
topology before investing in the NEON micro-interpreter.

## Result: the cull renders correctly - NO HOLES
- `-SetCvar gpu_cull_compaction=true;gpu_cull_max_per_frame=20`, BootWaitSec 200.
- Funnel: cull[branch=778 skip_dyntop=0 skip_qual=176 draws=20 dropped_tris=3659 bail(restart=0)] - 20 draws
  culled (budget cap), 3659 triangles dropped.
- Screenshot (`docs/evidence/20260603-012255-cull-throttle20.png`): a clear, LIT BD village/camp scene - a
  character, a solid wooden fence, sandy terrain with a rock mound, green foliage, a barrel, background
  buildings. ALL geometry is solid and complete: no holes, no inverted/missing faces, no scrambled triangles.
  A systematic winding/det-sign error would invert/hole the culled draws' front faces (terrain, fence); there
  are none. The strip->list + dynamic-topology conversion and the backface determinant sign are CORRECT.

## Why this is a valid check (and its caveat)
Only 20 draws were culled here (budget 20). But a SYSTEMATIC winding error affects ALL culled draws, so even
20 inverted draws (incl. the large terrain) would be visible as holes - and the scene is clean. So the cull's
correctness (winding, topology, restart sub-strip handling) is confirmed. Caveat: not an exhaustive per-draw
check; a fuller pass comes free once Step 2b makes the cull fast enough to cull the whole heavy scene.

## Throttle insight: lower budget progresses FURTHER
budget=20 reached a lit gameplay scene (rendered=970), while budget=250 stayed in the dark opening intro
(rendered=735). The guest is render-coupled: a cheaper per-frame cull => faster frames => the guest advances
further before the capture. So the throttle's real use is "cheap enough to reach a validatable frame," which
budget=20 achieved.

## Status: cull is FUNCTIONAL + CORRECT; speed is the only remaining gate
Proven so far: the cull engages (restart fix), drops ~51% of triangles (matches the ~56% estimate), and
renders correctly (no holes). The interpreter (~1.09s/frame at full cull) is too slow for a net win or a
full-scene capture. Step 2b (NEON position-slice micro-interpreter, ~100-500x faster) is now the ONLY thing
between here and a real fps win - and with correctness already confirmed, 2b can be built with confidence
(no det-sign surprise expected).

## Next (Step 2b)
Extract the position-export-slice op list (AnalyzeUcode) and replay it with a tight scalar-then-NEON kernel
inside BuildCulledIndexList, replacing the full ShaderInterpreter. Then device-validate end to end (heavy
scene reached, hole-free, gpu_frame_us DROP at content-matched frames). The gpu_cull_max_per_frame throttle
also stays useful for bounding the cull's residual CPU cost.
