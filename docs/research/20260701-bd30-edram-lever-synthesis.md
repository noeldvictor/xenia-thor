# BD-30 EDRAM lever synthesis (2026-07-01)

Consolidates this session's device + research findings into an actionable next-session plan
and an honest ceiling analysis. Goal unchanged: Blue Dragon 30fps via the EDRAM/fusion solve;
other games 30-60. Do NOT re-walk the dead-ends below.

## The problem, precisely
BD's field renders ~42 render passes, most breaking because post-process **composites read a
producer RT from an EARLIER pass at a TRANSFORMED texcoord** (not same-pixel). On the Adreno 740
TBDR each such break evicts GMEM to DRAM + reloads (the ~74ms tile-I/O, device-isolated via
`gpu_edram_passes_dont_care` 128->49ms). Desktop immediate-mode GPUs ("fable") pay none of this —
they read the producer from VRAM directly. That architectural gap is the whole difficulty.

## Lever verdicts (evidence-backed — do not re-chase the DEAD ones)
- **Same-pixel fusion (feedback_merge / VK local_read / ROAA): DEAD for BD.** Composites sample at
  transformed texcoords; subpassLoad/input-attachment force gl_FragCoord => garble (device: prior
  `merges=1 of 26` + block corruption). 3-agent research confirms the law: offset/transformed
  sampling cannot use any same-pixel on-chip read. Turnip also lacks `dynamic_rendering_local_read`.
- **cap=1 (MSAA 2x->1x, -42% GPU): DEAD (intrinsic garble).** EDRAM tile addressing is tied to the
  sample count; halving samples changes tiles-per-row so shared guest bases (2x main @0, 4x effects
  @810) alias. Not fixable per-site or by base-remap (addressing is sample-coupled).
- **`inpass_edram_transfers=1`: SAFE but MODEST.** Device: `inpass[x=22]` folds 22 transfer-dest
  passes, renders correctly, but `brk_img_sr` stays 42 — it folds the transfer PASS, not the
  RT-source->SHADER_READ barrier. Keep as a stacking micro-lever; not a BD-30 lever alone.
- **skip_bloom / consumer-draw skips: INERT.** Xenia has no lazy-resolve; the composite PASS runs
  even with 0 draws (opaque dst=ZERO). Only producer-PASS elimination helps.
