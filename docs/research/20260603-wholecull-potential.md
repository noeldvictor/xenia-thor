# Whole-draw frustum-cull potential on Blue Dragon (read-only sizing)

Date: 2026-06-03
Commit: `e0587c552` (read-only `wholecull` counter in `CountCullableTriangles`)
Device: AYN Thor `c3ca0370`, Blue Dragon, Vulkan/arm64, `--ez gpu_trace_cullable_tris true
--ez vulkan_trace_draw_outcomes_per_frame true`. Numbers read from logcat this session.

## Why whole-draw culling

The per-triangle affine cull is net-neutral (`20260603-cull-net-neutral-verdict.md`): culling triangles
*within* drawn draws doesn't remove the dominant **~333 µs per-draw binning cost** (frame ≈ 666 ms at
99% GPU busy across ~2000 draws). Whole-draw culling attacks that floor directly — if an entire draw is
off-screen, *not submitting it* saves its full per-draw cost.

## The counter

A draw is flagged whole-cullable when, over its replayed valid vertices, EITHER:
- every vertex is behind the camera (`w <= 0`, the near/behind-eye half-space), OR
- every vertex is in front (`w > 0`) AND all lie beyond the same X or Y clip plane
  (`x>w` / `x<-w` / `y>w` / `y<-w`).

Conservative (never flags a partially-visible draw); Z planes omitted to avoid clip-Z convention risk.

## Result

The `gpu_trace_cullable_tris` interpreter replay costs ~1.3 s/frame, so in 190 s wall-clock the guest
only reached a **light dark cinematic** (guest_ms ≈ 200k), **not** the heavy 2000-draw gameplay vista.
Three stable frames:

```
rendered=646  total_verts=120470  cullable_tris=66809  affine_mvp_pos_draws=572
  wholecull[draws=322 elig=458 verts=59608]
rendered=665  total_verts=120779  affine_mvp_pos_draws=591
  wholecull[draws=341 elig=477 verts=59653]
```

- **~50% of all rendered draws are whole-draw frustum-cullable** (322/646), **~70% of the eligible
  (replayed) draws** (322/458).
- **~50% of vertices** (~59.6k/120.5k) would be saved from binning.
- ~90% of draws are affine-position candidates (`affine_mvp_pos_draws=572/646`), so a fast clip-space
  bound is computable for the bulk without the interpreter.

**Caveat:** this is a tightly-framed cinematic, which likely has an above-average off-screen fraction.
The heavy gameplay vista's fraction is unmeasured (the interpreter is too slow to reach it). The cheap
build below runs fast enough to reach and confirm the heavy vista directly.

## Build plan (next)

Fast whole-draw frustum cull, reusing the existing fast-affine machinery:

1. In `BuildCulledIndexList`, for draws where `SetupFastAffineReplay` succeeds, the fast clip positions
   are already computed per vertex. Add an all-verts-outside-one-plane test; if true, **drop the whole
   draw** (emit 0 indices) so the GPU never bins it. Cvar-gated (`gpu_whole_draw_cull`). Runs fast
   (reaches the heavy vista) and *is* the optimization.
2. Device A/B on the heavy vista: whole-culled draw count, `gpu_frame_us` drop, net fps, screenshot
   hole-free.
3. Single-input-affine draws (~133 engaged on the heavy vista) are exact/safe — start there. If their
   coverage is too small on the heavy vista, extend to multi-input affine recovery
   (`clip = M·[in0, in1, 1]`, residual-guarded) to cover the ~90% affine bulk — the large win.

Conservative throughout: only draws *provably* fully outside the frustum are dropped, so no visible
geometry is ever culled (and a residual self-check still guards the affine map).
