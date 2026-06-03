# Cull A/B: the net -6% is the strip->list overhead, not the cull idea

Date: 2026-06-03
Device: AYN Thor `c3ca0370`, Blue Dragon, Vulkan/arm64. Numbers read from logcat this session.

## Content-matched A/B (matched by total_vertices ~= 291,406)

| config | rendered | avg | total_verts | gpu_frame_us | guest_ms |
|---|---|---|---|---|---|
| **ON** (cull: 224 whole-skips + per-triangle list conversion) | 2127 | 137 | 291,407 | **783,158** | 150,248 |
| **OFF** (no cull) | 2178 | 133 | 291,405 | **738,788** | 150,812 |

`total_vertices` (the binning-work predictor) matches to within 2 verts. **ON is +44,370 µs (+6.0%) WORSE**
— even though ON skips 224 fully off-screen draws (and the GPU draws ~275 fewer draws than OFF after the
skips).

## Root cause: strip -> list conversion

Blue Dragon's draws are triangle **STRIPS** (`prim ts ~1957`, `tl=0`). The cull emits the surviving
triangles as a triangle **LIST**, which has 3×(N−2) indices for an N-vertex strip — roughly **3× the index
count**, so the GPU binning pass processes ~3× the vertex-invocations for every surviving draw. That overhead
**exceeds** the saving from the 224 whole-skips + 97k dropped triangles, netting +6%.

This also explains the earlier "net-neutral" per-triangle result: the strip→list overhead ≈ the per-triangle
saving. On a strip-dominated title, per-triangle culling is self-defeating.

## The fix: whole-draw-ONLY mode

Separate whole-draw culling from per-triangle culling:
- If a draw is **fully off-screen** → skip it (`culled_index_count()==0`, not submitted).
- Otherwise → draw it **VERBATIM as the original strip** (no list conversion, no per-triangle cull).

This captures the ~74–91 ms (~10%) whole-skip saving **without** the strip→list overhead. Expected: OFF ~739k
→ whole-draw-only ~665k (~10% faster), hole-free.

## Next (build)

Add `gpu_whole_draw_only`: in `BuildCulledIndexList`, after the fast-affine per-vertex clip loop, run ONLY the
whole-draw all-outside test (all verts behind the camera, or all beyond one X/Y clip plane). If cullable →
`count=0` (skip). Else → return false so the command processor draws the draw verbatim (NOT the per-triangle
list). Device A/B content-matched: `gpu_frame_us` should drop ~10% vs OFF, hole-free, net fps up — the first
plausible net cull win.

(The deeper ~333 µs/draw per-draw binning floor still needs a user-run Snapdragon per-stage capture; this is a
real code-side win to land first.)
