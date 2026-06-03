# NEON cull implementation plan — the 24h Blue Dragon speed decision (2026-06-02)

Lead-architect synthesis of four design agents (interpreter speed, NEON-JIT feasibility, cull
integration, other HW levers) + two adversarial verify verdicts (one `not-viable`/medium, one
`viable-with-fixes`/high). Every claim is device-grounded or cited to `file:line` read this session.
This supersedes the "GPU-compute cull" framing in `20260602-cull-prize-sizing.md` and the "low expected
value MDI" framing in `20260602-cull-and-batching-scope.md §D`.

---

## 0. The one decision (TL;DR)

1. **The §A gate — the prerequisite the whole cull rests on — HAS PASSED on the device.** It was run
   as `gpu_skip_draws_below_verts=16` (`docs/evidence/20260602-130009-skip-tiny16.txt`). Dropping ~949
   tiny draws (~43% of all draws, but only ~5% of vertices) cut `gpu_frame_us` ~20-27% (baseline 680474
   → content-matched skip16 frame 498341). **A large per-DRAW-FIXED cost is REAL** — it is NOT a pure
   per-triangle binning floor. This is exactly the gate `cull-and-batching-scope.md §A` said must
   resolve before building anything, and it resolves toward **both** levers being live.

2. **GO on the CPU/NEON backface cull — but CONDITIONAL and SECOND, not first.** It is a net fps win
   *only* if built as a **per-shader affine-MVP NEON micro-kernel** (NOT the ShaderInterpreter, NOT a
   full NEON-JIT), backface-only first, and *only after* the two cheaper, surer levers below. The
   honest expected contribution is **bounded by the per-TRIANGLE share of the frame**, which the skip16
   result just showed is the *smaller* of the two cost components. See §1 for the verify:net-win
   arithmetic and the NO-GO conditions.

3. **Build the corrected zero-padding `vkCmdDrawIndexedIndirectCount` MDI FIRST** (hours, reuses all
   existing plumbing). The skip16 result upgraded its expected value from "low" to "plausibly real" by
   proving the per-draw-fixed cost it attacks is large. It is the cheapest experiment that can move fps
   and it is the empirical confirmation of the per-draw-fixed magnitude. See §4 Lever 1.

**Net: the cull is a GO as a build target for the 24h, but it is lever #2 by sequence. Lever #1 is the
corrected MDI Count. The cull's GO is conditional on the affine-MVP micro-kernel approach clearing
budget with margin (§2) and on holding the bit-exactness line (§3).**

---

## 1. GO / NO-GO on the CPU/NEON cull as a net fps win (verify:net-win)

### The arithmetic (this is the load-bearing analysis)

A net win requires `transform_cost < binning_saved`, AND `binning_saved` to be a non-trivial share of
the frame. Both sides are now bounded by device data:

**binning_saved (the prize) — bounded ABOVE, and it is the smaller cost component.**
- `cullable_tris=53835 / ~95.8k triangles ≈ 56%` cullable (`cull-prize-sizing.md:16`, device-measured).
- BUT the cull touches ONLY the per-TRIANGLE binning term and ZERO of the per-DRAW-FIXED term. The
  skip16 device A/B proved the per-draw-fixed term is LARGE: removing ~5% of vertices but ~43% of draws
  cut ~20-27% of frame time. So removing 56% of *triangles* (≈56% of the per-triangle term, none of the
  fixed term) yields a frame-time drop **bounded above by `0.56 × (per-triangle share)`**, after the
  ~33ms fixed floor and the large per-draw-fixed component are subtracted. The adversarial `not-viable`
  verdict's central point stands: **the prize is the smaller slice, and its exact size is still only
  bounded, not pinned** (the clean per-stage split needs a user-run Snapdragon Profiler capture,
  CLAUDE.md). Honest prize estimate: **~10-20% frame-time on a good day, could be less.**

**transform_cost (the cost) — the interpreter is unusable; only one path clears budget.**
- Device-measured interpreter replay: `cpu_other_us=920935` / 96451 verts ≈ **9.5 µs/vert**
  (`20260602-122319-cullable-strip-size.txt`; `cull-prize-sizing.md:21`). At ~210k verts that is
  ~920ms/frame — it would make the frame CPU-bound. **Unusable as posed.**
