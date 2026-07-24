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

### ✅ ANSWERED ON DEVICE (Thor, 2026-07-24) — the capability is there

The query committed this session reports, on the Thor's **MesaTurnip** driver:

```
* driverID: MesaTurnip / driverName: turnip Mesa driver
* depthStencilResolve: depthModes=0x21 stencilModes=0x21
                       independentResolveNone=0 independentResolve=0
```

- `0x21` includes **`VK_RESOLVE_MODE_SAMPLE_ZERO_BIT` (0x1)** for BOTH depth and
  stencil. SAMPLE_ZERO (take sample 0) is exactly what BD's MSAA 1<->2 depth
  conversion needs. **So the TBDR depth-resolve path is AVAILABLE.**
- ⚠️ **Constraint to design around:** `independentResolve = 0` AND
  `independentResolveNone = 0` mean depth and stencil must use the **same** resolve
  mode - you cannot resolve depth while leaving stencil at NONE. Satisfiable here
  because both masks are identical (0x21), so set SAMPLE_ZERO for both.
- (Bit 0x20 is also set in both masks; SAMPLE_ZERO is the one this build needs, so
  the extra bit is not decoded here rather than guessed at.)

**⇒ Recommended build: attach the depth resolve to the native pass itself via
`VkSubpassDescriptionDepthStencilResolve` (a resolve ATTACHMENT), not a separate
resolve pass.** On a TBDR the resolve then happens as part of the pass's existing
tile store - zero extra passes, zero extra GMEM round-trips - which is precisely
the property that makes it a deletion of BD's 23 MSAA depth transfers rather than a
relocation of them. Verify the mode support on-device first (one query, no run).

## 4c. A MEASURED constraint on the CPU lane's reach (Thor, 2026-07-24)

First on-device run of the LLVM backend this session (`--ez cpu_backend_llvm true`):
- **It works and does not regress:** L0 = **8.1 fps** field vs the a64 baseline
  **7.8 fps**, ALIVE@190s, 0 faults. Cross-run, so read that as "no regression",
  NOT as a win.
- ⚠️ **The rest of that ladder is CONFOUNDED and must not be quoted.** L1
  (+context_residency+writeback) read 9.9 fps, but its screenshot is the "Character
  Design / Akira Toriyama" credits camera while L0's is a different, more open
  shot - different scenes. BD's intro advances at a rate that depends on emulation
  speed, so a fixed wall-clock sample lands on a different FRAME in each run. The
  `LLVMmap` spread (222 / 5685 / 450 / 354) confirms the runs executed very
  different amounts of code. There is no measured residency win.
- ⚠️ **The follow-up "scene-matched" benchmark was ALSO invalid** - twice over, and
  both bugs are worth remembering. Timing a fixed frame range (swap 200->800) of
  the no-input intro DOES match content correctly, but (a) that range lands on the
  near-blank **"press START" title screen** (no work to speed up), and (b) the
  validated base stack contains `vulkan_present_refresh_capped`, which forces FIFO
  locked to the display refresh (vulkan_presenter.cc:67, default TRUE on Android) -
  so all four variants (20.3 / 20.8 / 20.9 / 20.8 s) were sitting **on a ~30fps
  cap**. Comparisons at the cap are meaningless.
  **Corollary that matters: BD's field runs ~8fps, far BELOW that cap, so field
  numbers are never cap-limited - only scene-confounded.** The correct instrument
  is a fixed-frame window resident IN THE FIELD.
- **Net: the residency lane is still UNMEASURED on a meaningful workload.** But see
  the prior below - for BD specifically it is also unlikely to be the lever, since
  the field is GPU-bound (119ms of 123ms is EDRAM tile-store, measured earlier).
- **Lowering coverage is high:** `LLVMbegin` == `LLVMmap` == 1865 in-run — every
  attempted lowering succeeded.
- **But there is a systematic hole:** 30+ UNIQUE functions logged
  `LLVMfallback ... -> a64`, and **every one is the same opcode class,
  `mul_add`/`mul_sub`** (vector vmaddfp/vnmsubfp). That is the deliberate
  `cpu_backend_llvm_lower_vmaddfp=false` workaround for the device
  codegen/regalloc interaction bug (memory: bd-llvm-postload-3d-cyan-bug).
- ⚠️ Checked, not assumed: BD's known vertex-transform 0x82282490 did **not**
  appear in this run's fallback list, so no claim is made that the hottest
  geometry routine is among them.

**Implication:** every guest function using vector MUL_ADD/MUL_SUB is excluded from
the LLVM backend and therefore from residency, cross-function residency, and the AOT
object cache — the entire committed CPU direction. Float-heavy math is exactly the
code that direction is meant to win on. **So fixing the vmaddfp lowering is not a
correctness side-quest; it is a COVERAGE lever that widens the CPU lane**, and it
should be priced against the return-trampoline when picking the next CPU build.

## 4d. MEASURED GPU WIN: the color-drop HLE is 1.20x, confirmed (Thor, 2026-07-24)

Ran the committed color-drop HLE against pure LLE on the field with per-frame
tracing, and compared with **matched-draw-bucket `gpu_frame_us`** (2310 vs 2521
rendering frames). Tools: `scratch/thor-debug/bd_gpu_hle_ab.ps1` +
`tools/gpu_ab_analyze.py`.

