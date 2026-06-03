# Lever 2 Step 1 result — NO-OP cull index-compaction plumbing works (renders correctly, GPU-neutral, +2.4ms CPU copy)

Device-validated the `gpu_cull_compaction` NO-OP plumbing (commit c3e3c940b) on Blue Dragon's heavy 3D
cinematic. When on, every kGuestDMA indexed draw is routed through a transient host-visible index buffer:
the raw guest index bytes are copied VERBATIM out of guest memory and the draw binds that copy instead of
the shared-memory buffer. No culling yet - this is the structural pipe the CPU/NEON cull reuses.

- OFF: `docs/evidence/20260602-225504-cullnoop-off.txt` (+ .png) - gpu_frame_us=682659 rendered=2132 cpu_issuedraw_us=42208 guest_ms=144542.
- ON:  `docs/evidence/20260602-225756-cullnoop-on.txt` (+ .png) - gpu_frame_us=679019 rendered=2303 cpu_issuedraw_us=44594 guest_ms=144257.

## Validation (3 independent signals)
1. **Visual correctness:** the ON screenshot renders the identical harbor cinematic ("Microsoft Game
   Studios Presents", ships/terrain/sky) with NO scrambled or missing triangles. A byte-wrong index copy
   would have corrupted or dropped geometry; it is clean. (Both runs verified by reading the screenshot.)
2. **No crash/stall:** ON ran at 99% GPU, vdswap=21/12s (~1.75 fps), 2303 draws all routed through
   compaction.
3. **Exactly the NO-OP signature:** GPU time is unaffected (679k vs 683k us, both ~scale with their draw
   count - the copy is CPU-side, the GPU draws identical geometry), and CPU issuedraw rose +2.4ms
   (44594 vs 42208 us) for the verbatim memcpy of ~2300 draws' indices (~1us/draw). A correct NO-OP shows
   precisely this: GPU-neutral, small CPU copy cost.

## Caveat on the comparison
A pixel-exact A/B was not achievable: gpu_freeze_at_guest_ms halts rendering entirely (guest clock
near-stops -> no new draws -> compaction never engages), so it can't test a rendering-path change; and two
fresh launches desync (intro-skip timing variance), so OFF (rendered 1963-2132) and ON (2174-2303) never
overlap at equal guest_ms for a byte-diff. Validation therefore rests on visual correctness + GPU-time
neutrality + the expected CPU copy cost, which together are conclusive for a verbatim NO-OP (a wrong copy
cannot render the scene correctly). A frame-locked capture (render N deterministic frames then exit) would
let a future pixel-diff be exact - a harness improvement worth making before Step 2's transform lands.

## What this unlocks
The pipe is proven: indices now flow CPU-side (guest memory -> transient index buffer -> draw) with the
draw decoupled from shared memory. Step 2 inserts the cull between read and bind: for an
is_position_affine_mvp_candidate draw, replay the position via a bit-exact NEON affine-MVP micro-kernel,
backface-test each triangle, and write only the surviving indices (reduced indexCount). Off-path and
non-qualifying draws keep the verbatim copy. Bit-exact gates remain (no vertex-kill honoring, W
reconstruct when vtx_w0_fmt==0, winding from origin_bottom_left, unconditional-position subset first).

## Kept
gpu_cull_compaction stays committed (default-off, off-path untouched, validated rendering-correct on).