- Both design agents + both verdicts converge: even fully optimized (multi-thread 5 cores + hoist
  constants + position-DCE + NEON-batch-4) the *generic interpreter* bottoms out at ~0.1 µs/vert ≈
  17-32ms aggregate — which only **BREAKS EVEN** with a 16-33ms frame. A generic bytecode VM always
  re-pays per-instruction decode/dispatch. **Do NOT productionize the interpreter for the cull.**
- The NEON-JIT (Xenos-ucode → ARM64+NEON) is feasible on *speed* (~0.02-0.08 µs/vert) but **6-11 weeks
  and carries an intractable bit-exactness tail** (Adreno transcendental curves are device-coupled, not
  CPU-reproducible; `spirv_shader_translator_alu.cc:901-909`). **Out of scope for a 24h push.**
- The ONLY path that clears budget with margin in 24h: a **per-shader affine-MVP NEON micro-kernel**
  (§2). ~0.01-0.02 µs/vert ≈ low-single-digit-ms/frame single-threaded, sub-ms multi-threaded. This is
  ~100-300× faster than the interpreter and leaves the wide margin a net-win requires.

### Verdict: **GO (conditional)**

GO **iff** the cull is built as the affine-MVP micro-kernel (§2), backface-only first, with the
bit-exact gates (§3) held. With `transform_cost ≈ 2-4ms` and `binning_saved ≈ 10-20%` of a ~545-680ms
frame (≈ 55-135ms), the inequality `transform_cost ≪ binning_saved` holds comfortably — **this is a net
win** on the arithmetic. The risk is not the inequality; it is (a) the prize being at the low end of the
bound, and (b) bit-exactness silently dropping visible geometry.

