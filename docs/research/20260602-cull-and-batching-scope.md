# Scope: GPU-compute triangle cull + corrected draw batching (2026-06-02)

From a 9-agent adversarially-verified workflow (wf_85e2eec4-e50; synthesis agent died on an API
error, so this is hand-synthesized from the four verify verdicts — all "viable-with-fixes" or
"not-viable", high confidence, file:line-grounded). Two user directives: (1) scope the GPU-compute
triangle cull; (2) "draw batching is NOT dead — we did it wrong." The honest answers reframe both.

## TL;DR
- **The GPU-compute cull is NOT viable as posed** — it re-pays the binning's position transform on
  the bottlenecked GPU AND forces a per-draw render-pass break (GMEM store/reload ×~2000/frame). The
  viable form of the cull is on the **idle CPU with NEON** (your hint), drop-only, fed via UMA.
- **Batching is not dead — but whether the *corrected* version wins is unmeasured**, and there is a
  **ZERO-BUILD experiment** (already-shipped `gpu_skip_draws_below_verts`) that decides it before we
  build anything. That experiment is the single most important next step.
- Bit-exactness has **two FATAL traps** in the existing cull reference math that would drop visible
  geometry if copied (vertex-kill, W-reciprocal). Both have concrete fixes.

---

## A. THE DECISIVE CHEAP EXPERIMENT (do this FIRST — no build, one gentle launch)
Both levers hinge on the same unknown: **is the ~297µs/draw mostly per-DRAW-fixed (context roll /
bin restart — coalescable, batching wins) or per-TRIANGLE binning (not coalescable — only a cull
wins)?** Neither MDI nor a Count A/B can isolate this (each sub-draw still bins independently — proven
in docs/research/20260602-mdi-device-ab-result.md:42). But the **already-shipped** `gpu_skip_draws_below_verts`
cvar (vulkan_command_processor.cc:4004) can:

- Content-matched (unfrozen, post-hoc on rendered/avg_vertices), drop the ~978 tiny draws
  (`vtx[tiny=978]`) and read Δgpu_frame_us:
  - falls ∝ **draws removed** → per-draw-fixed cost is REAL → corrected batching can win → build it.
  - falls ∝ **triangles removed** only → binning floor dominates → batching is low-value → the cull
    is the lever.
- Caveat: skipping tiny draws removes BOTH draws and (few) triangles; pair with the cull counter
  (cullable_tris) reading to attribute. One launch, no build, harness-guarded.

This replaces the workflow's originally-proposed "Count A/B isolates per-draw-fixed" — which the
adversarial verify proved CANNOT isolate it (FATAL).

---

## B. CULL — bit-exactness (verdict: viable-with-fixes; the per-triangle decision is NOT
provably non-over-culling as the counter stands)
The cull DECIDES what rasterizes, so any over-cull = visible geometry vanishes. Hard gates before any
cull (not just the counter) is trustworthy:

1. **FATAL — vertex-kill OVER-cull.** The Vulkan backend does NOT implement guest vertex-kill
   (`has_vertex_kill_and=0`, vulkan_pipeline_cache.cc:907-908; KILLVERTEX only feeds point-size,
   spirv_shader_translator.cc:2654-2658) — the live renderer KEEPS killed triangles. But the counter
   DROPS them (draw_extent_estimator.cc:521-524). **Fix: a cull must NOT honor vertex-kill** (treat
   killed verts as live) to match the host. (The counter's current behavior is fine as a *fraction*
   estimate, fatal as a *culler*.)
2. **FATAL — W-reciprocal trap.** ShaderInterpreter exports raw ucode W (shader_interpreter.cc:944-959);
   the host applies W=1/exported_w when `PA_CL_VTE_CNTL.vtx_w0_fmt==0` (spirv_shader_translator.cc:1635-1648).
   The counter tests raw W and only guards `vtx_xy_fmt`, never `vtx_w0_fmt` → arbitrary over-cull. **Fix:**
   reconstruct W=1/w when vtx_w0_fmt==0, or bail the draw. (Option A below fixes it for free.)
3. **MAJOR — backface determinant sign** is correct only because `origin_bottom_left=false`
   (vulkan_command_processor.cc:3740) + front_face mapping today. Derive winding from the SAME viewport-Y
   sign the host uses; assert it; device-A/B the survivor set.
4. **MAJOR — strip winding parity** is not re-based after primitive restart (strips stay strips on
   Vulkan, primitive_processor.h:144-147). **Fix:** exclude primitive-restart strips (fall back to
   normal draw), as the merge path already does.
5. **MAJOR — frustum.** Bail on `PA_CL_CLIP_CNTL.clip_disable` (screen-space draws). Keep **Z-plane
   culling OUT**. Ship **backface-only first** (exact, ~half the prize); add XY frustum only with a
   guard-band margin + screenshot A/B.
6. **MINOR** — user clip planes ignored by both sides (safe today; document the coupling). poly_mode
   points/lines: skip face cull.

## C. CULL — performance (verdict: NOT viable as a per-draw GPU-compute dispatch)
1. **FATAL — re-pays the binning transform on the bottlenecked GPU.** The cull dispatch runs a
   position transform per vertex = exactly what the Adreno binning pass already does (LRZ culls
   occlusion at fragment stage, NOT vertices/backfaces). Transforming ~100% of verts to drop ~56% of
   triangles ≈ pays what it saves. Same wrong-axis trap as MDI. Net win requires post-transform binning
   SETUP to dominate the transform — UNCONFIRMED without a Snapdragon per-stage capture.
