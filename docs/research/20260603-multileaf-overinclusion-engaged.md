# Multi-leaf draws are over-inclusion: per-candidate recovery engages 1412 (was 133)

Date: 2026-06-03
Commit: `fcdce21c8` (try single-leaf affine recovery per candidate leaf)
Device: AYN Thor `c3ca0370`, Blue Dragon, `--ez gpu_cull_compaction true
--ez vulkan_trace_draw_outcomes_per_frame true`. Numbers read from logcat this session.

## Change

`SetupFastAffineReplay` previously bailed (`kMultiLeaf`) whenever the position slice read >1 vfetch'd
register. Now it extracts the per-leaf logic into `TryRecoverAffineForLeaf` and calls it for EACH
candidate vfetch'd leaf the slice reads (capped at 4). The residual self-check accepts a leaf only if
clip position is genuinely affine in it — so a multi-leaf draw whose position depends on just one of the
read inputs (the others being slice over-inclusion / position-irrelevant) now engages; a genuine
multi-input (skinned/bilinear) position fails every candidate and falls back. Residual-guarded.

## Result (heavy field vista, guest_ms 150389, rendered=2159, hole-free)

```
fastrep[engaged=1412 fail(noleaf=0 multi=192 novf=0 badfmt=0 recov=0)]  multi_lc[2=16 3=176]
cull[draws=560 dropped_tris=96042 bail(... zerodrop=852)]
gpu_frame_us=792852   cpu_issuedraw_us=226533   total_vertices=291535  avg=135
```

- **`engaged` 133 → 1412** (~90% of `affine_mvp_pos_draws=1934`); **`multi` 1507 → 192**.
- So the multi-leaf draws were **over-inclusion** — position *is* single-leaf-affine; the slice merely
  read extra position-irrelevant vfetch'd attributes. (This overturns the earlier "genuine multi-input"
  read of the structured 2/3-leaf counts.) The remaining 192 (mostly 3-leaf) are the genuine skinned set.
- `dropped_tris` 22807 → 96042. Screenshot hole-free — engaging 1412 fitted matrices did not corrupt.

## Not yet a win — what's still needed

This is **not** content-matched: `total_vertices=291535` / `avg=135` here vs the OFF baseline's 272910 /
127, so `gpu_frame_us=792852` cannot be compared to OFF's 666253 (rough scaling even suggests ON ≈ +10%).
And `cpu_issuedraw` rose to ~250k (basis sampling for 1412 draws).

Per-triangle culling is net-neutral (proven). The only real win is **whole-draw skips** — a fully
off-screen engaged draw drops all its triangles → `culled_index_count()==0` → the draw is not submitted
(`vulkan_command_processor.cc:4243`), saving its full ~333 µs per-draw binning cost. That count is
**unmeasured** (no count==0 counter in the `gpu_cull_compaction` path). The ~50% off-screen figure was a
tight cinematic; the wide gameplay vista likely has far fewer fully-off-screen draws.

## Next (decisive)

1. Add a count==0 whole-draw-skip counter to the cull log.
2. Content-matched A/B (ON vs OFF at matched `rendered`/`avg_vertices`) on the heavy vista: read whole-skip
   count, `gpu_frame_us`, `cpu_issuedraw_us`, net fps; screenshot hole-free.
3. If whole-skips are few and `gpu_frame_us` doesn't drop, the cull family is exhausted for Blue Dragon
   gameplay (off-screen draws are a cinematic phenomenon) — set `gpu_cull_compaction` firmly default-off
   and pivot (request a user-run Snapdragon per-stage capture, or accept the ceiling).
