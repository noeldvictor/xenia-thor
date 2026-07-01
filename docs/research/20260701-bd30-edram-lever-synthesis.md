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
  - ⭐ CORRECTION (2026-07-01, VERIFIED — the toolchain is NOT actually a blocker): the `_cs.h` are just SPIR-V
    bytecode arrays. A hand-written PLAIN-GLSL compute shader that binds the EDRAM SSBO directly (set0/binding0,
    std430 `uint xe_edram[]`, push const offset/count) COMPILES with the available RenderDoc glslangValidator
    (`-V --target-env vulkan1.2`) — verified: `scratch/compute_probe/edram_identity.comp` -> 1504-byte `.spv`.
    Hex-dump that `.spv` into the `_cs.h` uint-array format and the experimental compute path needs NEITHER
    `xb buildshaders` NOR the host tool NOR xesl. So the next session starts DIRECTLY at composite-compute LOGIC.
    The compute<->EDRAM-buffer PLUMBING (compute pipeline + storage-buffer descriptor + dispatch) is ALREADY
    proven in-tree by `resolve_fsi_clear_32bpp_pipeline_` (a working compute dispatch over the EDRAM SSBO), so the
    remaining novel work is: author the composite op in GLSL (per-composite, or a generic fragment->compute) +
    dispatch it in the hybrid context in place of the composite fragment draws + sync + device-validate.
- **Build order (next session, COOL device, validate each step on-device):** (a) pick ONE simple full-screen
  composite (tonemap), emit a compute variant, dispatch it into the EDRAM SSBO, confirm pixel-correct vs the
  render-pass version; (b) extend to the bloom downsample via SPD; (c) route the rest, measure pass-count +
  single-run A/B gpu time. Expected order-of-magnitude cut on the composite tile-I/O (~74ms -> ~5-10ms) IF the
  residual geometry/ROP doesn't dominate (the honest ceiling above still applies to the non-composite work).

## ⭐ BRICK 1 BUILT (2026-07-01) — mid-frame EDRAM compute-dispatch foundation
The first concrete piece of the compute core is IMPLEMENTED (not just planned), default-off, in
`vulkan_render_target_cache.cc`:
- `scratch/compute_probe/edram_identity.comp` compiled -> `bytecode/vulkan_spirv/edram_identity_probe_cs.h`
  (376-word SPIR-V, hex-dumped via python, NO xb buildshaders — the toolchain-unblocked path, proven).
- cvar `gpu_vulkan_compute_postprocess_probe` (default false), allowlisted in EmulatorActivity.java.
- `compute_postprocess_probe_pipeline_[_layout_]` created UNCONDITIONALLY (all paths) reusing
  `descriptor_set_layout_storage_buffer_` (the EDRAM SSBO layout) + an 8-byte push const {offset,count}.
- `RunComputePostProcessProbe()` called at the END of `Resolve()` on the host-RT path (outside any render
  pass — the required precondition): `UseEdramBuffer(kComputeWrite)` -> bind EDRAM descriptor set -> bind
  pipeline -> push {0,64} -> `SubmitBarriers(true)` -> `CmdVkDispatch(1,1,1)`. The shader reads then writes
  back the SAME dwords => EDRAM byte-unchanged, so BD MUST render pixel-identical. Deliberately does NOT call
  `MarkEdramBufferModified` (identity => ownership stays "unmodified").
- **What it validates (the one genuinely-unproven thing):** a compute dispatch over the EDRAM buffer,
  synchronized against LIVE host-RT rendering, mid-frame on Turnip. `resolve_fsi_clear` only proved compute
  at FSI-resolve time on the FSI path; this proves it on the DEFAULT host-RT path. Once green on-device, the
  identity op is replaced by the real composite op (build order (a) above) — the plumbing is then done.
