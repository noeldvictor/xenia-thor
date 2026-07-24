# The recomp scene (ReXGlue / XenonRecomp / UnleashedRecomp) + an honest 10x budget

Research pass for the "get BD 10x faster, lower power" goal. Sources are the
shipping recomp projects (which solve OUR problem for other titles) plus the DBT
literature. The headline: **the literature says the CPU micro-levers cannot get
10x; the recomp scene says the native renderer can.** Numbers below are from the
sources, not from our device.

## 1. The 10x budget — where a 10x can and cannot come from

Measured BD field baseline (ours, 85 LLE frames, GPU-bound): `gpu_frame_us` =
123ms = 3.5ms in-pass work + 119ms between-pass GMEM tile-store across ~95 EDRAM
passes. 30fps needs ~33ms.

- **DBT/JIT micro-optimization ceiling (literature):** register-mapping cost
  models, peephole passes and function inlining on ARM64 QEMU-class translators
  report **1.07x–1.32x** (avg 1.15x, ~11% code-size cut). Box64's CALLRET —
  the single biggest structural dynarec lever — is **>10%, up to 10-20%** in the
  best cases. So the entire CPU-side lever stack is a ~1.2-2x class of win, and
  **cannot produce 10x by itself.** This is the honest calibration for the goal.
- **Architecture-level (the recomp scene):** deleting the emulated GPU model
  entirely. Our own split says 119 of 123ms is EDRAM-emulation tile-store, i.e.
  ~**6x of GPU headroom** sits in the pass structure, not in shading. That is
  where a 10x-class number lives, and it is exactly what UnleashedRecomp did.
- **Conclusion:** 10x = (native renderer, ~5-6x GPU) x (AOT-primary CPU +
  CALLRET-class levers, ~1.2-2x). The CPU work is necessary (it is also the
  lower-power axis) but the multiplier is the renderer.

## 2. UnleashedRecomp — a SHIPPING implementation of our target architecture

Sonic Unleashed, statically recompiled, runs as a native PC port. Its renderer is
a **translation layer, explicitly NOT a GPU emulator** — the same thing our
"full native BD HLE" verdict calls for. What it confirms, point by point:

- **No EDRAM at all.** Render targets are plain native textures (`GuestSurface`
  holding a `RenderTexture`); depth is a normal `D32_FLOAT` resource. There is no
  tile layout, so there is nothing to reinterpret — which is precisely why their
  frame has no equivalent of BD's 45 bridging transfers.
- **Resolves become explicit GPU passes.** They keep dedicated resolve pipelines:
  `g_resolveMsaaColorShaders[0..2]` and **`g_resolveMsaaDepthPipelines[i]`**.
  🎯 **This is the direct answer to our depth wall:** our own RenderDoc data says
  BD's 23 dominant depth transfers are MSAA 1<->2 conversions (the rest pitch
  downscales 720->400). A shipping port handles exactly that class with a native
  MSAA **depth** resolve pipeline instead of an EDRAM transfer. Our
  `BdNativeRenderer::ResolveSurface` (per-surface MSAA resolve, commit d7427f846)
  is the same construct — it is the right substrate, it needs the depth variant.
- **Native vertex declarations** -> native input layouts (vertex shaders take
  real inputs). Validates our plan's "native VkBuffer vertex-input, NOT SSBO
  vertex-fetch".
- **Constant buffers**: root constant buffers on D3D12; on **Vulkan the constant
  buffer GPU addresses are passed as push constants**. Directly applicable — we
  already landed push-descriptor capture for the decouple work.
- **Pipeline compilation is folded into the game's asset loading** => no
  first-appearance stutter. This is the GPU-side analogue of our AOT-primary CPU
  direction, and a *lower-power* lever too (codegen spikes move off the frame).
