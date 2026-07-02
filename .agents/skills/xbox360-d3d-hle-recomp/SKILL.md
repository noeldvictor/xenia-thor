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
Stop emulating the Xbox 360 GPU at the PM4 command level (xenia LLE) — that faithfully reproduces the
game's GPU inefficiencies. Instead **translate the D3D API to Vulkan directly (HLE)** + **AOT-recompile
the PPC to native**. hedge-dev's stack is the reference implementation:

- **XenonRecomp** (../reference/XenonRecomp) — Xbox360 PPC XEX -> C++.
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
    (unimplemented — but xenia HAS an XMA decoder + full kernel HLE to port/reference).
- **XenosRecomp** (../reference/XenosRecomp) — Xenos shader bytecode -> HLSL -> DXIL/SPIR-V (via DXC).
  - Vertex fetch: Xbox vertex DECLARATIONS -> native D3D12/Vulkan INPUT LAYOUTS (no runtime shader
    permutations); endian-swaps vertex data; 16-bit swizzle fix + R11G11B10 unpack via spec constants.
  - Constant buffers: 3 slots (VS 256 float4, PS 224 float4, shared) -> root cbuffers (D3D12) / push
    constants (Vulkan), populated from shader reflection.
  - **BYPASSES tiling / EDRAM / resolve ENTIRELY** — the translation layer never reproduces them. THIS is
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
- TACTICAL (now): the one-tile guest patch (task #39) — a SCOPED HLE of just BD's tiling (force tile
  count->1). Near-term correct-pixel 30fps shot within xenia.
- STRATEGIC: study how UnleashedRecomp's renderer maps Xenos draws -> Vulkan and design an xenia HLE path
  (intercept D3D draw/setrendertarget/tiling at the guest-call level instead of PM4). Start with the
  smallest surface (the tiling + main draw path) and grow.

## BD TILING MACHINERY (RE'd this campaign — for the one-tile patch)
- Walker `0x82487878`: reads tile count `*(FUN_826bfc68()+4)`, walks cmd-list segments, per draw computes
  a per-tile predication mask vs a tile-rect table at `state+0xC` (16B/tile), writes `mask|0x80000000` to
  draw+8. (This fn CRASHED the resolution patches @0x82487948 — delicate; changing dims desyncs the table.)
- Tiling loop `0x82487978`: gets state via `0x826BFC7C`, calls the walker per segment.
- Emitter `0x82473BD8`: writes SET_BIN_MASK/SELECT PM4 (0xc0006000/6200/6100/6300); this one resets to
  all-bins 0xffffffff. Per-tile selects (0x80000003, 0x0C) emitted in the loop.
- PATCH TARGET: where the tiling SETUP writes state+4 = tile count (2) + state+0xC = rects. Force count=1 +
  full-screen rect. NOT YET PINNED — trace who writes state+4; state getter FUN_826bfc68 is a large fn.

## KNOWLEDGE SOURCES (the "comprehensive RAG")
- Reference source (cloned): ../reference/XenonRecomp, ../reference/XenosRecomp. (UnleashedRecomp = the
  renderer reference; clone hedge-dev/UnleashedRecomp when building the HLE renderer.)
- Xenos GPU / PM4 command reference: IN xenia's own source — src/xenia/gpu/xenos.h (PM4 opcodes),
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
  whether to (a) PATCH it (.patch.toml, cheapest — e.g. tile count->1), or (b) HLE-replace it (patch entry
  to trampoline into a host impl — the real translation layer).
- Validate: device A/B (single-run alternating for perf; screenshot for pixels); the alternator-gated
  method (gpu_freeze_ab_alternate_vrs phase) is the rigorous perf harness.