- **✅ DEVICE-VALIDATED (2026-07-01, commit c8ece5fc1).** BD, Turnip, full stack, cap=2, 720p
  (`scratch/thor-debug/bd_compute_probe.ps1`): dispatch PROVEN engaged (heartbeat log "recorded identity
  EDRAM dispatch #54000..#55800" — the cvar reaches C++ and the dispatch records, not early-returns), BD
  renders pixel-correct (opening-credits village scene clean), UNHANDLED=0, no VK_ERROR, no init-fail —
  robust across 54000+ dispatches in one session. **The dispatch + sync plumbing on the DEFAULT host-RT
  path is DONE.** Next = brick 2 below.
- **BRICK 2 (the fps lever) — TARGET NOW DEVICE-SCOPED (2026-07-01, classify capture on BD field):**
  `sr_cls[rtsrc=42 tex=0 fscomp=0 rtfc=28]` at `brk_img_sr=42`. So ALL 42 tile-I/O breaks are a just-rendered
  producer RT going ATTACHMENT->SHADER_READ (a producer read), and **28 of the 42 have a consumer that is a
  full-screen composite** (rect/quad, ps_hash!=0, writes color) reading that producer. The `EDRAM_FEEDBACK
  xfer` (PerformTransfers ownership-transfer) path logged NOTHING => BD's breaks are NOT ownership transfers,
  they are the guest's OWN composite draws sampling producer RTs. **Brick 2 = convert those ~28 full-screen
  composite guest draws to COMPUTE dispatches over the EDRAM SSBO** (read the producer from the SSBO at its
  EDRAM address — arbitrary-address read expresses the TRANSFORMED cross-pass texcoord that killed same-pixel
  input-attachments — run the guest composite ALU, write the dest into the SSBO; no render pass => no break).
  Mechanism = a COMPUTE-TARGET mode in SpirvShaderTranslator (gl_FragCoord->gl_GlobalInvocationID, producer
  texture-sample->EDRAM SSBO read, color export->EDRAM SSBO write). This is the research-grade piece; brick 1
  proved the dispatch+sync it rides on. FIRST cut = the SIMPLEST of the 28 (per-composite ps_hash from the
  fixed `IMG_SR break` detail grep in bd_feedback_inventory.ps1 — re-capture on a COOL device to pick it +
  confirm same-pixel vs transformed), emit its compute variant, dispatch in place of its fragment draws,
  confirm pixel-correct, measure the `brk_img_sr` drop + single-run A/B. Removing 28/42 breaks is the bulk of
  the ~74ms tile-I/O; stacked with cap=2 (+VRS) the ceiling analysis says that is plausibly sufficient for 30.
- **⭐ BRICK 2 COMPOSITE INVENTORY (2026-07-01, per-composite IMG_SR-break detail, BD field) — the authoring
  spec.** 16 distinct composite pixel shaders drive the 28 rtfc breaks (all consumer `prim=13 host_verts=6`
  quads or `prim=8 host_verts=3` tris; `oldlayout` 2=COLOR/3=DEPTH -> `newlayout` 5=SHADER_READ). Ranked by
  frequency, split by blend mode:
  - **OPAQUE `blendctl0=00010001` (src=ONE dst=ZERO = replace, NO dest read) — the simple majority, do FIRST:**
    `2E372EA28CC404B7` (top, prim8/3v, cm=000F), `1B132051B5504DA9` (prim13/6v), `1E70EB9513D670C9` (prim13/6v),
    `2A0674C564A8A8C5`, `0ABADD9DA4373CBA`, `05775DE8A2B0B3F5` (prim13/6v family, shared `vs_hash=A3431D6AB36AB469`).
    These are read-producer -> ALU -> write-dest, NO read-modify-write => trivially a compute store (no atomics,
    no blend). dst is `dst_color0_info=00030000` (RB_COLOR_INFO: base=0, fmt=3). This class is the first target.
  - **BLENDED `blendctl0=07060706` (src_alpha / inv_src_alpha = needs dest read+blend) — do LATER:**
    `9567C79307ACC6F5` (cm=000F), `5E6B8038D6E50B65` (cm=0007), `EE2DB831D964AFCA`. Compute must read dest from
    the SSBO, blend, write back (still no render pass; just a heavier op).
  - `ps_hash=0000000000000000` (14k samples) = depth-only / clear draws (cm=0000, fscomp=0) — NOT composites, ignore.
  - Producer `producer_img` ptrs repeat across a ~dozen RTs (0x76374f1470, 0x7637501c60, 0x76374fcf20, ...) =
    the bloom/blur pyramid ping-pong. Next data needed (COOL device): `--dump_shaders` to get `2E37..B7`'s guest
    microcode + texcoord (confirm same-pixel vs transformed) + map `producer_img` -> EDRAM base/pitch/fmt via the
    RT-cache image map. Then hand-author that ONE opaque composite's compute variant, intercept its draw, dispatch,
    validate pixel-correct + measure brk_img_sr 42->~41 (proof-of-concept for the class), then generalize.
  - NOTE: consumer-draw SKIPS are already device-proven INERT (memory: xenia has no lazy-resolve; the pass runs
    with 0 draws), so the ONLY lever is REPLACEMENT-by-compute, not skipping.
- **⭐⭐ BRICK 2 GUEST ALU CAPTURED (2026-07-01, dump_shaders on-device; ucode archived in
  docs/research/bd_composite_ucode/).** dump_shaders now allowlisted (EmulatorActivity.java); the target
  composites' Xenos microcode is in hand (filenames = the ps_hash = ucode_data_hash, confirmed same hash).
  Decoded:
  - `1E70EB9513D670C9` (11k) = **`tfetch2D r0, r0.xy, tf0; max oC0, r0, r0`** = a pure 1-TAP TEXTURE COPY
    (sample producer at interpolated texcoord, output it). **THE ideal first target** — minimal but it DOES
    read the producer, so it exercises the one novel thing (arbitrary-coord producer read in compute).
  - `2E372EA28CC404B7` (26k, top by count) = **`max oC0, r0, r0`** = passthrough of interpolant r0, NO tfetch
    => does NOT read the producer (a fill/resolve-target, not a resample). Skip as a demo despite being #1.
  - `1B132051B5504DA9` (15k) = **13-tap separable blur** (tfetch tf0 at r0.xy + c0..c12 offsets, weighted mad
    c13..c25, predicated early-outs). `0ABADD9DA4373CBA` = 9-tap multi-texture (tf0..tf8) accumulate.
    `2A0674C564A8A8C5` = 1-tap + tonemap (dp3/muls). `05775DE8A2B0B3F5` = DOF/bloom, log/exp + predicated
    multi-tap. All read via `tfetch2D` at INTERPOLATED `r0.xy` (NOT gl_FragCoord) => texcoord is TRANSFORMED,
    definitively confirming same-pixel input-attachments cannot express these (the compute rationale holds).
- **🚨🚨🚨 BUFFER-PATH FORCED + MEASURED ON THE HEAVY FIELD (2026-07-01, gpu_vulkan_edram_atomic=true, racy
  ROP) — MECHANISM PROVEN, but 4.7x SLOWER; the real blocker is per-pass SSBO SYNC, not correctness.** Forced
  the whole scene through the single EDRAM SSBO. Results (heavy field, 6 frames):
  - **✅ Ownership transfers ELIMINATED BY CONSTRUCTION: `n[xfer]` 35 -> 0, `gap_xfer` ~78ms -> 0.** The
    ~80ms-tile-I/O prediction's transfer half is confirmed removed exactly as reasoned.
  - **✅ RENDERS BD RECOGNIZABLY** (windmill/tower, character, rocks, "Microsoft Game Studios Presents"),
    ONE white-box race artifact (missing atomicMin depth/blend ordering — the racy path; atomicMin/task #31
    would clean it). Boots, UNHANDLED=0, no device-lost. So the buffer path is REAL and close-to-correct.
  - **🚨 BUT 4.7x SLOWER: `gpu_frame_us` 98ms -> 466ms; `gap_guest` 1-40ms -> ~450ms.** With no hardware FSI,
    ordering still forces ~20 guest passes (n[guest]=20) and a full-buffer barrier/flush on the 90MB EDRAM
    SSBO between each => ~450ms of GPU sync (gpu_busy=99%, real). The software-ROP/sync cost DWARFS the ~78ms
    tile-I/O it removed. This MEASURES + CONFIRMS the memory's old unproven guess "full buffer path likely
    LOSES to software ROP" and is why xenia ships host-RT ("FSI path much slower").
  - **⭐ KEY REDIRECT: atomicMin (task #31) fixes CORRECTNESS (the white box) but NOT the 450ms — it would
    yield a correct-but-4.7x-slower frame. The REAL problem to solve first is the per-pass SSBO barrier/sync
    cost (~450ms / ~20 passes ≈ 22ms per pass-barrier on 90MB).** So do NOT build atomicMin next. Next lever =
    kill the inter-pass barrier cost: (a) SCOPE each barrier to only the EDRAM byte-range the pass touches
    (not the whole 90MB), (b) MERGE passes so there are far fewer barriers, (c) investigate whether the racy
    path even NEEDS a barrier between non-overlapping passes. The measurement saved the multi-session atomic
    build from producing a correct-but-useless result. Instrumentation + `scratch/thor-debug/
    bd_edram_atomic_measure.ps1` reusable. Screenshot: scratch/thor-debug/bd_edram_atomic.png.
  - **⭐ ROOT CAUSE of the 450ms LOCATED (device-free): `CommitEdramBufferShaderWrites` (vulkan_render_target_
    cache.cc:3015) places a barrier on the ENTIRE 90MB EDRAM buffer `PushBufferMemoryBarrier(edram_buffer_,
    0, VK_WHOLE_SIZE, ...)` as a SHADER_WRITE->SHADER_READ, ~20x/frame (once per guest pass commit). Each is
    a full-buffer cache flush + full pass-to-pass serialization (pass N+1 waits for ALL of pass N) => ~22ms x
    20 ≈ 450ms.** THE NEXT LEVER (the actual BD-30 path now): cut this barrier cost, three angles, cheapest
    first — (a) SCOPE the barrier to only the EDRAM byte-range each pass actually touches (its RT tile range),
    not 0..WHOLE_SIZE, so the driver flushes/serializes less; (b) ELIDE the barrier entirely between passes
    that don't ALIAS in EDRAM (independent RTs need no ordering) — likely the big win since most of BD's 20
    passes write disjoint EDRAM regions; (c) MERGE passes (the original one-pass vision). Build (b)+(a) as a
    cvar, re-run bd_edram_atomic_measure.ps1, watch gap_guest fall from 450ms. If gap_guest drops below the
    ~78ms it replaced, the buffer path becomes a NET WIN and atomicMin (task #31) then makes it correct.
- **🚨🚨🚨 GAP-ATTRIBUTION CORRECTED — THE REAL FRAME MODEL (2026-07-01, `GPU pass split` top-gap log, heavy
  field, inpass=1 run, stable x20 frames).** The per-kind gap attribution (gap_xfer≈78ms "transfer tile-I/O")
  was a MISLEAD. The pass-split decomposition shows the ~82ms gap is **TWO GIANT GAPS: top_gap≈[38.5ms, 35ms,
  0.9ms] — ~73ms in just two holes flanking the MAIN GEOMETRY PASS** (fb=670b, 233-275 draws; the giant gaps
  merely FOLLOW tiny 1-2-draw transfer passes adjacent to it, so the kind-attribution credited them to xfer;
  also explains the earlier unstable guest<->xfer attribution whose SUM stayed ~79ms). All 79 pass BRACKETS
  sum to only ~13.5ms; nothing is recorded inside the holes (disp=0 bufcp=0 barr=0) = pure deferred GPU
  execution. **So BD's ~94ms frame ≈ 73ms main-pass deferred work (bin ~38.5 + render/store ~35) + ~13.5ms
  all pass brackets + ~9ms small gaps. The EDRAM pass STRUCTURE (transfers/composites/count) is only ~20ms
  TOTAL — the 42-pass tile-I/O model of the frame is WRONG.** Also: inpass=1 folds guest+composite passes
  15-17+19-21 -> 7-9+9-10 but n[xfer]=35 and the giant gaps unchanged; frame 98.5->94-95ms (~4%, noise-level)
  => inpass stays MODEST-confirmed on the right metric.
  - **✅✅✅ DISCRIMINATOR ANSWERED (2026-07-01, dont_care + pass-split, heavy field, stable x5): THE 73ms IS
    PER-PASS TILE LOAD/STORE TRAFFIC.** With dont_care: **top_gap [38.5,35] -> [7.1,7.0]ms (−59ms); gap_total
    82.5 -> 22.2ms; gpu_frame_us ≈ 32.9ms = 30fps-CLASS on an even HEAVIER frame (366-draw main pass).** The
    surviving 7+7ms = the main pass's REAL bin+render. Bandwidth math checks: ~42 passes x (~16MB load +
    ~16MB store of 1280x768x2xMSAA color+depth) ≈ 1.3GB/frame GMEM<->DRAM. **=> BD-30 IS DIRECTLY REACHABLE
    by eliminating the DEAD load/stores correctly (dont_care kills live traffic too => garbage; the build =
    correct per-pass/per-attachment elision).**
  - **🚨🚨 INPASS=2 DECISIVE (2026-07-01): PASS-COUNT IS NOT THE LEVER EITHER.** inpass=2 (depth folding,
    Turnip has EXT_shader_stencil_export) ENGAGES massively: inpass[x=34 skip_fmt=0 skip_oth=1], n[xfer]
    35->24-25, TOTAL brackets 95->70 (25% pass-count cut), stable, UNHANDLED=0. **Frame UNCHANGED (~100ms;
    drains [36.5,35.5]ms).** So the drains are INVARIANT to: pass count (95->70), barrier scope (90MB->4KB),
    gmem-force, ops-elision (classes empty). ONLY dont_care (removing load/store VOLUME) collapses them
    (33ms). The folded transfer passes were SMALL-attachment passes (320/160/400x768 per the coverage diag,
    ~2-4MB round-trips = noise); the ~72ms volume lives in the BIG main-RT-sized attachments' load/stores.
    NOTE: my bandwidth arithmetic persistently UNDER-predicts the measured cost 3-5x => the per-pass cost
    model (2xMSAA sample planes, D32S8 two planes, UBWC meta, GMEM tile-walk latency, unclamped transfer
    framebuffers?) is wrong somewhere big. NEXT INSTRUMENT (authoritative, next session): PERFETTO Adreno
    GPU trace (per-surface load/store byte counters; gpu_perfetto_config.txt + prior pftrace exist in repo
    root) to attribute the 72ms per-attachment/per-pass. inpass=2 itself = SAFE + free 25% pass cut; keep.
  - **🔍🔍 INCREMENT B(a) VERDICT (2026-07-01, per-pass coverage end-diagnostic): the full-coverage
    color-load-elision class has ~ZERO population on BD — CLOSED by measurement.** The end summaries show
    the captured passes are the bloom-pyramid levels (ext 320/160/400/720 x768) + the main 1280x768 pass;
    their in-pass replace writes are PARTIAL SLIVERS ([640,672) of a 720-wide pass, [0,320) of 400-wide) -
    BD NEVER full-clears an attachment within a pass; the loads are LIVE (levels accumulate onto prior
    contents). retro_cl=0 with tolerance 48 is the true class size, not a bug. Both A (depth, ~2 passes)
    and B(a) (color, ~0) are mechanically proven + correct but BD's load/store OPS are almost all live.
    **=> THE 73ms is live ops made expensive by STRUCTURE: ~70 render passes x full-attachment GMEM<->DRAM
    round-trips (~2.1GB/frame). The eliminable slack is the PASSES (35 EDRAM-aliasing transfer passes +
    composite ping-pong), not their load/store flags - the user's original FUSION thesis, now proven by
    elimination of every alternative (buffer path 4.7x loss; composite-compute 2%; depth elision 2 passes;
    color elision 0; barrier scope inert; gmem force null). NEXT THREAD: the measured inconsistency that
    inpass folding engaged (x=22) yet n[xfer] stayed 35 - if folding genuinely removed transfer passes the
    count should drop; resolving that either unlocks the fold (pass-count cut ~35->13) or explains why
    folding can't work, pointing at RT-image aliasing/sharing (one memory per EDRAM range, images as
    views) as the structural fix.**
  - **🔍 INCREMENT A VERDICT (2026-07-01, A2 diagnostic run): the depth-elision CLASS is ~2 passes on BD's
    heavy field — root-caused, not a bug.** The why-not-eligible diagnostic shows the blockers are REAL
    depth geometry: `z_en=1 zfunc=3(LESSEQUAL) z_wr=1` draws with idx=52-493 inside nearly every guest/
    composite-opened pass (pass KIND is classified by the opening draw; the passes then contain genuine
    3D draws), plus `zfunc=7(ALWAYS)+z_wr=1+stencil=1` depth-writing clears. So BD does NOT have ~35
    depth-free passes; retro depth-none is mechanically PROVEN (patches apply, renders pixel-correct,
    UNHANDLED=0 across two runs) but its BD yield is bounded ~2 passes. KEEP it (free, correct, will help
    other titles); the 59ms budget lives in COLOR/TRANSFER traffic => increment B: (a) retro COLOR-load
    elision - track the UNION of full-coverage replace draws across the pass (BD clears in strips; the
    per-draw dc_safe estimator exists, retro can OR coverage rects), patch load_dont_care_mask variant at
    pass end; (b) transfer-pass STORE/LOAD liveness via the steady-state frame graph (BD's pass sequence is
    IDENTICAL every frame - n constant; last frame's observed consumption = this frame's); (c) dead-transfer
    elision (transfers into dests whose contents are fully overwritten before any read). All ride the proven
    recorded-begin patch mechanism.
  - **PROGRESS LOG (2026-07-01, increment A built + device-tested twice):** (1) `gpu_vulkan_retro_depth_none`
    SHIPPED (default-off): hindsight depth elision at pass end via recorded-begin patch — mechanically
    CORRECT on device (retro_dn=2 patched, renders pixel-correct, UNHANDLED=0) but ENGAGEMENT-LIMITED: only
    the same 2 passes the begin-time version caught => BD's composites set depth state; A2 (zfunc==ALWAYS
    refinement + why-not-eligible diagnostic) built, validation pending. (2) `mesa.tu.debug=gmem` (force
    GMEM) = NULL (gaps/frame unchanged) => autotuner-sysmem hypothesis REFUTED, passes already GMEM. (3)
    RECONCILED MODEL: the two giant gaps are DRAIN points where the ~42 small passes' ACCUMULATED deferred
    tile load/store executes (small-pass timestamps retire early; the main pass's VSC serialization forces
    the drain) — consistent with dont_care collapsing them, gmem-force not, and the aggregate bandwidth math
    (~2-2.6GB/frame at ~30GB/s ≈ 70-87ms). The elision target is therefore ALL passes' load/store aggregate,
    attributed by the drain, not per-pass timestamps.
  - **⭐⭐ THE BUILD (the goal's "major rearch", now with a measured 59ms payoff): SUBMIT-TIME FRAME-GRAPH
    LOAD/STORE RECOMPILER.** xenia records the WHOLE frame deferred before vkQueueSubmit, and patching a
    recorded BeginRenderPass is PROVEN in-tree (feedback-merge: feedback_producer_begin_pos_ +
    IsCommandPositionInRange + patch). So at submit time we have PERFECT HINDSIGHT: walk the recorded passes,
    compute per-attachment liveness (is this pass's write READ later — by a pass that blends/ztests, a
    texture bind of the RT image, a transfer source, a resolve — before being fully overwritten?), and PATCH
    each recorded pass to a render-pass VARIANT with per-attachment loadOp=DONT_CARE (nothing read) /
    storeOp=NONE (nothing consumed). Variant machinery EXISTS (load_dont_care_mask variants +
    depth_store_op_none + framebuffer keying). Increments: (A) record pass table (begin position,
    attachments, EDRAM ranges) + liveness walk + patch loadOp for provably-unread loads; (B) color
    STORE_OP_NONE variant + store elision; (C) partial-range/strip unions. Conservative = correct: any
    uncertainty leaves LOAD/STORE. Verify each increment with the pass-split log (top_gap shrink) +
    pixel-correct screenshot.
  - **🚫 DIAGNOSTIC RESULT (2026-07-01, gpu_edram_atomic_barrier_bytes=4096): scoping the barrier 90MB->4KB
    changed NOTHING — gap_guest stayed ~447ms, gpu_frame_us ~468ms (6 frames). The cost is NOT the cache flush;
    it is pipeline SERIALIZATION / deferred software-ROP fragment execution (per-fragment atomic-less depth+
    color RMW over BD's overdraw runs in the TBDR deferred phase = the "gap"). Barrier optimization CANNOT fix
    it. => THE FULL BUFFER PATH IS DEAD FOR BD's HEAVY FIELD: removes ~78ms transfer tile-I/O, adds ~450ms
    software-ROP/serialization = 4.7x net LOSS. MEASURED + CLOSED (vindicates xenia shipping host-RT; confirms
    "software ROP loses on overdraw"). Do NOT build atomicMin (task #31) / global buffer path for BD.**
  - **⭐ WHERE BD-30 ACTUALLY IS (host-RT is the 4.7x-faster base; its ~80ms tile-I/O is the target):** ~80ms =
    gap_xfer (~40-78ms EDRAM ownership transfers = RT<->EDRAM copies when guest RTs alias tiles) + gap_guest
    (~1-40ms geometry RT deferred stores). Lever = REDUCE those ON host-RT, NOT switch storage model: (1) cut
    OWNERSHIP TRANSFERS (big chunk) — keep more RTs simultaneously resident so tile-alias ownership changes
    don't force copies, and/or reorder passes to minimize ownership flips (task #28); (2) reduce PASS COUNT.
    Buffer path AND composite-compute are both now closed by measurement; the live EDRAM lever = host-RT
    ownership-transfer / pass-count reduction.
- **DECISIVE HEAVY-FIELD MEASUREMENT (2026-07-01, user-authorized 75C heat-danger run reached the
  ~98.5ms field) — REDIRECTS THE WHOLE STRATEGY. Brick 2 (composites) is a ~2% lever; the REAL ~80% is the
  EDRAM ownership-TRANSFER + geometry-RT deferred tile-I/O.** Heavy field confirmed (gpu_frame_us≈98500,
  n[composite=20 xfer=35 guest=15-17], stable x6). Per-kind GPU time (us):
  `guest(shade)=6-10k composite(shade)=0.5-4k xfer(shade)=2.3-2.6k rcopy=1-4.7k | gap_composite=1.5-2.9k
  gap_guest=1-40k gap_xfer=37-78k gap_total≈83k`. So of the 98.5ms frame:
  - **gap_composite ≈ 2ms ≈ 2% => BRICK 2 (composite->compute) IS NOT THE BD-30 LEVER.** The many turns
    scoping the composite fragment->compute translator targeted a ~2% win. The measurement kills it as the
    top lever (the ALU-capture + coupling work stays valid, just not the priority).
  - **gap_guest + gap_xfer ≈ 79ms ≈ 80% of the frame = the deferred TBDR tile stores of the GEOMETRY RTs +
    the 35 EDRAM OWNERSHIP-TRANSFER passes (RT<->EDRAM store/load quads).** (Attribution shifts between
    gap_guest and gap_xfer frame-to-frame — pass-ordering noise — but their SUM ≈79ms is rock-stable and
    gap_composite is consistently ~2ms.) THIS is the ~74ms tile-I/O the memory always cited, now
    kind-attributed: it's the per-RT stores + ownership transfers, NOT the composites.
  - **THE LEVER = eliminate the per-RT tile stores + ownership transfers = the SINGLE-EDRAM-SSBO buffer path
    (atomic-ROP, task #31).** With all rendering in ONE EDRAM SSBO there are NO separate RT VkImages => NO
    per-RT deferred tile stores and NO RT<->EDRAM ownership-transfer passes => gap_guest + gap_xfer (~80%)
    vanish BY CONSTRUCTION. The cost moves to software ROP (atomic depth/blend) in the fragment shaders. The
    memory earlier guessed "full buffer path likely LOSES to software ROP on BD's overdraw" — but that was
    never measured, and this shows the tile-I/O it removes is ~80ms, a HUGE budget to trade against per-
    fragment ROP ALU. **This VINDICATES the user's "solve the EDRAM issue via the buffer/FUSION path, don't
    rescope" mandate and REDIRECTS effort from composite-compute back to the buffer path.** NEXT: re-measure
    a full-scene buffer-path A/B (does software ROP cost < the ~80ms tile-I/O it removes?), and drive the
    atomic-ROP SPIR-V (task #31) which is the mechanism. Instrumentation (kGuestComposite + gap-by-kind) is
    committed + reusable for verifying the buffer path removes gap_guest+gap_xfer.
- **BRICK 2 CEILING — first (light-scene) measurement, SUPERSEDED by the heavy-field run above (2026-07-01,
  committed). TEMPERS the hypothesis; HEAVY-FIELD CONFIRMATION STILL PENDING.** Built a non-corrupting GPU-
  timestamp split: composite-consumer guest passes -> kGuestComposite, and each deferred-tile-I/O GAP
  attributed to its preceding pass kind (the tile store is deferred into the gap, NOT the bracket, which is
  only shading). `gap_composite` = the composites' deferred tile-I/O = exactly what brick 2 (composite->
  compute) removes. **Measured (device, but the device is HEAT-SOAKED from ~8 runs and watchdogs at 60-120s
  BEFORE the ~100ms heavy field, so this is the ~22ms LIGHT scene — a lower bound, NOT the heavy-field
  verdict):** per-frame, stable x6: `gpu_frame_us≈22300  composite(shade)≈2.5ms  gap_composite≈2.0ms
  gap_guest≈2.1ms  gap_xfer≈4.4ms  gap_total≈13.4ms  rcopy≈3.3ms  n[composite=19-20 xfer=26 guest=11]`. So
  in THIS scene: (1) brick 2's ceiling `gap_composite ≈ 2.0ms / 22.3ms ≈ 9%` — MODEST; (2) the EDRAM
  ownership-TRANSFER passes' tile-I/O `gap_xfer ≈ 4.4ms` is 2x BIGGER than the composites'. **Implication:
  the NARROW brick-2 (composites only) is NOT sufficient alone; the bigger lever is the FULL "everything in
  one EDRAM SSBO" model that removes composite + transfer + producer tile-I/O together (composite-compute +
  the atomic-ROP/buffer producer path, unified) — which reinforces that brick 2 and the buffer path are two
  halves of one model, not independent.** CAVEAT (rigorous): this is the LIGHT scene; the heavy field may
  shift the ratios (more/larger composites). NEXT: a genuinely cooled device (long idle) OR a save-state to
  reach the field fast, then re-read gap_composite/gap_xfer on the ~100ms field before committing the big
  translator build. The instrumentation is committed + ready.
- **⚠️ ARCHITECTURAL COUPLING the ALU exposes (reshapes brick 2 — important):** the composites sample the
  producer as a BOUND TEXTURE (`tfetch2D tf0`). On the host-RT path the tile-I/O break is the producer
  RT->SHADER_READ transition (end the producer's render pass = GMEM store). Moving the CONSUMER to compute
  does NOT remove that transition BY ITSELF — a compute shader sampling a just-rendered RT-as-texture still
  forces the same store. The break only vanishes if the compute reads the producer from a buffer already in
  DRAM, i.e. **the PRODUCER pass must have written the EDRAM SSBO** (the buffer-path producer model). So
  composite-compute and the atomic-ROP buffer path are TWO HALVES of one "everything renders to one EDRAM
  SSBO" model, not independent levers. Brick 2's honest scope = (a) producer writes to the EDRAM SSBO
  (atomic-ROP / buffer path, task #31), THEN (b) consumer composite reads it from the SSBO in compute. The
  cheaper INTERMEDIATE that IS independently shippable: keep producers as GMEM-RESIDENT storage images and
  have the compute consumer read them as storage images (no render-pass-to-texture transition) — worth a
  feasibility check next, on a cool device, starting from the 1E70 copy.
- **⭐ COUPLING RESOLVED — the win + metric CORRECTED (2026-07-01, reasoned from ALU + TBDR hardware fact).**
  Worked the tile-I/O accounting through precisely. Hardware fact: on a TBDR (Adreno/Turnip) a COMPUTE
  dispatch does NOT use the tiling/GMEM/binning path — it reads/writes DRAM images & buffers directly (this
  is why xenia's EDRAM resolves are already compute dispatches). Therefore for a composite consumer C (reads
  producer P as texture tf0, writes dest D):
  - As a FRAGMENT draw in its own render pass: (1) P must be stored to DRAM to be sampled = the brk_img_sr
    (P ATTACHMENT->SHADER_READ); (2) C's render pass LOADS D's tiles, renders, STORES D's tiles.
  - As a COMPUTE dispatch: (1) P still stored (unchanged); (2) compute samples P and writes D straight to a
    DRAM storage image — **NO render-pass tile load/store for D at all.**
  - So converting C to compute REMOVES D's tile load+store (~a tile-resolve's worth), and for a CHAIN
    (blur pyramid P->C1->C2->C3: the 13-tap 1B13 + friends) it removes ALL the intermediate stores+loads —
    each Di is compute-written to DRAM and compute-read by the next, zero render-pass tile-I/O across the chain.
    The initial P store stays (that's the residual brk_img_sr; removing it needs the producer-SSBO buffer path).
  - **TWO corrections to my own earlier plan: (a) the SUCCESS METRIC is `gpu_frame_us` (single-run A/B), NOT
    `brk_img_sr` — the break count barely moves (P stores remain) while the TIME drops as D/chain tile-I/O
    vanishes; measuring brk_img_sr would have made a real win look like a failure. (b) The producer-writes-SSBO
    (full buffer path) is NOT required for this first win — compute can sample P as a normal texture and write
    D as a storage image. So brick 2's first win is SIMPLER and independently shippable; the buffer path is a
    LATER increment that removes the remaining P stores.** Biggest bang = the multi-pass blur CHAINS, not the
    single 1E70 copy. Next-session build: compute-target mode in SpirvShaderTranslator (gl_FragCoord->
    GlobalInvocationID, oC0 export->storage-image store, tfetch stays a normal sampledImage read), intercept
    the composite consumer draws, dispatch, validate pixel-correct + measure gpu_frame_us single-run A/B.
