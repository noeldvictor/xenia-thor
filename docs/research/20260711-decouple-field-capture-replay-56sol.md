Q1 (feasibility of deferred-command splice as the snapshot): The captured field draws are deferred Vulkan COMMANDS that reference resources by handle (descriptor sets, vertex/index buffers, pipelines, push-constant values baked in). If I capture them into a side buffer and replay them later in the SAME frame/submission, are those handles still valid at replay? Specifically: (a) descriptor sets - xenia uses per-submission descriptor arenas that may be recycled; do the field draws' descriptor sets survive to a frame-end replay, or must I pin/version them? (b) transient vertex/index buffers (upload ring) - same lifetime question. (c) the pipelines must be the 2-subpass CR pipelines (subpass 0). If the deferred commands bake CmdVkBindPipeline with the CR pipeline handle, replay is fine. Is same-frame deferred-command replay of arbitrary draws safe in xenia, or does it require resource-lifetime pinning like a real HLE framegraph?

Q2 (the capture hook): where/how do I DIVERT the field draws from the main stream into the side buffer? Options: (a) at IssueDraw, when the draw targets the field producer, redirect deferred_command_buffer_ recording to bd_field_command_buffer_ for that draw (temporarily swap the recording target); (b) capture at a higher level. The prepass does (a)-like redirection. But the field draws also do state setup (bind pipeline/descriptors/dynamic state) that's emit-on-change - if I divert only SOME draws, the emit-on-change state tracking desyncs (the diverted draws inherit wrong leading state, and the main stream's tracking thinks state was emitted). How does the prepass handle the emit-on-change leading-state problem, and how do I replicate it so the captured field draws are self-contained (re-emit ALL their binds at the start of the captured block)?