2. **FATAL — per-draw compute dispatch forces a per-draw render-pass break.** `SubmitBarriers(true)`
   calls `EndRenderPass()` (vulkan_command_processor.cc:2846/2868); IssueDraw re-enters the pass per
   draw (:3985-3987). On a TBDR each break = a GMEM tile store+reload → ~2000/frame = the brk_open ~12%
   cost class. Batching the cull is MANDATORY (not the design's "optional step 4") and is a major
   re-arch on strip-heavy ~2000 churning draws.
3. **MAJOR — "reuse the VS→compute path" is false.** The translator emits only Vertex/TessEval/Fragment
   execution models (spirv_shader_translator.cc:807-834); the "VS as compute" site is an unimplemented
   TODO (vulkan_command_processor.cc:3489-3496). A GPU position-only compute VS is net-new translator
   work (new GLCompute model + LocalSize + position-only DCE + new pipeline-cache entry), and
   bit-exactness-critical (cf. arm64_vmx_dot/fp16 black-screens).

### → The viable cull form: CPU/NEON conservative drop-only cull (the recommended direction)
The CPU is ~85% idle on this GPU-bound scene (CLAUDE.md). Transform unique strip vertices ONCE on the
X3/A715 with NEON, apply the bit-exact backface (+later XY-frustum) test, emit a compacted index buffer
via the 16 GB UMA, and issue the guest draw over fewer indices. This removes backfaces from the GPU
binning transform WITHOUT re-paying it on the GPU, and uses the idle silicon. It must obey ALL the §B
gates (no vertex-kill, W-reciprocal handled, clip_disable/restart-strip bail, backface-only first).
Risk: CPU transform cost vs the ~927ms/frame the *interpreted* replay showed — the production path must
be a fast NEON transform (not the ShaderInterpreter), and amortized (transform shared vertices once,
not per-triangle). This is a different, larger design than the GPU-compute one — scope it next if §A
shows the binning floor (per-triangle) dominates.

## D. BATCHING — corrected zero-padding drawIndirectCount (verdict: viable-with-fixes)
- **You are right the padding was the flaw.** Verified: the 32-slot head-emit is **73.5% no-op
  commands** (238 runs / 275 heads / 8800 slots / 6468 no-ops — recomputed from the device run-length
  histogram, matches to the digit). That overhead is the prime suspect for the +8-14% loss.
- The corrected variant — `vkCmdDrawIndexedIndirectCount` with a CPU-written count (no padding) — is
  **feasible and cheap**: the function is Vulkan-1.2-core and the Thor is 1.3 (guaranteed present);
  add `device_khr_draw_indirect_count.inc` (the established `XE_UI_VULKAN_FUNCTION_PROMOTED` pattern),
  register the ext, add the deferred command, drive drawCount from the live `merge_mdi_count_`. Skip
  the <1.2 fallback (dead code on this one device). multiDrawIndirect already enabled; pool already has
  INDIRECT usage.
- **HONEST expected range: break-even to ~+3% most likely** (just removing the 6468 no-op fetches
  flips the loss sign); **+9-13% ONLY if** the per-draw context-roll is both real AND shared across MDI
  sub-draws — which is the §A unknown. So: **run §A first; build the Count variant only if §A shows a
  non-trivial per-draw-fixed share.**

## E. Feasibility cheat-sheet (what exists vs what's net-new; black-screen-risk flagged)
- Compute dispatch — **GO** (in production: texture cache, RT cache).
- Atomic/SSBO compaction — **GO** (memexport atomics proven).
- Per-frame INDIRECT(+STORAGE) ring — **GO** (indirect_buffer_pool_ exists; one-flag OR for STORAGE).
- vkCmdDrawIndexedIndirectCount — **addable, LOW risk** (1.2-core, device 1.3, isolated behind the
  existing default-off cvar).
- GPU VS→compute — **net-new translator work** (no GLCompute path today); CPU ShaderInterpreter replay
  is the only existing position-only mechanism.
- **buffer_device_address — ABSENT; DO NOT design around it.** Enabling it needs a new
  VkPhysicalDeviceFeatures2 pNext struct = the exact feature-chain change class that black-screened all
  games (UMA present-hang). Use descriptor-bound SSBOs instead.

## Recommended order (cheapest-decisive first; bit-exact/guest-visible discipline throughout)
1. **§A: `gpu_skip_draws_below_verts` content-matched A/B** — NO BUILD, one gentle launch. Decides
   per-draw-fixed vs per-triangle. THE gate.
2. If per-draw-fixed is non-trivial → **build the zero-padding `drawIndirectCount` Count variant**
   (§D), default-off, device A/B for ≥0 (flips the padded loss).
3. If per-triangle/binning dominates → **scope the CPU/NEON drop-only cull** (§C-viable + §B gates),
   backface-only/list+strip(no-restart)/exact first, screenshot+oracle A/B each step.
4. **Ask the user for one Snapdragon Profiler per-stage capture** (binning vs fragment vs transform) —
   the one thing that resolves §A's deepest uncertainty definitively and headlessly-impossible here.
5. Kill criteria: cull over-culls (any screenshot delta) → off; corrected MDI not ≥0 on device → off.