| | A: pure LLE | B: color-drop HLE |
|---|---|---|
| matched-bucket weighted | **75421 us** | **62662 us** |
| heavy field buckets (~1100-1250 draws) | ~129 ms | ~108 ms |
| `rt_xfers_dropped` per frame | 0 | **30 of 45** |
| `gpu_pass_us` (in-pass) | 2034 us | 17365 us |

**B/A = 0.831 => 16.9% faster => 1.204x**, independently reproducing the recorded
-17%. Render is correct (screenshot: village + Shu) apart from the known
pre-existing right cyan strip.

- **Mechanism confirmed in the same trace:** 30 EDRAM ownership transfers deleted
  per frame. On a TBDR each is a render pass = a GMEM tile store, so this is both
  the speed win and the energy win (less memory traffic). `gpu_pass_us` RISING
  2034->17365us is the predicted signature of work FUSING into fewer, larger native
  passes rather than disappearing.
- 🛑 **Why the method matters:** the NAIVE whole-run medians were A=65682us vs
  B=104966us, which would have reported the HLE as **60% SLOWER**. That is pure
  scene-mix artifact - the two runs sampled different scenes. Only the matched
  buckets are valid, and they show a consistent ~0.84 ratio across *every* heavy
  bucket. This is the same confound class that invalidated this session's CPU
  ladder; `tools/gpu_ab_analyze.py` exists so it stops recurring.
- **Power:** B rendered 9% more frames (2521 vs 2310) in the same 190s window at a
  comparable end temperature (68.1C vs 67.0C) => better energy per frame. Absolute
  watts are not measurable while USB-attached (charging pollutes `current_now`), so
  no wattage figure is claimed.

**Where this leaves the target:** the heavy field is still ~108ms (~9fps) against a
33333us (30fps) budget, and **15 of 45 transfers still execute - the DEPTH ones**.
Deleting those via the in-pass depth resolve attachment (device-confirmed available,
section 4b) is the next lever, and it is the same ~3x that separates 108ms from 33ms.

## 4e. XeO3 / Fission (Microsoft's own 360 emulator) + XWine1 — user pointer, 2026-07-24

**XeO3** (codename **Fission**, ships as `emu.exe`) is *Microsoft's* Xbox 360
emulator — the tech behind Xbox One / Series backward compatibility, recently
ported to PC and datamined out of the new Xbox PC BC package. It emulates the Xenon
PPC CPU, the 360 hypervisor, and Xenos **on top of Direct3D**. **XWine1** is an Xbox
One→Windows translation layer whose **SlimEra** component was combined with the XeO3
PC port to launch dumped 360/OG-Xbox titles (Plants vs. Zombies, Steins;Gate) with
"varying degrees of success". Not a public release; needs MS's own BC files.

**What it CONFIRMS for us (this is the valuable part):**
1. **Per-title execution profiles + game-specific patches are how a 360 emulator
   ships at scale.** Fission supports 632 360 titles, each with its own patches,
   configuration and "execution profiles"; missing profiles are a known failure
   cause. That is *exactly* our `GameProfiles` + `.patch.toml` + per-game HLE toggle
   (the Cemu model) — independent validation from the vendor implementation.
2. **Shaders are PRE-COMPILED per title**, shipped as DX11 Durango-format shader
   DLLs inside the BC package, with a `BackgroundShaderCompiler.exe` runtime
   recompiler as the fallback for titles lacking them. Same lesson as
   UnleashedRecomp's compile-at-asset-load: **bake pipelines ahead of time**. Our
   analogue is a per-title AOT shader/pipeline precompile (we have
   `vulkan_persistent_pipeline_cache`; front-loading it is the lever).
3. **MS's CPU side is a JIT** (with an interpreter option), not full static
   recompilation. Useful counterweight to the ReXGlue/AOT framing: a JIT is
   evidently *sufficient* if the GPU side is right — consistent with our own
   measurement that BD's field is GPU-bound, not CPU-bound.
4. It translates Xenos **to Direct3D** rather than shipping a 95-pass EDRAM
   emulation — a third independent vote (with DXVK and UnleashedRecomp) for the
   native-translation architecture.

**What it does NOT give us:** no public detail on EDRAM / tiling / resolve
handling — the wiki explicitly says the XeO3 shader format is not understood and its
DX11 Durango shaders cannot currently be created or rendered. So there is nothing to
lift for our actual wall.

**⚠️ Weak as a RUNTIME path for the Thor, and I would not chase it:** XeO3 is a
proprietary x86/Windows binary. Running it on the Thor means Box64/FEX + Wine +
DXVK/vkd3d — i.e. **nested emulation**: XeO3 JITs PPC→x86, then Box64 JITs
x86→ARM64. Double translation of the hottest path is a strong prior for poor
performance, quite apart from needing Microsoft's own BC files. RE2 runs well under
GameNative because it is *native x86 code*, not an emulator. Treat XeO3 as
**architectural evidence**, not as a shortcut.

Sources: [Xbox One Research wiki — XeO3](https://xboxoneresearch.github.io/wiki/games/xeo3-x360-classic-xbox-emulator/) ·
[Emulation General — Fission](https://emulation.gametechwiki.com/index.php/Fission) ·
[VideoCardz — MS PC emulator runs dumped 360 games](https://videocardz.com/newz/microsofts-new-xbox-pc-emulator-already-runs-dumped-xbox-360-games) ·
[XWine1 announcement](https://x.com/XWineOne/status/2080172284422029742)

## 5. What this changes about our plan

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
