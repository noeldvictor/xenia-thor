# Whole-draw skips on the heavy vista: ~10% of draws (hole-free), the cull ceiling

Date: 2026-06-03
Commit: `3e693b01f` (count==0 whole-draw-skip counter)
Device: AYN Thor `c3ca0370`, Blue Dragon, `--ez gpu_cull_compaction true
--ez vulkan_trace_draw_outcomes_per_frame true`. Numbers read from logcat this session.

## Result (heavy field vista, guest_ms 150248, rendered=2127, avg=137, total_verts=291407)

```
whole_skip[draws=224 verts=59200]   (216 / 57887 the next frame)
fastrep[engaged=1410 fail(noleaf=0 multi=194 ...)]   multi_lc[2=16 3=178]
cull[draws=581 dropped_tris=97385 zerodrop=829]
gpu_frame_us=783158   cpu_issuedraw_us=226374
```

- **224 of ~2127 draws (~10.5%) are whole-draw skipped** — every triangle culled → `culled_index_count()==0`
  → the draw is not submitted, so the GPU never bins it. ~59,200 verts (~20% of 291,407) spared.
- Screenshot: BD field vista, **coherent and hole-free** — dropping 224 whole draws removed no visible
  geometry. The high-stakes whole-draw hole check passed (conservative all-verts-outside test + residual-
  guarded affine M only ever flags truly off-screen draws).

## Interpretation

This is a **real, correct, but modest** win — unlike the net-neutral per-triangle cull, the whole-skips
remove the full per-draw cost of ~224 draws (~74 ms ≈ ~9% of the 783 ms GPU frame, if realized net). It is
**not** the ~50% seen on the tight cinematic — the wide gameplay vista has far fewer fully-off-screen draws.

Net fps is **unproven**: this frame (291k verts) is not content-matched to the OFF baseline (273k verts), and
`cpu_issuedraw` rose to ~250k from the basis sampling for 1410 engaged draws (though the frame is still
GPU-bound at 783k, so the GPU saving should translate).

## Strategic verdict

Culling cannot make Blue Dragon's heavy scene fast. ~90% of draws are necessary on-screen draws, each paying
the **~333 µs per-draw binning floor**; removing the ~10% off-screen draws is at best a ~9% win. To reach
playable speed, the ~333 µs/draw floor itself must drop — and ~333 µs is pathologically high for a ~130-vert
draw, suggesting a per-draw **stall / state / barrier**, not raw binning compute. Confirming that requires a
**user-run Snapdragon Profiler per-stage GPU capture** (binning vs fragment vs stall) — the documented external
unblock (headless perfetto is blocked on the retail device, no root).

## Next

1. One content-matched A/B (ON vs OFF at matched `rendered`/`avg_vertices`) to prove and, if positive, ship the
   ~9% whole-skip win (the first non-neutral, hole-free cull result). It is gated by `gpu_cull_compaction`
   (default-off) so it ships as opt-in regardless.
2. Pivot: request the Snapdragon per-stage capture, and/or re-target Lost Odyssey / Banjo / Burnout where the
   bottleneck may differ.
