# Cull family closed: the Adreno TBDR already rejects off-screen geometry cheaply

Date: 2026-06-03
Commits: `2441b3bc5` (gpu_whole_draw_only), with `b9b22670c` (per-tri A/B), `fcdce21c8` (multi-leaf engage).
Device: AYN Thor `c3ca0370`, Blue Dragon, Vulkan/arm64. All numbers read from logcat this session.

## The three rigorous content-matched A/Bs

| lever | result | why |
|---|---|---|
| Lever 1 — draw batching (concat / MDI) | dead | per-draw-fixed cost paid per sub-draw on the TBDR |
| Lever 2 — per-triangle cull | **+6% gpu_frame_us** | surviving strips emitted as lists (3× indices → ~3× binning) |
| Lever 2c — whole-draw-only cull | **neutral** (−0.1% .. +2.0%) | off-screen geometry is already cheap in binning |

## Whole-draw-only A/B (the decisive one)

`gpu_whole_draw_only`: skip a draw when all its fast-affine clip verts are outside one frustum plane; otherwise
draw it verbatim as its original strip (no list conversion).

Heavy field vista, hole-free, 273 whole draws skipped (~24% of vertices not binned):

| match basis | ON (whole-only) | OFF | delta |
|---|---|---|---|
| guest_ms ~150.3k | 753,444 | 738,788 | +2.0% |
| total_vertices ~291,4xx | 737,730 | 738,788 | −0.1% |

**Skipping ~24% of vertices produced no gpu_frame reduction.**

## The mechanistic reason

On a tile-based deferred renderer (Adreno FlexRender), the binning pass runs the position VS per vertex, then
assigns each triangle to the tiles it covers. An **off-screen triangle is frustum-rejected and written to no
tile primitive list** — so it costs only the (cheap) position VS, not the (expensive) per-tile primitive-list
writes. The hardware already skips the expensive part for off-screen geometry.

So CPU-side culling of off-screen / backface geometry removes work the GPU was **already doing nearly for free**,
while adding CPU (basis sampling) and, for the survivors, index-copy/topology overhead. That is why every cull
variant is net-neutral or worse. (It also retro-explains the earlier "~13% GPU drop" claim as a content-confound
— the rigorous total_vertices-matched A/Bs supersede it.)

## Conclusion and pivot

The Blue-Dragon GPU software-reduction search is **exhausted**: batching dead, culling dead, fill 0%, load/store
~3%, render-pass breaks ~12%. The remaining lever is the **~333 µs per-draw floor itself** — the on-screen,
tile-contributing per-draw cost (per-draw fixed context-roll + tile primitive writes). ~333 µs for a ~130-vertex
draw is pathologically high, suggesting a per-draw **context roll / state re-latch / stall**, not raw binning
compute — but confirming that requires the per-stage GPU split.

All cull cvars stay **default-off** (no regression); the machinery (affine recovery, slice extraction, fast
replay, whole-draw test) is correct, instrumented, and reusable.

**Highest-value next move: a user-run Snapdragon Profiler capture** of the running emulator on the heavy scene
(per-stage GPU metrics — binning VS % vs tile/primitive % vs fragment % vs % stalled). Headless perfetto is
blocked on the retail device (no root). Alternatives: re-target a CPU-bound title (e.g. Lost Odyssey) where NEON
actually applies — Blue Dragon is GPU-bound, so NEON cannot help its floor; or accept BD's ~1.3 fps ceiling.