Q3 (interleaved dependencies): the field draws may DEPEND on interleaved shadow-RT results (e.g. a field draw samples a shadow map rendered in an intervening RT-B pass). If I defer ALL field draws to frame-end, a field draw that samples shadow-B is fine (shadow-B was rendered earlier and is available). But a field draw whose RESULT is sampled by a later shadow/other pass would break (the field isn't produced until frame-end). Does BD's field get sampled mid-frame by other RTs, or only at the final composite? If mid-frame, deferring the whole field to frame-end is unsafe. How do I determine/handle this - is per-consumer lazy production (produce the field the first time it's sampled) the right rule, and how do I hook "first sample"?

Q4 (depth): the field depth-tests against a depth buffer. In the interleaved model the depth is shared/managed by BD. In the decoupled contiguous replay, the field's depth must be present in the dedicated pass. Do I capture the field's depth-prepass draws too (into the same dedicated pass, before the color draws), or reference BD's existing depth image? If BD's depth is built interleaved too, I may need to capture the depth draws contiguously as well.

Q5 (minimal viable first version): what's the SIMPLEST correct first version to validate the decoupling concept + measure fps, before the full framegraph? E.g., can I start by capturing ONLY the field's color draws (assuming depth is available), replay them contiguously, custom-resolve, and see if (a) it renders correct and (b) fps improves - accepting known limitations (no shadow-dependency handling yet)? Or is there a fundamental reason a partial version won't render at all?

Q6 (is this even the right decoupling, or is there a simpler one): given xenia's deferred command buffer already records self-contained-ish command streams, is capture-and-replay the right approach, OR is there a simpler decoupling - e.g., a "super render pass" holding the field RT resident across the interleaved shadow passes (5.6-sol mentioned this needs: no intervening compute/copy/non-local dependency, coherent extents/layouts, GMEM fit)? Which is more tractable in xenia: capture-replay (framegraph) or hold-resident (super-pass)? Give a decisive recommendation on the architecture + the concrete xenia hooks + the minimal first milestone.

Cite xenia mechanisms (DeferredCommandBuffer, prepass_command_buffer_/InsertStreamFrom, the direct-native producer) and Vulkan/Turnip specifics. Decisive recommendation + minimal-first-milestone.

[35m[3mcodex[0m[0m
I’m going to ground this in the fork’s actual command-buffer, prepass, direct-native, and custom-resolve code before recommending an insertion point. I’m using the repo’s Xbox 360 D3D HLE/recomp and Vulkan/Adreno debugging skills because this decision crosses guest draw ordering, Xenia’s deferred recorder, and Turnip render-pass behavior; I’ll also inspect the latest relevant research note and Vulkan spec/Turnip primary sources.
  [36m→[0m Read the applicable repo instructions and identify the active direct-native/custom-resolve implementation
  [2m•[0m [2mTrace DeferredCommandBuffer, prepass capture/splice, state emission, descriptor/upload lifetimes, and sampling hooks[0m
  [2m•[0m [2mCheck Vulkan/Turnip constraints and Blue Dragon dependency evidence[0m
  [2m•[0m [2mSynthesize a decisive architecture and minimal proof milestone[0m
[35m[3mexec[0m[0m

FullName                                                                                                               
--------                                                                                                               
C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\.agents\skills\xbox360-d3d-hle-recomp\SKILL.md              
C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\.agents\skills\xenia-vulkan-adreno-renderdoc\SKILL.md       
C:\Users\leanerdesigner\Documents\New project 8\xenia-thor\.agents\skills\xenia-windows-powershell-command-hygiene\S...



[35m[3mexec[0m[0m
---
name: xbox360-d3d-hle-recomp
description: >
  Knowledge base + workflow for the LONG-TERM DIRECTION (user 2026-07-02, "hybrid"): D3D9-Xbox -> Vulkan
  HLE + AOT static recompilation of the PPC, to escape xenia's PM4-level LLE that faithfully reproduces
  Xbox 360 GPU inefficiencies (BD's predicated tiling that doubles foliage binning; EDRAM round-trips).
  Use when: designing/building an HLE renderer path inside xenia, evaluating XenonRecomp/UnleashedRecomp,
  RE-ing a game's D3D tiling/surface setup, or reasoning about the AOT-recompile + translation-layer arch.
  Reference sources cloned at ../reference/ (XenonRecomp, XenosRecomp). This is the accelerator for both the
  tactical RE (one-tile patch) and the strategic HLE build.
---

# Xbox 360 D3D-HLE + AOT-recompile knowledge base

## THE STRATEGIC MODEL (proven by UnleashedRecomp)
Stop emulating the Xbox 360 GPU at the PM4 command level (xenia LLE) ƒ?" that faithfully reproduces the
game's GPU inefficiencies. Instead **translate the D3D API to Vulkan directly (HLE)** + **AOT-recompile
the PPC to native**. hedge-dev's stack is the reference implementation:

- **XenonRecomp** (../reference/XenonRecomp) ƒ?" Xbox360 PPC XEX -> C++.
  - CPU state struct (`PPCContext`, XenonUtils/ppc_context.h) passed to EVERY recompiled fn; 2nd arg = base
    pointer (guest 32-bit ptrs). Instructions operate little-endian; loads/stores swap endian (marked
    volatile to stop unsafe Clang reordering). Vector regs: the WHOLE 16-byte vector is reversed (use WZY
    not XYZ in dot products, reverse args for pack). FPU keeps denormals; VMX flushes them (FP-state flag
    in the context toggled per instruction).
  - **VMX via x86 intrinsics; ARM64 via SIMDe (simd-everywhere)** => THE ARM64 PORT IS NOT FROM SCRATCH.
  - Indirect/virtual calls: perfect-hash table (fn addr*2 -> recompiled fn ptr), placed after the XEX
    region in base memory; macros in generated `ppc_config.h`.
  - Jump tables: detected by `mtctr r0` + `bctr` patterns; XenonAnalyse emits a TOML; per-game tuning.
  - Function boundaries: `.pdata` segment + heuristics; manual overrides in TOML.
  - **NO runtime provided** (you write kernel HLE + renderer + input + audio). NO exceptions. NO MMIO/XMA
    (unimplemented ƒ?" but xenia HAS an XMA decoder + full kernel HLE to port/reference).
- **XenosRecomp** (../reference/XenosRecomp) ƒ?" Xenos shader bytecode -> HLSL -> DXIL/SPIR-V (via DXC).
  - Vertex fetch: Xbox vertex DECLARATIONS -> native D3D12/Vulkan INPUT LAYOUTS (no runtime shader
    permutations); endian-swaps vertex data; 16-bit swizzle fix + R11G11B10 unpack via spec constants.
  - Constant buffers: 3 slots (VS 256 float4, PS 224 float4, shared) -> root cbuffers (D3D12) / push
    constants (Vulkan), populated from shader reflection.
  - **BYPASSES tiling / EDRAM / resolve ENTIRELY** ƒ?" the translation layer never reproduces them. THIS is
    exactly why it avoids BD's foliage-binning-doubling: it renders the scene ONCE, optimally.
- **UnleashedRecomp** (Sonic Unleashed) = the full proven port; renderer = "translation layer, not GPU
  emulation". **ReXGlue** = the same static-recompile lineage the /goal names.

## WHY THIS KILLS BD's BOTTLENECK (rigorous, from this campaign)
BD's field is GPU-bound (cpu_real 2.9ms vs 52ms GPU-fence-wait) on **foliage TRIANGLE-BINNING doubled by
predicated tiling** (2 tile passes; host_draws 940 vs rendered 675). Every xenia-LLE lever failed:
fill/MSAA/resolution (not fill-bound), naive flatten (tiles are pre-offset by the D3D RUNTIME at replay =
can't merge emulator-side), merge_draws (inapplicable), deint (inert), decimation (~25fps + artifacts,
thins foliage). HLE renders the scene ONCE (no tiling) => bin-once by construction, full foliage, correct
pixels. AOT is ORTHOGONAL: helps CPU-bound titles (Burnout/Gears) + heat; BD field GPU-bound => AOT is
thermal-only there.

## HYBRID PLAN (user-chosen 2026-07-02)
Keep xenia as the runtime base; build a D3D-HLE renderer path INSIDE it incrementally; share our existing
LLVM whole-fn backend (src/xenia/cpu/backend/llvm) as the AOT foundation. Two parallel tracks:
- TACTICAL (now): the one-tile guest patch (task #39) ƒ?" a SCOPED HLE of just BD's tiling (force tile
  count->1). Near-term correct-pixel 30fps shot within xenia.
- STRATEGIC: study how UnleashedRecomp's renderer maps Xenos draws -> Vulkan and design an xenia HLE path
  (intercept D3D draw/setrendertarget/tiling at the guest-call level instead of PM4). Start with the
  smallest surface (the tiling + main draw path) and grow.

## BD TILING MACHINERY (RE'd this campaign ƒ?" for the one-tile patch)
- Walker `0x82487878`: reads tile count `*(FUN_826bfc68()+4)`, walks cmd-list segments, per draw computes
  a per-tile predication mask vs a tile-rect table at `state+0xC` (16B/tile), writes `mask|0x80000000` to
  draw+8. (This fn CRASHED the resolution patches @0x82487948 ƒ?" delicate; changing dims desyncs the table.)
- Tiling loop `0x82487978`: gets state via `0x826BFC7C`, calls the walker per segment.
- Emitter `0x82473BD8`: writes SET_BIN_MASK/SELECT PM4 (0xc0006000/6200/6100/6300); this one resets to
  all-bins 0xffffffff. Per-tile selects (0x80000003, 0x0C) emitted in the loop.
- PATCH TARGET: where the tiling SETUP writes state+4 = tile count (2) + state+0xC = rects. Force count=1 +
  full-screen rect. NOT YET PINNED ƒ?" trace who writes state+4; state getter FUN_826bfc68 is a large fn.

## KNOWLEDGE SOURCES (the "comprehensive RAG")
- Reference source (cloned): ../reference/XenonRecomp, ../reference/XenosRecomp. (UnleashedRecomp = the
  renderer reference; clone hedge-dev/UnleashedRecomp when building the HLE renderer.)
- Xenos GPU / PM4 command reference: IN xenia's own source ƒ?" src/xenia/gpu/xenos.h (PM4 opcodes),
  packet_disassembler.cc, registers.h, the vulkan/ translator. This IS the authoritative PM4/EDRAM/resolve
  reference.
- Xbox 360 D3D9 API (XDK): the guest D3D is statically linked in each XEX (0x8246xxxx region for BD). No
  public XDK redistribution; reconstruct the API shapes from RE + the Free60/libxenon community docs +
  XenonRecomp's handling. D3DDevice_* ordinals.
- This campaign's RE artifacts: scratch/thor-debug/bd_*.txt (walker/tiling/resflow/modeflag decompiles),
  scratch/ghidra/scripts/Bd*.java.

## WORKFLOW
- RE a game's tiling/surface: Ghidra headless (see xenia-thor-ghidra-game-patch skill) on the XEX; find
  SET_BIN_SELECT emitter via the PM4 header constants (ori 0x6200/6300/6000/6100); decompile the walker +
  its callers; trace the tile-count writer.
- Design an HLE intercept: identify the guest D3D fn (statically linked, by ordinal/behavior), decide
  whether to (a) PATCH it (.patch.toml, cheapest ƒ?" e.g. tile count->1), or (b) HLE-replace it (patch entry
  to trampoline into a host impl ƒ?" the real translation layer).
- Validate: device A/B (single-run alternating for perf; screenshot for pixels); the alternator-gated
  method (gpu_freeze_ab_alternate_vrs phase) is the rigorous perf harness.

## ƒo. PROVEN REƒ+'HLE RECIPE (2026-07-03) ƒ?" reusable across games, this is THE fast path
The "reach in, pin the D3D fn, HLE it" loop, validated on BD's tiling replay. Four committed tools:

**1. PIN the D3D fn that writes a target struct ƒ?" PAGE-WATCH (`cpu_watch_guest_write_page`, a64_backend.cc
ExceptionCallback + processor.cc walker-protect).** The store-watch (`arm64_guest_store_watch`) FAILS on the
field (per-store JIT emit = ~10x slowdown ƒ+' timed nav desyncs). The page-watch has ZERO per-store cost: host-
mprotect the target guest page read-only; on write-fault, **emulate-on-fault** (decode store size[31:30]+Rt
write) + `code_cache->LookupFunction(ex->pc())` ƒ+' the writer guest fn + `ctx->lr` (PPCContext @ host x[20]) ƒ+'
its CALLER. Runs BD full-speed ƒ+' reaches the field. THIS is the tool the multi-session RE wall needed.

**2. Peel helper wrappers.** Writers are often memcpy(0x826bf770)/calloc(0x8246B3B8) helpers; the real D3D fn
is the CALLER ƒ?" read `ctx->lr` in the page-watch hit. calloc/memcpy have NO static refs (deep indirect) so
static Ghidra caller-analysis is DEAD; the runtime page-watch lr is the only way.

**3. GOTCHA ƒ?" xenia's DECLARED entry ƒ%ÿ Ghidra prologue.** BD's tiling replay: Ghidra prologue-scan said
0x82487cc8, but xenia harvested it as **0x82487CC0-82487F50** (grep logcat `shared-fn-harvest`). HLE the
xenia-DECLARED entry (0x82487CC0), else `IsHleIntercept` never matches ƒ+' planted=0. Also: fns reached by
tail-call/indirect (driver 0x82487988) are NEVER LookupFunction'd ƒ+' un-plantable; only harvested entries plant.

**4. HLE HOST BODY ƒ?" reimplement the D3D fn (`cpu_hle_tiling_replay_addr`, processor.cc HleTilingReplayHandler).**
SetupExtern at LookupFunction plants the host handler (kExtern dispatch, no guest-code patch). For a REPLAY/
token-loop fn, reimplement the loop in host C++, reading the recorded stream (r3) + ctx (r4) with load/store_
and_swap (BE), and calling guest helpers (ring writer 0x8246E100, tiling helpers) REENTRANTLY via
`Processor::Execute(ThreadState::Get(), addr, args[], n)` (argsƒ+'r[3+i], returns r[3]; keep your loop state in
C++ LOCALS since Execute clobbers ctx->r). VIABLE: only ~dozens of setup PM4 emits/frame (draws go via the
driver/walker, not the tiling-setup fn). Device: **planted+faults=0 = the host body IS the D3D fn + runs**.
Then apply the bin-once tweak (force tile count=1) + the COHERENT surface rewrite (RB_SURFACE_INFO pitch +
color/depth EDRAM bases + resolve TOGETHER ƒ?" piecemeal downstream override = 0fps black EDRAM desync; that's
why HLE-at-SOURCE beats RT-cache coalescing). VdGlobalDevice ptr = *(0x820005f4).

## dY?-‹,? LOAD-TIME XEX PIPELINE (RPCS3-model endgame ƒ?" the user's "progress bar" vision)
The right architecture = at GAME LOAD, a progress-bar pass that (a) AOT-LLVM-compiles the PPC (we HAVE the
object cache: [[llvm-jit-backend-build]], `cpu_backend_llvm` + AOT .o cache), and (b) SIGNATURE-scans the XEX
for the static-linked XDK D3D9 fns ƒ+' installs HLE trampolines (the mechanism above), caching both keyed by
XEX hash. RPCS3 analog: its boot progress bar AOT-recompiles PPU/SPU modules; we AOT the PPC + (the SPUƒ+'HLE
analog) signature-identify + host-replace the D3D fns. This = build-order step 4 (a signature DB stable per
XDK version 2005-2013, generic across games ƒ?" NOT a per-game port). EDRAM is THE core inefficiency; HLE-ing
BeginTiling/Resolve bypasses the predicated tiling + EDRAM round-trip AT THE SOURCE = render once / resolve
once. To generalize: the page-watch recipe pins BeginTiling/Resolve/Draw per game; their SIGNATURES (byte
patterns of the harvested fn) go in the DB; the loader matches + trampolines them without re-RE.


[35m[3mexec[0m[0m
---
name: xenia-vulkan-adreno-renderdoc
description: Debug xenia-thor GPU bring-up on AYN Thor Adreno 740, including Xenos PM4 packets, command processor ring pointers, GPU writebacks, VdSwap, PM4_XE_SWAP, Vulkan IssueSwap, texture cache, presenter output, Android Vulkan layers, RenderDoc, AGI, and black-screen-after-draws failures.
---

# Xenia Vulkan Adreno RenderDoc

Use this skill when logs show real GPU traffic, PM4 packets, draw calls,
writeback waits, or black output after Vulkan setup.

## First Rule

RenderDoc is a GPU frame tool, not proof that guest CPU execution is correct.
Only use it after normal logcat proves a meaningful guest GPU frame path.
For this fork, AYN Thor Adreno proof matters more than Windows renderer proof,
but CPU/A64 speed evidence still outranks GPU deep dives when the profile says
the guest CPU is the wall.

## Mature Vulkan Port Bias

Use `docs/research/20260524-165737-mature-vulkan-port-speed-design.md` as the
GPU design ladder, and `docs/research/20260524-171203-vulkan-counter-surface-audit.md`
as the repo-local counter surface map. Other mature emulator Vulkan ports
converge on the same shape:

- device and driver profile before optional Vulkan behavior;
- console GPU milestone classification before frame tools;
- deterministic trace/replay before broad renderer edits;
- shader/pipeline cache timing before async compilation or prewarming;
- EDRAM, resolve, texture-cache, and writeback correctness before speed claims;
- explicit queue/fence/present wait attribution before "push more GPU" work;
- validation, RenderDoc, AGI, and quiet FPS captures kept as separate lanes.

For Blue Dragon, do not start broad GPU work while quiet captures still show the
main CPU/JIT thread as the wall. A GPU slice needs fresh evidence such as high
GPU wait, resolve/copy body cost, shader/pipeline stalls, present pacing stalls,
or a real black-frame milestone regression.

Before a GPU speed patch, run the no-behavior audit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_vulkan_counter_surface_audit.ps1
```

The current default-off counter skeleton is documented in
`docs/research/20260524-173639-vulkan-perf-counter-skeleton.md`. Enable it
through `tools\thor\thor_xenia_debug.ps1` with
`-VulkanTracePerfCounters true -VulkanTracePerfCountersLogInterval <N>`. It
already logs IssueSwap, graphics pipeline create/cache-hit, queue submit,
present acquire/submit/present, barriers, and render-pass starts. Prefer a
route-stabilized control/counter sandwich over a renderer rewrite until Thor
route metadata proves whether the GPU wall is pipeline creation, queue/present,
barriers, readbacks/uploads/copies, descriptor churn, resolves, or completion
waits.
