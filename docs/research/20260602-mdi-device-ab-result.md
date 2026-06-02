# MDI draw-batching (Lever 2b) — device A/B result: CORRECT but a NET LOSS (2026-06-02)

Device A/B of `vulkan_merge_draws_indirect` (commit 6feba3d7b) on the AYN Thor
(Adreno 740), Blue Dragon intro (windmill scene), content-matched. Captures:
- OFF: `docs/evidence/20260602-120703-mdi-off.txt` / `.png`
- ON:  `docs/evidence/20260602-120934-mdi-on.txt` / `.png`

Both unfrozen, boot 110s, 18s window (the freeze cvar near-stops frame submission so
`gpu_frame_us` can't be read — see note). Each run dumps every draw-outcomes frame
(guest_ms, gpu_frame_us, rendered, avg_vertices) for post-hoc content matching.

## Correctness: PASS
Both screenshots render the IDENTICAL windmill intro (same sails, frame, terrain,
figure; only the sail-rotation phase differs by ~107 guest_ms between the two unfrozen
captures). No dropped/extra geometry. The HEAD-EMIT ordering (emit the indirect draw at
the run head, fill zeroed slots as the run grows) is sound — no wrong-pipeline/constant
corruption. So MDI is bit-faithful.

## Performance: NET LOSS (~8-14%, growing with batch size)
Matched on rendered draw count AND avg_vertices (equal GPU geometry workload — the clean
metric; raw fps and same-guest_ms are confounded because a slower GPU presents
less-complete frames at a given guest uptime):

| rendered | avg_vtx | OFF gpu_frame_us | ON gpu_frame_us | ON vs OFF |
|----------|---------|------------------|-----------------|-----------|
| 1007     | 164     | 362,317          | 391,428         | +8.0%     |
| ~1345    | 132     | 400,186          | 455,758         | +13.9%    |
| ~1400    | ~127    | 409,760          | 465,743         | +13.7%    |

Corroborating symptom: at matched guest_ms the ON run renders FEWER draws/frame
(e.g. gm~131k: off=2014 vs on=1768) — the GPU falls behind and presents older,
simpler frames. GPU busy 99% @ 680MHz (max) in both.

## Why (the real lesson)
1. **Padding overhead.** Head-emit pre-sizes each run to `mdi_max_draw_count_`=32 ZEROED
   commands; runs average only ~6-8 draws (runlen histogram), so ~3/4 of every batch is
   no-op indirect commands. Adreno's command processor does NOT skip indexCount=0 indirect
   commands cheaply enough — the per-command fetch/setup overhead × ~32 × ~150 run-heads
   swamps the savings.
2. **The cost is the BINNING FLOOR, not draw-call overhead.** ~300us/draw is the Adreno
   binning pass running the position VS per vertex per draw. Batching N draw CALLS into one
   vkCmdDrawIndexedIndirect does NOT reduce that — each sub-draw still bins independently.
   This confirms prior research: the per-triangle binning floor is NOT coalescable by merging
   draw calls. Only a real triangle CULL (fewer triangles to bin) or fewer/bigger guest draws
   lowers it.

## Decision
- KEEP `vulkan_merge_draws_indirect` committed, **default OFF**, documented as a validated
  negative. Do NOT enable it.
- A drawIndirectCount variant (exact count, zero padding) MIGHT recover the padding loss, but
  point 2 says draw-call batching is the wrong axis on this TBDR — low expected value.
- The lever that actually targets ~300us/draw is reducing per-triangle binning work: a CPU/GPU
  triangle cull (HELD — guest-visible, needs exact clip/winding/guard-band match) gated behind
  the read-only cullable_tris counter, OR a user-run Snapdragon Profiler per-stage capture to
  confirm binning is the dominant GPU stage before investing.

## Note: gpu_freeze_at_guest_ms breaks gpu_frame_us measurement
Freezing (time_scalar->0.0001) makes the displayed frame static (screenshot renders) but the
guest stops submitting new draws -> VdSwap=0, gpu_busy~2%, no draw-outcomes line in the window.
So content-matching must be done UNFROZEN + post-hoc on rendered/guest_ms (the all-frames dump
added to thor_evidence.ps1 this session), not via freeze.
