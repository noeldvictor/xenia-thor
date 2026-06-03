# Fast-cull: kNoLeaf fixed (vfetch-origin leaf), and the interpreter fallback is the fps killer

Date: 2026-06-03
Commits: `41901d119` (FastSetupFail histogram), `7584082ef` (vfetch-origin leaf detection)
Device: AYN Thor `c3ca0370`, Blue Dragon, Vulkan/arm64, `gpu_cull_compaction=true`,
`vulkan_trace_draw_outcomes_per_frame=true`. All numbers read from logcat this session.

## What was measured

### Run 1 (heavy field scene, guest_ms 162228–166896, rendered≈2196–2210) — diagnosis
Replaced the (now-useless) fallback-format histogram with a definitive `FastSetupFail`
breakdown set at each `SetupFastAffineReplay` early return:

```
fastrep[engaged=585..606 fail(noleaf=980..981 multi=0 novf=0 badfmt=0 recov=1..3)]
```

The slice-tighten hypothesis (over-inclusion → multi-leaf) was **doubly wrong**: `multi=0`.
The dominant fallback is **kNoLeaf** (~981 of ~1587 fast-replay attempts, ~62%).

### Root cause of kNoLeaf
Leaf detection was:
```cpp
uint64_t leaf_regs = read_regs & ~written_regs;  // read but not ALU-written
```
`leaf_regs == 0` means *every register the slice reads is also written within the slice* —
the **register-reuse / in-place-update** case: the vfetch'd position lands in `r0`, an
in-slice ALU op does `r0 = f(r0)`, so `r0` ∈ `written_regs` and gets masked out, even though
the pre-transform `r0` (the vfetch value) is the true affine input. ~62% of Blue Dragon's
position shaders transform the input in place.

### The fix (`7584082ef`)
A leaf is a register the slice **reads** whose value **originates from a vertex fetch**:
```cpp
uint64_t leaf_regs = read_regs & vfetch_regs;  // vfetch_regs = regs written by a vfetch attr
```
The whole chain from the vfetch input to clip stays affine (`clip = M·A·[p,1] = M'·[p,1]`),
so M recovery is still valid, and the residual self-check (>1% → fall back) still guards
correctness. Every previously-successful leaf was already a vfetch register (`novf=0`), so
this is a strict superset of the working cases plus the in-place-transform cases.

### Run 2 (lighter cinematic, guest_ms 149922–151962, rendered≈698) — after the fix
```
fastrep[engaged=59 fail(noleaf=0 multi=271 novf=0 badfmt=0 recov=0)]
```
**kNoLeaf eliminated (980 → 0).** Fallback moved entirely to **kMultiLeaf** (~271): the slice
now reads >1 vfetch'd register for those draws. Screenshot: coherent dark/bloom scene, no holes.

> Note: Run 2 is **not content-matched** to Run 1 (different, much lighter frame — the button
> sequence landed earlier), so `engaged=59` is not comparable to Run 1's 606. The decisive,
> content-independent signal is the *cause* shift: kNoLeaf → kMultiLeaf.

## The real blocker: the interpreter fallback

`draw_extent_estimator.cc:1239–1268` confirms: when fast setup fails, `BuildCulledIndexList`
**still culls the draw via the per-vertex `ShaderInterpreter::Execute()`** (line 1252). On
Run 2: ~271 multi-leaf draws × ~170 verts × ~9.5µs ≈ the observed `cpu_other_us≈1,000,065`
(`cpu_issuedraw_us≈1,008,641` ≈ 1.0 s/frame) while `gpu_frame_us≈180,622` (0.18 s). The cull
makes the CPU the bottleneck by **5×** — the whole point of the affine fast path was to avoid
exactly this per-vertex interpreter.

## Next steps

1. **Make the cull fast-path-ONLY** (high value): if `SetupFastAffineReplay` fails, draw
   verbatim — never run the interpreter. Cvar-gated so the interpreter path stays available for
   the GPU-proof A/B. Bounds cull CPU to the cheap fast draws → net fps should finally go
   positive. Verify content-matched on the heavy 162k frame: `fastrep[engaged]` meaningful,
   `cpu_issuedraw_us` collapses, `gpu_frame_us` still drops, net fps ≥ OFF, screenshot hole-free.
2. **Diagnose kMultiLeaf** (genuine multi-input/skinned position vs slice over-inclusion). If
   over-inclusion, tightening the slice converts the 271 to fast → cull most draws cheaply
   (jackpot). If genuine skinning, fast-path-only is the ceiling for the affine approach.