- **Full-atomic buffer path for the WHOLE scene: likely LOSES.** Software ROP on BD's heavy overdraw
  (844 draws) is costlier than the tile-I/O it saves (Xenia ships host-RT because "the FSI path is
  much slower now"). So do NOT route the main scene through the buffer.

## The correct architecture (the hybrid — partially built, default-off)
Main overdraw-heavy scene stays **host-RT (hardware ROP, GMEM)**; the **zero-overdraw composites**
(full-screen, 1x coverage, races harmless) route through the **EDRAM buffer path** with
arbitrary-address reads (handles the transformed producer reads). Built: `gpu_vulkan_hybrid_postprocess`
renders BD correctly with brk_img_sr 42->21. This is the right shape and the user's "fusion pipelines."

### Actionable next-session steps (device COOL, fresh context — the SPIR-V is delicate)
1. **Complete composite routing:** route all 26 `rtfc` composites (not just 21) through the buffer
   path. Detection = the existing fscomp classifier; the remaining ~5 are interleaved with depth/shadow
   passes (harder ordering). Target brk_img_sr 42 -> ~16.
2. **Atomic-ROP core for composites only** (races are harmless for 1x-coverage composites, so this is
   LOW-risk vs the main scene): drop `OpBegin/EndInvocationInterlockEXT` (done, gated on
   `edram_fsi_no_hardware_interlock_` at spirv_shader_translator_rb.cc:734/1316); the depth/color RMW
   for composites needs no atomics (barrier-separated). So the composite buffer path may need NO
   atomicMin at all — validate the plain-store composite path renders correctly first (cheapest test).
3. **Do NOT naively stack `inpass_edram_transfers=1` with hybrid** — device-tested 2026-07-01:
   they FIGHT over pass boundaries (hybrid ends the host pass per composite; inpass folds transfers
   INTO the guest pass) → brk_img_oth exploded 21->254, pass_break_barrier 15->137, + black-streak
   CORRUPTION. To combine, inpass must be suppressed on the passes/frames the hybrid rewrites, or the
   composite routing must not fragment transfer passes. inpass ALONE (clean host-RT) is safe.
4. Measure with a SINGLE-RUN alternating A/B (never cross-run gpu_frame_us).

## CEILING RE-ESTIMATE (2026-07-01, corrects my own pessimism — compute core may be SUFFICIENT for 30)
My earlier "fusion+cap = ~15fps ceiling" rested on the img_sr BREAK-OVERHEAD proxy (~0.6ms/break -> ~13ms for
the composites). That UNDERCOUNTS: the device-isolated `gpu_edram_passes_dont_care` test measured ~74ms of
tile-I/O, and the composites' FULL cost is the store+reload of their oversized tile-rounded RTs, not just the
per-break barrier. So compute-fusing the composites (keeping intermediates on-chip in LDS, zero render passes)
plausibly saves TENS of ms, not ~13ms. If it recovers even half of the 74ms, BD goes ~103ms -> ~65ms (~15fps);
if it approaches the recipe's "74ms->5-10ms", BD -> ~35-45ms (~22-28fps), and STACKED with clean cap=2 MSAA +
VRS could cross 30. CONCLUSION: the compute-post-process is not merely the ceiling-breaker — it is PLAUSIBLY
SUFFICIENT for BD-30. The exact save is unmeasured (needs the build); do NOT anchor on the old ~15fps number.
The residual below still applies to the NON-composite work (geometry/main-scene), but that is the smaller part.

## Honest ceiling (why even complete fusion may not reach 30)
Device decomposition: complete composite fusion removes the composite tile-I/O (~13-26ms), but a
**~66ms residual of NECESSARY rendering remains** (geometry/binning ~19ms + per-sample hardware ROP
on the overdraw + the main-scene depth/shadow-pass tile-I/O + clears). 33ms (30fps) requires cutting
that residual too. Candidate residual levers (separate, unproven): safe color STORE_OP_NONE elision
(extend `gpu_vulkan_skip_unused_depth_store` to color), foliage overdraw reduction (VRS ships -22%),
and — the one research-grade direction that could break the ceiling — **compute post-process**: run
the composites as compute dispatches reading/writing the EDRAM buffer, eliminating their passes
entirely (needs a compute variant of the translated composite shaders = the "frame EDRAM-I/O
recompiler"; large but the only path that removes composite passes without any ROP tradeoff).

## Other games (requirement #2) — status
Regression fix `52c16faff` restored the default host-RT init path (was SIGABRT-ing every non-hybrid
launch). Device-validated in-target + correct: **Burnout Revenge** (~30-42fps, Car Select) and
**Back to the Future** (31.6fps, DeLorean/mall). Untested/available: Gears 1/2/3/Judgment, Lost
Odyssey, Infinite Undiscovery, Magna Carta 2, Project Sylpheed (spot-check when device is cool).
Device-note: the 64C thermal watchdog tripped 3x this session — heavy frames need extended rest between fires.

## Compute-post-process implementation recipe (the ceiling-breaker — 2026-07-01 research)
The one path that removes composite passes with NO ROP tradeoff: run the zero-overdraw post-process
composites as COMPUTE dispatches over the EDRAM SSBO instead of render-to-texture passes. Concrete recipe:
- **Downsample/bloom chain -> ONE dispatch (AMD FidelityFX SPD pattern):** 256-thread workgroup, 64x64
  input tile/group, LDS `float4[16][16]`, hierarchical 2x2 reductions produce up to 12 mips in a single
  dispatch with ONE inter-workgroup sync (atomic last-group finishes the low mips). Box/tent kernel =
  bloom needs no separate blur pass. This alone collapses the multi-pass downsample.
- **Upsample+composite -> a 2nd dispatch:** 8x8 groups, read the mip pyramid, 3x3 tent, additive/blend write.
- **Fragment->compute translation table (mechanical):** gl_FragCoord.xy -> gl_GlobalInvocationID.xy;
  texture(uv) -> imageLoad(ivec2(uv*size)) (SSBO random read ~= texture sampling perf on Adreno, TLB-backed
  — so the TRANSFORMED producer reads are fine in compute); ROP blend -> explicit imageLoad+combine+imageStore
  (additive can be unordered; ordered blend needs interlock/atomics, but composites are barrier-separated so
  plain RMW is fine). HARD CONSTRAINTS that BREAK the translation: dFdx/dFdy (NOT in compute — pass precomputed
  or neighbor-sample), `discard` (-> early return, no store), per-sample MSAA (resolve input first),
  interpolated varyings (must feed via per-pixel SSBO). BD's composites are simple full-screen sample+blend =
  inside the "works" set; audit each composite shader for dFdx/discard/MSAA before converting.
- **On-chip residency:** `VK_QCOM_tile_memory_heap` would keep intermediates in GMEM across dispatches, BUT it
  is ABSENT on this Thor Turnip build (device-enumerated, CLAUDE.md gap audit) — NOT required: SPD keeps its
  mips in LDS WITHIN one dispatch, so the fusion works without it (just uses device-local images between the
  2-3 dispatches). Re-verify the extension list before relying on it.
- **Precedent:** Xenia's own EDRAM render-target-cache rework already gave 3.4x (Halo3 menu 79.8->23.5ms) by
  eliminating copies — same "kill the round-trip" lever; compute fusion extends it to the passes the cache can't.
- **BUILDING BLOCKS CONFIRMED (2026-07-01, in-repo — the core is NOT blocked on missing infra):**
  - xenia ALREADY runs compute post-process: `src/xenia/gpu/shaders/apply_gamma_pwl.cs.xesl` (+ `_fxaa_luma`)
    apply gamma/FXAA as a COMPUTE dispatch over the framebuffer. Compute post-process is precedented here.
  - EDRAM-buffer compute template: `src/xenia/gpu/shaders/resolve_clear_32bpp.xesli` — 8x8 workgroup, binds the
    EDRAM byte_buffer (set0 binding0/1), `XeEdramOffsetBytes(...)` address calc, `byte_buffer_align16_store16_xe`.
    A composite-compute shader mirrors this but READS the producer address + WRITES the blended dest.
  - Compute-pipeline-over-EDRAM host template: `resolve_fsi_clear_32bpp_pipeline_` in vulkan_render_target_cache.cc
    (CreateComputePipeline + `descriptor_set_layout_storage_buffer_` + push consts) — mirror for the postproc pipeline.
  - Toolchain (VERIFIED 2026-07-01): glslangValidator IS available (`/c/Program Files/RenderDoc/plugins/spirv/`).
    BUT the `.xesl` macro framework (byte_buffer_align16_store16_xe, entry_bindings_begin_compute_xe, etc.) is
    resolved by xenia's SHADER-PRECOMPILE build step (the `xenia-gpu-shader-compiler` target + premake), which
    emits the committed `bytecode/vulkan_spirv/<name>_cs.h`. A standalone glslang call does NOT work (the
    `scratch/xesl_wrap_test` probe fails on the framework includes). So adding a compute shader = author the
    `.xesl` + run xenia's shader-precompile flow (multi-session setup, but confirmed-doable — the flow exists).
  - DEFINITIVE COMMAND (2026-07-01): committed `_cs.h` say `// Generated with \`xb buildshaders\``. So the flow is
    **`xb buildshaders`**, which needs the HOST `xenia-gpu-shader-compiler` tool BUILT (NOT built here — no binary
    found) via the HOST toolchain (premake5 + MSVC — neither in PATH; this env builds the Android APK via gradle,
    a different toolchain). `xb`/`xb.bat` + premake5.lua exist at root. So STEP 0 of the compute build = provide
    premake5 + a host C++ compiler, build the shader-compiler tool, `xb buildshaders`. That is the concrete first
    multi-session task, BEFORE any composite logic — the reason the compute core cannot be finished in one sitting.
- **Build order (next session, COOL device, validate each step on-device):** (a) pick ONE simple full-screen
  composite (tonemap), emit a compute variant, dispatch it into the EDRAM SSBO, confirm pixel-correct vs the
  render-pass version; (b) extend to the bloom downsample via SPD; (c) route the rest, measure pass-count +
  single-run A/B gpu time. Expected order-of-magnitude cut on the composite tile-I/O (~74ms -> ~5-10ms) IF the
  residual geometry/ROP doesn't dominate (the honest ceiling above still applies to the non-composite work).
