# Verdict: the per-triangle affine cull is net-neutral on Blue Dragon

Date: 2026-06-03
Device: AYN Thor `c3ca0370`, Blue Dragon, Vulkan/arm64. All numbers read from logcat this session.

## The GO/NO-GO A/B

Both runs reached the heavy field vista ("Microsoft Game Studios Presents" windmill scene) and were
content-matched by `rendered` + `avg_vertices` + `total_vertices` (the measurement rule — never
match by `guest_ms` alone).

| run | rendered | avg | total_verts | gpu_frame_us | cpu_issuedraw_us | tris culled |
|---|---|---|---|---|---|---|
| **OFF** (no cull) | 2139 | 127 | 272910 | **666253** | ~44000 | 0 |
| **ON** (fast-only cull) | 2174 | 125 | 272843 | **666641** | ~62000–73000 | 22807 |

Total geometry is identical within 0.02%. `gpu_frame_us` differs by **+0.06%** — well inside the
±2% frame-to-frame noise (OFF alone swung 658209→682469 across adjacent frames). Both screenshots are
the same windmill vista, hole-free.

**Culling ~8.5% of the triangles produced zero measurable GPU frame-time reduction, while adding
~20–30k µs of CPU.** The per-triangle affine cull is net-neutral, slightly negative.

## Why there is no winning configuration

- **Interpreter cull** (`gpu_cull_fast_only=false`): culls the bulk (~589 draws, 66375 tris, the
  earlier device-proven ~13% GPU drop) but runs the per-vertex `ShaderInterpreter` at ~9.5 µs/vert ≈
  **~1 s/frame CPU** → net loss.
- **Fast-only cull** (default): ~0 extra CPU, but the cheap single-input-affine path engages on only
  ~113 draws / ~8.5% of triangles → **no measurable GPU win**.
- The multi-input bulk (1431 draws, each reading exactly 2 or 3 vfetch'd inputs — `multi_lc[2=461
  3=970 4=0 5p=0]`, mostly 3-leaf skinning/bilinear) cannot be cheaply transformed by any affine
  recovery. There is no middle ground.

`gpu_cull_compaction` is **default-off**, so normal play is unaffected — no regression. The cull stays
a correct, instrumented opt-in, and its machinery (position-slice extraction, affine matrix recovery,
fast replay, leaf analysis) is reusable.

## The deeper reason and the pivot

The frame is ~666 ms at 99% GPU busy across ~2000 draws ≈ **~333 µs per draw**. Per-triangle culling
only attacks the per-triangle binning half, and only for the cullable fraction — it cannot touch the
**per-draw floor** (~140 µs/draw × ~2000 ≈ 280 ms). Both software levers against the binning front
end have now failed on this TBDR:

- **Lever 1 — draw-call batching (concat / MDI):** dead — the per-draw-fixed cost is paid per
  sub-draw regardless of submission.
- **Lever 2 — per-triangle cull:** net-neutral (this doc).

**Pivot: whole-draw frustum/occlusion culling.** Removing an entire off-screen draw saves its full
~333 µs and works for *any* draw (including skinned) via a conservative bounding-volume-vs-frustum
test: compute the object-space AABB of the vfetch'd input positions, transform its 8 corners by the
primary matrix, reject only if all corners are outside one frustum plane (conservative → never culls
visible geometry). This reuses `SetupFastAffineReplay`'s leaf/decode machinery.

**Next step (read-only):** measure how many of the ~2000 draws are fully frustum-outside — the
whole-draw cull potential — to validate the premise before building it. If that potential is also
small, the per-draw binning floor is likely irreducible in software, and the honest path is a user-run
Snapdragon per-stage capture (binning-vs-fragment split) and possibly accepting Blue Dragon's ceiling.