- **XenosRecomp** converts Xenos shader binaries -> HLSL -> DXIL/SPIR-V via DXC.
  We do not need it (xenia's Xenos->SPIR-V translator already exists), but it
  confirms shader translation is a solved, non-blocking part of the problem.

## 3. ReXGlue — the AOT model, and it already targets Blue Dragon

Xbox 360 -> portable C++ AOT recompiler, "heavily rooted in Xenia's
foundations", inspired by XenonRecomp/rexdex. Within weeks of release, alpha
builds existed for **Blue Dragon** (our exact title), Ninja Gaiden 2, DBZ Budokai
HD, DBZ Raging Blast 2.

- **Register model = hybrid**, exactly what our residency levers approximate:
  registers hot in a function become C++ **locals** (`PPCRegister`), everything
  else lives in the `PPCContext&` passed through. Our
  `cpu_backend_llvm_residency_writeback` (within-function) +
  `cpu_backend_llvm_residency_abi` (across calls) are the JIT-side equivalent of
  their locals-vs-context split. Independent convergence on the same design.
- **Indirect calls** use a guest-memory function table indexed
  `(address - CODE_BASE) * 2` (`PPC_LOOKUP_FUNC`); `bctr` switch patterns become
  real C++ `switch` blocks. NOTE: we already **evaluated and ruled out** the
  direct-index table for xenia (2026-07-23) — over BD's ~16MB code range the
  table is ~32MB and blows the cache, whereas our 64KB self-validating resolve
  cache stays hot. Their model wins only because AOT dereferences it inline. That
  ruling stands; this is a difference in execution model, not a missed lever.
- **MMIO**: GPRs holding addresses >=0x7F000000 are tracked in an
  `mmio_base_regs` bitmask to emit optimized accessors — a cheap idea our
  frontend could copy if MMIO decode ever shows up hot.

## 4. Box64 CALLRET — the named form of our return-trampoline

Box64's `CALLRET` optimizes guest CALL/RET by using semi-direct native call/ret
and **skipping the jump-table dispatch** when possible: **>10%, and 10-20% in
some cases**. `CALLRET=2` adds handling for returns into dirty/modified blocks.
It composes with **Secondary Entry Points (SEP)** — extra native entry points
registered at CALLRET return sites.

Mapped to us: every non-tail guest call from the LLVM backend goes through the
C helper `xe_llvm_guest_call` -> resolve -> `host_to_guest_thunk`, because the
guest->guest ABI returns by *branching to x0*, not by a host `ret`
(llvm_assembler.cc:256-288). The documented fix is a **per-call-site host return
trampoline**: a `blockaddress` of the post-call point registered in the
guest->host map as the x0 the callee branches back to — i.e. Box64's SEP, exactly.
Two prior attempts used a bare `CreateCall` (no return stub) and crashed BD at
opt=2; the trampoline is the correct form. This is the #1 remaining CPU lever and
it is what unlocks full cross-function residency.

## 4b. HOW to build the depth resolve on a TBDR (the Thor-specific correction)

UnleashedRecomp resolves depth with *pipelines* (shader passes) because its
abstraction targets D3D12 too. **Copying that shape verbatim would be wrong for the
Thor**: a separate resolve pass is another render pass = another GMEM tile
store/reload = exactly the cost the native renderer exists to delete.

Two facts from this repo:
- `BdNativeRenderer::ResolveSurface` (bd_native_renderer.cc:137) resolves with
  `vkCmdResolveImage`, and that call is **COLOR-ONLY** in Vulkan - it cannot be
  pointed at a depth image. So the depth variant is NOT a small edit to it.
- **`VK_KHR_depth_stencil_resolve` appears NOWHERE in the codebase** (no hit in
  src/xenia/gpu/vulkan or src/xenia/ui/vulkan). It is **core in Vulkan 1.2** and
  the Thor reports **Vulkan 1.3**, so the API is available unconditionally; only
  the supported *modes* need querying via
  `VkPhysicalDeviceDepthStencilResolveProperties` (`supportedDepthResolveModes`,
  `independentResolve`/`independentResolveNone`). SAMPLE_ZERO is the mode our
  1<->2 MSAA depth conversions want (pick one sample), with MIN/MAX as
  conservative-Z alternatives.

**⇒ Recommended build: attach the depth resolve to the native pass itself via
`VkSubpassDescriptionDepthStencilResolve` (a resolve ATTACHMENT), not a separate
resolve pass.** On a TBDR the resolve then happens as part of the pass's existing
tile store - zero extra passes, zero extra GMEM round-trips - which is precisely
the property that makes it a deletion of BD's 23 MSAA depth transfers rather than a
relocation of them. Verify the mode support on-device first (one query, no run).

1. **The full native BD HLE stays the call** — now with an existence proof that
   the architecture ships and with their resolve model to copy.
2. **The depth lane is NOT closed the way we wrote it.** Our "depth deletion is
   un-winnable (temporal-snapshot wall)" verdict was derived under the *hybrid*
   (native color + LLE depth). UnleashedRecomp's native MSAA depth-resolve
   pipelines are the full-native answer to the same conversion class. Treat the
   depth-lane-closed note as hybrid-scoped, per the 5.6-sol full-native verdict.
3. **CPU: build the return trampoline (SEP)**, not more micro-levers — the
   literature caps the rest at ~1.15x.
4. **Steal pipeline-precompile-at-asset-load** for the Vulkan side (stutter +
   power).

Sources: [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) ·
[XenosRecomp](https://github.com/hedge-dev/XenosRecomp) ·
[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) ·
[ReXGlue recompilation concepts](https://mintlify.wiki/rexglue/rexglue-sdk/concepts/recompilation) ·
[Box64 usage docs (CALLRET/SEP)](https://github.com/ptitSeb/box64/blob/main/docs/USAGE.md) ·
[Box86/64 dynarec writeup](https://box86.org/2024/07/revisiting-the-dynarec/) ·
[Partial cross-compilation for DBT, arXiv:2512.00487](https://arxiv.org/abs/2512.00487) ·
[Boosting cross-architectural emulation, arXiv:2501.03427](https://arxiv.org/pdf/2501.03427) ·
[Peephole optimization in DBT (MDPI 13/9/1608)](https://www.mdpi.com/2079-9292/13/9/1608)