### NO-GO conditions (be honest — these would kill it)
- If the affine-MVP **qualifying draw fraction is low** on Blue Dragon (UNMEASURED — see §2; effects/
  skinned/UV-animated VS don't qualify). If most BD draws fall back to the interpreter, the cull does
  almost nothing. **This must be measured with a per-shader classifier counter before committing the
  full build** — it is the cull's own §A-style gate.
- If a device screenshot A/B shows ANY visible geometry delta at the backface-only step (bit-exactness
  failure) → cvar off, NO-GO. This is the `arm64_vmx_dot_f32_fastpath` black-screen failure class.
- If the corrected-MDI Lever 1 (§4) already captures most of the per-draw-fixed cost AND the per-stage
  capture shows binning ≪ fragment, the cull's remaining prize shrinks toward zero → NO-GO, pursue the
  ranked alternatives (§4).

### Honest best alternative if the cull is NO-GO
**The corrected zero-padding `drawIndirectCount` MDI (§4 Lever 1) is the better first spend regardless**,
and is itself the honest fallback: it attacks the *larger* (per-draw-fixed) cost the skip16 data proved
real, costs hours not weeks, and reuses all existing MDI plumbing. If both the cull and corrected-MDI
stall, the honest answer is **"nothing else in software moves this frame"** — levers 3/4/5 in §4 are
confirmed dead (EDS already shipped; NEON CPU paths irrelevant on a GPU-bound CPU-idle frame; no
driver-side lever reaches the binning front-end). The one thing that would change the verdict is an
external **Snapdragon Profiler per-stage capture** pinning binning-vs-fragment — request it from the user.

---

## 2. If GO: the EXACT transform to build first — per-shader affine-MVP NEON micro-kernel

**Build the affine-MVP micro-kernel, NOT the improved interpreter and NOT the NEON-JIT.** Rationale,
grounded in the cost model:

- The 9.5 µs/vert is **~90% generic-bytecode-VM overhead**, not the math: per-vertex `state_.Reset()`
  memset (`shader_interpreter.cc:29`), full CF re-walk from index 0 every vertex (`:36`), scalar
  per-component `FlushDenormal`+swizzle+abs+negate with NO NEON (`:321-330`, `FlushDenormal` at
  `shader_interpreter.h:108`), per-access `GetFloatConstant` memcpy + `SQ_VS_CONST` re-read (`:261-285`),
  virtual `Export` dispatch (`:956` → `draw_extent_estimator.cc:76`). The actual MVP math is ~20-40
  cycles; the interpreter spends ~14,000. Optimizing the VM keeps the decode/dispatch tax → only
  break-even (§1).
- A BD opaque-mesh position output is almost always a small **MVP transform**: vertex-fetch (3-4
  components) then a handful of `kDp4`/`kMad` against 4 constant matrix rows → `oPos`. This is
  detectable **once per shader** (not per vertex) from the already-parsed ucode (`is_ucode_analyzed()`,
  opcode-info tables `ucode.h`, position export = `instr.is_export() && vector_dest()==kVSPosition`,
  `shader_interpreter.cc:935/957`).

**The classifier (once per shader, cached):** walk the position export's backward dependency slice. The
shader is **affine-MVP** iff the slice reduces to: one vertex-fetch into a temp, then only
`{kDp4, kDp3, kMad, kMul, kAdd}` ops whose non-fetched operands are float constants — with **no** loops,
predication, `a0` relative addressing, texture fetch, `kCube`, or transcendentals (`rcp`/`rsq`/`exp`/
`log`). Anything else → **fall back to the full interpreter** (or bail the draw from the cull entirely).

**The kernel (per qualifying draw):** load the 4 constant matrix rows once; per vertex `vld` the fetched
position, run 4× `vfmaq`/dp4 in NEON `float32x4` → `oPos`. Process 4 vertices SoA where it helps. With
the guest quirks baked in (§3).

**us/vert target: ≤ 0.02 µs/vert** (≈ 2-4ms for 210k verts single-threaded; sub-ms across the 5 fast
cores). This is the number that makes `transform_cost ≪ binning_saved` hold. If a built kernel measures
worse than ~0.05 µs/vert, investigate before proceeding — the margin is the whole point.

**WHY this and not the interpreter:** the interpreter's *floor* is break-even (§1); the micro-kernel's
*floor* is a comfortable win, and it adds the smallest possible new bit-exactness surface (only the MVP
op subset, vs the interpreter's entire ALU). **WHY measure the qualifying fraction first:** if BD's
strip-dominated `tl=0, ts~2009` draws (the cull's target) mostly qualify, the cull bites; if they're
mostly skinned/effects VS, it does almost nothing — and that fraction is UNMEASURED. Gate it with a
per-shader classifier counter (count draws/verts hitting affine-MVP vs fallback), analogous to the
existing `gpu_trace_cullable_tris`, run on-device ONCE before the full build.

---

## 3. The incremental build plan (each step: touch points, gated cvar, device validation,
success + kill criteria)

**Cvar (add first, follow `gpu_trace_cullable_tris` exactly, `gpu_flags.cc:413`):**
`DEFINE_bool(gpu_cull_backface, false, "...default OFF; bit-identical when off...", "GPU")` in
`gpu_flags.cc` + decl in `gpu_flags.h`. Add to the EmulatorActivity `--ez` allowlist
(`android/.../EmulatorActivity.java`, the `copyBooleanExtra` list). Remember the device TOML override
gotcha (CLAUDE.md "Config gotcha") — an `--ez` extra beats the persisted config.

### Step 1 — compaction plumbing + NO-OP cull (renders identically; device A/B for byte-identity)
- **What:** add `cull_index_pool_` (a second `VulkanUploadBufferPool` with
  `VK_BUFFER_USAGE_INDEX_BUFFER_BIT`), `CanCullDraw()` gates (§3-gates, but the per-triangle test always
  "survives"), and `EmitCulledDraw()` that copies EVERY index of a `kTriangleList` kGuestDMA draw into
  the ring and binds+draws from it. Drops nothing.
- **THE FILL RECIPE (this is the FATAL bug both verdicts caught — get it right):** the emitted bytes
  must be the **RAW guest index bytes, VERBATIM** — a byte-subset copy of the guest index stream, same
  width and endianness as the standalone bind at `vulkan_command_processor.cc:4029-4030`. Do **NOT**
  swap, mask, or offset the emitted bytes. The kGuestDMA path binds the raw guest buffer and the VERTEX
  SHADER itself applies `EndianSwap32Uint` (`spirv_shader_translator.cc:1592`) and `+vertex_base_index`
  (`:1606`); `vertex_index_endian`/`vertex_base_index` are live system constants the cull does NOT
  change. A pre-swapped/pre-masked index would be swapped/offset AGAIN in the VS = corrupt geometry. The
  `GpuSwap(raw,endian)&0xFFFFFF + index_offset + clamp[min,max]` mapping
  (`draw_extent_estimator.cc:500-515`) is applied ONLY **internally**, to pick which vertex the
  transform fetches for the cull decision — never to the bytes written to the ring.
- **Touch points:** `vulkan_command_processor.h:603` add `cull_index_pool_`; new methods `CanCullDraw`/
  `EmitCulledDraw` near `:502-509`. `vulkan_command_processor.cc:351-367` create the pool (guarded by
  `cvars::gpu_cull_backface`), `:4891-4892` Reclaim, `:1316` reset, `:5260` `FlushPendingMergeRun` for
  mutual exclusion with merge/MDI. Insert the cull branch as the first test inside the indexed `else` at
  `:4025`, before the `vulkan_merge_draws` test at `:4049`. Ring `Request`/bind pattern mirrors the MDI
  head-emit at `:4180-4197`. Standalone fallback mirrors `:4225-4228`.
- **Ring sizing:** list-from-list = `idx_count * stride` (never exceeds source). (Strip→list in step 4
  needs `3*(num_indices-2)*stride` — the verdict's minor-but-real overflow fix.)
- **Device validation (UNVALIDATABLE-BLIND — needs the device):** frame must be **byte-identical** to
  default-off. Compare screenshot at matched `guest_ms` + equal `rendered` (the MEASUREMENT RULE) and
  `cullable_tris` oracle unchanged. Success: identical frame proves index read width/endian, ring
  Request/Reclaim lifecycle, bind offset, and the system-constant/vertexOffset invariant (the verbatim
  copy reproduces the standalone bind exactly). Kill: any pixel delta → recipe wrong, fix before step 2.

### Step 2 — backface-only cull behind the affine-MVP micro-kernel (the first real cull)
- **What:** per-triangle homogeneous-determinant backface test (`draw_extent_estimator.cc:583-592`),
  winding from §3-gate-3, vertex-kill IGNORED (§3-gate-1), W reconstructed when `vtx_w0_fmt==0`
  (§3-gate-2). List-only. Surviving triangles' raw indices → ring.
- **Touch points:** `EmitCulledDraw` calls `transform(vertex_index) → {x,y,z,w}` (the §2 micro-kernel)
  per unique vertex; reuses the per-triangle math at `draw_extent_estimator.cc:583-592` but does NOT
  copy the vertex-kill drop at `:521-524` and DOES reconstruct W (which the counter omits).
- **Device validation (UNVALIDATABLE-BLIND):** the surviving-drop count must track the existing
  `cullable_tris` counter (they share the backface math) → the counter is the **oracle** for how many
  triangles step-2 drops. Screenshot must be **visually identical** (backface culling is invisible on
  closed meshes). Success: identical screenshot + `gpu_frame_us` drop at matched `guest_ms`/`rendered`.
  Kill: ANY visible delta → cvar off (the bit-exactness kill criterion, `cull-and-batching-scope.md:131`).
- **det-sign resolution (verdict's major fix):** the `det>0 ↔ CCW` mapping
  (`draw_extent_estimator.cc:553-556`) assumes guest clip-space Y; if the device A/B shows the survivor
  set is the COMPLEMENT (front faces dropped instead of back), flip the det sign. Resolve empirically
  here against the counter oracle.

### Step 3 — + XY frustum with guard-band margin
- **What:** add the same-side clip-plane test (`draw_extent_estimator.cc:577-580`) but with a guard band
  (test `x > w*(1+m)` not `x > w`) so guard-band-visible triangles are never dropped. **Keep Z-plane
  culling OUT** (near/far + guard-band are the riskiest; backface+XY is ~the prize).
- **Device validation (UNVALIDATABLE-BLIND):** screenshot A/B at MULTIPLE scene points (frustum edges
  are content-dependent). Success: identical frames + further `gpu_frame_us` drop. Kill: any edge-of-
  screen geometry pop → reduce guard band or revert to backface-only.

### Step 4 (optional, later) — strip→list expansion
- **What:** expand surviving strip triangles to a list so strips (the BULK of BD —
  `prim[... ts=2009 ...]`) can be culled at all. **This is where BD actually benefits** — steps 1-3
  list-only cull ZERO BD draws (the verdict's major point: BD is strip-dominated). Emit list triangle
  `(i,i+1,i+2)` for even `i`, `(i+1,i,i+2)` (one swap) for odd `i` to normalize winding to the list
  convention. Gate on `vulkan_dynamic_state_topology` so list/strip share one pipeline
  (`gpu_flags.cc` topology lever); ring sized `3*(num_indices-2)*stride`.
- **Device validation (UNVALIDATABLE-BLIND):** identical screenshot + the real BD `gpu_frame_us` win.

**Which steps are unvalidatable-blind:** ALL device-validation steps above need the physical Thor — the
build compiles/links as primary verification (CLAUDE.md), but byte-identity (step 1), invisible-backface
(step 2), frustum-edge (step 3), and the BD payoff (step 4) are each only confirmable on-device, gently,
content-matched, force-stopped if the GPU thrashes. Plan one supervised launch per step, not a loop.

**Gate before step 2-4 even starts:** the per-shader affine-MVP classifier counter (§2) must show a
non-trivial qualifying fraction of BD's strip draws on-device. If it's near zero, stop — the cull is a
NO-GO for BD and the effort goes to Lever 1 (§4).

---

## 4. Ranked OTHER levers for the 24h if the cull stalls (scope:other-hw-levers)

| # | Lever | Touches per-draw / binning cost? | Build cost | Verdict |
|---|---|---|---|---|
| **1** | **Corrected zero-pad `vkCmdDrawIndexedIndirectCount` MDI** | YES — collapses N draw *calls* → 1, attacking the per-DRAW-FIXED cost skip16 proved REAL | **LOW (hours)** — `multiDrawIndirect` enabled, MDI ring + `merge_mdi_count_` + `FlushPendingMergeRun` all exist (`vulkan_command_processor.cc:351-363,4180-4208`). Net-new: one `device_khr_draw_indirect_count.inc`, ext registration, drive `drawCount` from live `merge_mdi_count_` instead of fixed `mdi_max_draw_count_` (`:4196`, the padding flaw) | **BUILD FIRST — cheapest fps-mover.** The padded MDI lost (`mdi-device-ab-result.md`) because 32-slot zero-padding added ~6468 no-op fetches; the Count variant removes them, flipping the sign. Expected break-even to ~+3%, more if per-draw-fixed is large (which skip16 says it is). Device A/B for ≥0. |
| **2** | **CPU/NEON affine-MVP cull** (§1-3) | YES — removes per-triangle binning | High (new micro-kernel + compaction + bit-exact gates) | **BUILD SECOND, conditional.** No longer the sole lever. Carries the bit-exact landmines + must measure the qualifying fraction first. |
| 3 | EDS dynamic-state pipeline collapse | NO — cuts binds, not binning | n/a | **DONE + confirmed not the bottleneck.** Only 259-262 pipeline_binds vs ~2180 draws (baseline). No lever. |
| 4 | Already-NEON CPU paths (byte_swap, a64 V128) | NO — CPU is ~85% idle on this GPU-bound frame | n/a | **Exhausted as standalone levers.** They ARE the engine to reuse for the §2 micro-kernel, but don't move a GPU-bound frame on their own. |
| 5 | Adreno driver feature (LRZ / load-store) to cut binning | NO — LRZ rejects at fragment stage AFTER binning; load/store is ~3% tile traffic | n/a | **No lever.** `gpu_edram_passes_dont_care` already flips loadOp/storeOp; binning front-end is hardware, driver-independent (`turnip-driver-scope.md §4`). |

**Order for the 24h:** (1) corrected MDI Count — hours, surest, attacks the larger cost → device A/B.
(2) affine-MVP classifier counter on-device → if qualifying fraction non-trivial, build the cull steps
1-4. Drop levers 3/4/5.

---

## 5. Honest expected fps outcome range

Baseline device frame: `gpu_frame_us ≈ 680474` (~1.8 fps proxy, vdswap 27/15s). The skip16 frame ran
`gpu_frame_us ≈ 498341-545355` (~2.27 fps) — that is an *unsafe* lever (drops real tiny draws, may drop
visible geometry) but it BOUNDS what attacking the per-draw-fixed cost can do: **~20-27% frame-time, a
1.25-1.4× fps proxy, from the per-draw-fixed component alone.**

- **Lever 1 (corrected MDI Count):** removes the no-op-fetch overhead that sank padded MDI; captures
  part of the per-draw-fixed cost WITHOUT dropping any draw. Honest: **break-even to ~+10%**, plausibly
  toward the high end given skip16 (most likely **~+3-8%**, i.e. ~1.9-2.0 fps proxy). Cheap to confirm.
- **Lever 2 (affine-MVP backface+XY cull, step 4 strips):** prize bounded by `0.56 × per-triangle
  share`. After subtracting the ~33ms fixed floor and the large per-draw-fixed term, honest estimate
  **~+10-20% frame-time IF the qualifying-draw fraction is high AND binning ≫ fragment** — both
  UNMEASURED. Could be **near zero** if BD's strip draws mostly don't classify as affine-MVP, or if the
  per-stage split shows fragment-bound. Realistic: **2.0-2.7 fps proxy on a good outcome; no movement on
  a bad one.**
- **Stacked best case (both levers land + favorable per-stage split):** roughly **2.5-3.0× the baseline
  fps proxy** (≈ 4.5-5.5 fps from ~1.8). **Honest expected case:** **~1.3-1.8× (≈ 2.3-3.2 fps)** — Lever
  1 nearly certain at the low end, Lever 2 adding the rest only if its two unmeasured gates pass.
- **This does NOT reach "full speed."** The ~33ms fixed-floor + per-draw-fixed residue keeps BD well
  under 60fps even with both levers. Full speed needs the deeper re-arch
  (`20260531-thor-gpu-binning-rearch-plan.md` — draw concatenation + CPU pre-transform), which is beyond
  a 24h window. **Do not promise full speed from the cull.** The 24h deliverable is a real, measured
  step (Lever 1 certain, Lever 2 conditional), not the finish line.

---

## Key file:line evidence (the build + decision surface)

- §A gate device result: `docs/evidence/20260602-130009-skip-tiny16.txt:14-51` (skip16 frame
  gpu_frame_us 498341 @ guest_ms=121138, vtx[tiny=931]) vs baseline
  `docs/evidence/20260602-104215-baseline-head-6526e91.txt:16` (gpu_frame_us=680474, vtx[tiny=949]).
- Interpreter cost: `cpu_other_us=920935` `docs/evidence/20260602-122319-cullable-strip-size.txt`;
  per-vertex driver loop `draw_extent_estimator.cc:483-519`; `Execute()` CF re-walk
  `shader_interpreter.cc:27-36`; scalar operand assembly `:321-330`; `FlushDenormal`
  `shader_interpreter.h:108`; per-access constant memcpy `shader_interpreter.cc:261-285`; virtual
  `Export` `:956` → `draw_extent_estimator.cc:76`.
- FATAL fill recipe: kGuestDMA raw bind `vulkan_command_processor.cc:4029-4030`; VS applies swap
  `spirv_shader_translator.cc:1592` + base `:1606`; internal index mapping (NOT for emitted bytes)
  `draw_extent_estimator.cc:500-515`.
- FATAL 32-bit DMA reload (gate the cull off): `shader_32bit_index_dma`
  `vulkan_command_processor.cc:3868-3874`; VS reload path `spirv_shader_translator.cc:1533-1556`;
  `fullDrawIndexUint32` must be read on-device.
- Cull bit-exact gates: vertex-kill drop to NOT copy `draw_extent_estimator.cc:521-524`; host ignores
  vertex-kill `vulkan_pipeline_cache.cc:907-908`; W-reciprocal `spirv_shader_translator.cc:1635-1648`;
  raw-W test (counter omits reconstruction) `draw_extent_estimator.cc:535,574`; backface determinant +
  strip winding parity `:583-606`; origin_bottom_left/viewport-Y `vulkan_command_processor.cc:3739-3743`.
- Cull integration surface: insert branch `vulkan_command_processor.cc:4025`; ring Request/bind pattern
  `:4180-4197`; standalone fallback `:4225-4228`; pool create/reclaim/reset `:351-367,4891-4892,1316`;
  `FlushPendingMergeRun` `:5260`; header `vulkan_command_processor.h:603,502-509`;
  `VulkanUploadBufferPool::Request` `ui/vulkan/vulkan_upload_buffer_pool.h:26-27`.
- Lever 1 MDI: ring + live count `vulkan_command_processor.cc:351-363,4167,4180-4208`; fixed-count
  padding flaw `:4196`; `multiDrawIndirect` enabled `vulkan_device.cc:628`.
- Cvar pattern: `gpu_flags.cc:413` (cullable-tris), `:340` (skip-draws); add `gpu_cull_backface` here +
  `gpu_flags.h` + EmulatorActivity allowlist.
