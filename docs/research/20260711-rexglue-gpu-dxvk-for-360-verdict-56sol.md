The user’s architectural instinct is right, but the labels are wrong:

- This is not an “easy D3D9 port.”
- It is not AOT-recompiling GPU draws.
- It is a BD-specific, runtime D3D9-Xbox HLE renderer/framegraph, optionally fed by AOT-recompiled PPC later.

If rendering—GPU execution or renderer submission CPU—is on the critical path, option A is the only proposal with credible 3× upside. If unrelated guest CPU work already exceeds 33.3 ms, A is a detour and a perfect renderer cannot deliver 30 fps.

## Q1: Is Xbox 360 D3D9 easily portable to Vulkan?

No. The concepts are portable; the existing execution boundary is not.

PC DXVK receives a stable, high-level `d3d9.dll` interface: resource creation, surface handles, render-target binding, state changes, draws, and copies. It replaces that DLL and retains the semantic information needed to manage Vulkan resources. [DXVK’s own installation model explicitly replaces `d3d9.dll`](https://github.com/doitsujin/DXVK).

Xbox 360 breaks that model in three precise places:

1. **There is no stable host-visible API boundary.**

   The XDK’s D3D9-like runtime is linked into the XEX and becomes ordinary PPC game code. Xenia’s own GPU documentation describes it as part of the executable, directly interacting with the hardware. By the time Xenia normally sees work, calls such as `SetRenderTarget`, `Resolve`, and `DrawIndexedPrimitive` have already been lowered into PM4 registers and command buffers. [Xenia eDRAM architecture deep dive](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html).

   Byte signatures can recreate an API boundary, as this fork’s `SetupExtern` mechanism demonstrates, but signatures will vary with XDK version, compiler/linker output, inlining, title updates, and game wrappers. A signature database is feasible, not magically universal. The current interception infrastructure is real and proven in [processor.cc](<C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/src/xenia/cpu/processor.cc:1290>).

2. **PM4 has discarded logical resource identity.**

   At the hardware level there are no `IDirect3DSurface9`-style handles—only EDRAM address, pitch, format, MSAA state, registers, and resolve destinations. Xenia explicitly notes that hardware-level render-target identity must be inferred from those properties, and that height is not even directly specified. The same EDRAM range may be reused for multiple unrelated surfaces or reinterpreted under another format. [Xenia’s explanation is especially explicit here](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html).

   That is exactly why BD’s base-0 and same-size surfaces collide in the current reconstruction; the fork documents this at [vulkan_command_processor.cc](<C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/src/xenia/gpu/vulkan/vulkan_command_processor.cc:4465>).

3. **Xbox D3D9 semantics are not PC D3D9 semantics.**

   Predicated tiling, explicit EDRAM resolves, sample selection, exponent bias, destination swap, EDRAM reinterpretation, 7e3 color, 20e4 depth, Xenos microcode, endianness, and arbitrary shader vertex fetch all need translation or deliberate elimination.

   Shader portability is the solved half. Renderer/resource semantics are not. XenosRecomp itself warns that its Unleashed implementation is title-specific: native vertex input requires endian conversion and semantic fixes; locations are hardcoded; mini-fetch, integer constants, and texture features remain incomplete. [XenosRecomp README](https://github.com/hedge-dev/XenosRecomp).

So the exact answer is: **BD’s D3D9 usage is translatable, but only after reconstructing the high-level resource/state API that PM4 erased.** The “easy” part starts after that reconstruction.

RE2 proves the Thor can run a much larger modern renderer. It does not prove whether BD’s current 100 ms is GPU execution, GPU starvation, or guest CPU work.

## Q2: AOT versus runtime GPU translation

“AOT-recompile the GPU draws” is a category error.

PM4 buffers are runtime data. Draw count, indices, constants, visibility, texture addresses, render targets, and ordering depend on live game state. Static recompilation cannot turn a dynamic frame into a fixed Vulkan command buffer.

Meaningful AOT targets do exist:

- PPC instructions and functions.
- Known Xenos shader binaries.
- Observed pipeline combinations.
- D3D runtime functions or call sites, replaced with direct host calls.
- Static reflection describing shaders, vertex declarations, or resource layouts.

But those AOT-generated host calls still execute at runtime with dynamic arguments. XenonRecomp exposes weak function overrides and mid-assembly hooks precisely for this purpose—and explicitly says it provides no runtime or renderer. [XenonRecomp README](https://github.com/hedge-dev/XenonRecomp).

Therefore:

- **A—runtime HLE at the recovered D3D seam—is the architectural lever.**
- **B—AOT callers—is an optimization of A, not an alternative to it.**

AOT does not collapse 95 passes. The renderer does.

The correct order is:

1. Intercept and validate runtime state/draw/resource semantics.
2. Build the separate renderer.
3. Profile host-transition overhead.
4. Only then replace hot redirects with direct AOT calls or batch at `EndTiling`/frame-submit level.

This also exposes why the prior “native HLE” was not a valid test: it generated synthetic PM4 and sent it back through `ExecutePacket → ExecutePacketType3Draw → IssueDraw`, preserving CP translation and Xenia’s normal backend assumptions. That is visible directly in [command_processor.cc](<C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/src/xenia/gpu/command_processor.cc:2905>).

A true implementation must enqueue native HLE draw records directly into the separate renderer. No synthetic PM4, no EDRAM `RenderTargetCache`, and no ownership-transfer planning for HLE-covered work.

## Q3: Is the full native renderer the right architecture?

Yes—if the rendering stack is on the critical path. It should be described as a **BD-specific translation layer**, not a generic DXVK-for-360.

Unleashed Recompiled validates the broad model: its renderer was written from scratch, translates the game’s draws to modern APIs, deliberately skips unnecessary original-hardware behavior, and includes game-specific copy/shadow optimizations. It does not claim that Xbox 360 rendering becomes automatically generic. [Unleashed Recompiled renderer description](https://github.com/hedge-dev/UnleashedRecomp).

The right BD design is:

- Intercept resource creation/binding, RT/depth binding, resolve, texture/shader/state changes, and draws—not only `0x82489F40`.
- Preserve logical D3D surface/texture object identity above PM4.
- Build a runtime dependency graph.
- Keep the main field contiguous until its first consumer.
- Use a handful of native passes where dependencies require them.
- Bind resolved render-to-texture images directly when semantics permit.
- Materialize conversion/copy nodes only where BD actually observes the converted representation.
- Never execute the corresponding PM4/EDRAM backend work.

“One held-open pass” is a valid goal for the main field, but not an invariant for the entire frame. Interleaved shadow producers, compute/copies, feedback, and post-processing may require pass boundaries. Vulkan cannot arbitrarily insert copies or dispatches inside a normal graphics render-pass instance. [Vulkan render-pass rules](https://docs.vulkan.org/spec/latest/chapters/renderpass.html).

The three hardest concrete problems are:

1. **Logical resource identity and lifetime — tractable, but only above PM4.**

   Current reverse mapping fails because EDRAM base, pitch, format, and MSAA do not uniquely identify same-sized logical resources; resolve destination becomes known later. The solution is to intercept `CreateSurface`/`SetRenderTarget`/`SetDepthStencilSurface`/`Resolve` and key native resources by guest D3D object identity plus generation, not EDRAM coordinates.

   This is tractable for BD because the relevant D3D runtime and device state have already been located. It becomes a true blocker only if BD bypasses the runtime and authors essential PM4 directly outside every recoverable seam.

2. **Resolve, reinterpretation, and dependency semantics — tractable per game, not generically.**

   A resolve can be elided only when later consumers merely need the logical produced image. It must be materialized when BD depends on quantization, sample selection, exponent bias, channel swap, scaling, clear side effects, or CPU-visible memory.

   The native framegraph should classify each resolve edge as:

   - alias producer image directly;
   - lazy format-convert at first consumer;
   - explicit copy/scale/resolve;
   - CPU-visible materialization.

   Custom resolve going negative in the LLE backend does not invalidate this architecture. The HLE win is avoiding most intermediate surfaces entirely, not performing the same 23 resolves more efficiently.

3. **Mixed-resolution depth — the hardest gating problem, but not a fundamental blocker.**

   Your evidence that dropping depth transfers blackens the field means they carry real information. A native implementation must track logical depth surfaces, their resolution/sample state, clears, and any depth-as-texture consumers.

   Vulkan does not permit format conversion between depth images through `vkCmdBlitImage`, and blitting cannot use multisampled images, so a 720↔400 or Xenos-20e4 conversion may require a specialized compute/fragment path. [Vulkan `vkCmdBlitImage` restrictions](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBlitImage.html).

   This is multi-session work, but finite for BD: identify the actual depth producer/consumer edges and implement only those conversions.

One correction to the proposed first implementation: **do not gate initial correctness on native vertex input.** Xenia’s current native-fetch variant deliberately rejects mini-fetches, computed indices, unsupported strides, and non-contiguous formats in [spirv_shader_translator.cc](<C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/src/xenia/gpu/spirv_shader_translator.cc:1405>). Reuse the proven SSBO fetch initially; replace it draw-class by draw-class later. Pass/EDRAM deletion is the structural win.

The real reason this has not been completed is therefore not merely “large rewrite.” Previous attempts crossed abstraction levels: they redirected selected draws after Xenia had already planned EDRAM state, while other producers and consumers remained EDRAM-authoritative. That guarantees double work, alias ambiguity, and render-target-cache state divergence. The existing investigation records these failure modes in the [native renderer plan](<C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/docs/research/20260705-native-vulkan-renderer-plan.md:149>).

## Q4: What if BD is genuinely CPU-bound?

Then a GPU-only rewrite does not deliver 30 fps.

The hard bound is 33.3 ms. If non-wait, non-renderer guest CPU work is 70–100 ms, reducing the GPU to 4 ms still leaves approximately 10–14 fps. A CPU thread sleeping on a GPU fence is not CPU-bound; it is GPU-bound or synchronization-bound.

There are three distinct cases:

- **GPU active-work bound:** A helps.
- **CPU bound in guest D3D command construction or Xenia CP/PM4 translation:** a true A also helps, because it skips both the guest D3D lowering body and the CP decoder. The existing synthetic-PM4 experiment did not test this.
- **CPU bound in simulation, animation, VMX math, kernel synchronization, or unrelated JIT work:** A is a large detour. C is the correct lane.

The approximately 190 redirects per frame are not automatically a wall. At 30 fps, even 20 µs each is 3.8 ms/frame. If they are expensive because they re-enter guest execution, take locks, or synchronize threads, batch them behind one higher-level frame/`EndTiling` handler. Measure before building B.

The clean measurement should produce two upper bounds on the exact same frozen frame:

1. **CPU-only bound:** run guest render submission and CP decoding with a null/no-wait GPU backend. Measure guest engine, guest D3D, CP, and non-wait CPU separately.
2. **GPU-only bound:** replay an already-built native/host command stream with guest and CP generation removed, while collecting Turnip render-stage Perfetto data, KGSL clock/busy, and per-pass timestamps.

Mesa exposes separate Turnip render-stage/counter tracing; Adreno may choose GMEM or sysmem dynamically, so first-to-last timestamps alone are not active-work proof. [Mesa Freedreno/Turnip architecture](https://docs.mesa3d.org/drivers/freedreno.html).

Use these go/no-go thresholds:

- `non-render CPU ≥ 33.3 ms`: A alone is falsified for 30 fps.
- `non-render CPU ≤ 25 ms` and GPU active work `> 33 ms`: commit to A.
- `non-render CPU ≤ 25 ms`, GPU active work small, but guest-D3D+CP `> 15–20 ms`: commit to A because it deletes renderer submission CPU.
- GPU active work `≤ 10 ms`, guest-D3D+CP small, total still near 100 ms: stop GPU work; C is mandatory.
- If the CPU-only upper bound remains near 100 ms, C needs roughly 3×. Given Xenia already has whole-function LLVM and an object cache, locked 30 then implies D unless profiling reveals one removable class. The current backend already targets whole-function residency rather than mere compilation caching in [llvm_backend.h](<C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/src/xenia/cpu/backend/llvm/llvm_backend.h:17>).

## Q5: Decisive ranking

| Proven bottleneck | Ranking | Decision |
|---|---|---|
| GPU/EDRAM active work | **A > B > C > D** | Build runtime HLE renderer; use B only after it works. |
| CPU in guest D3D + PM4/CP submission | **A > B > C > D** | A deletes this CPU path too; do not preserve synthetic PM4. |
| CPU in unrelated guest simulation/JIT | **C > D > A > B** | GPU HLE is a detour; invoke D if C cannot get below 33.3 ms. |
| Both unrelated CPU and GPU exceed 33.3 ms | **A + C, otherwise D** | No single rewrite reaches 30. |

My overall recommendation is:

1. **Stop all further incremental LLE/EDRAM experiments.** Custom resolve is the final confirmation that rearranging the same emulated graph has no expected value.
2. **Finish the clean bottleneck split before authorizing the renderer rewrite.**
3. If the critical path is either GPU work or renderer submission CPU, choose **A**. It is the only option with structural 3× upside and can delete both PM4/CP work and EDRAM/pass work.
4. Treat **B as a later implementation optimization of A**, never as a separate project.
5. If unrelated guest CPU is already over budget, choose **C with a hard upper-bound test**. If C cannot demonstrate a path below roughly 25–28 ms, accept **D**: locked 30 requires a multi-month whole-title/static-recompile effort, not more GPU tuning.

Bottom line: **the user is right about rethinking BD’s GPU usage and bypassing Xenia’s LLE renderer. They are wrong that this is “easy D3D9→Vulkan” or “AOT-recompiling the GPU.” The real product is a runtime, resource-aware BD renderer; AOT merely makes its call path cheaper.**

The heavyweight external review was attempted but could not initialize its temp/app-server state under this session’s read-only filesystem, so this verdict comes from the direct repo, Xenia, Vulkan, Mesa, XenonRecomp, XenosRecomp, and UnleashedRecomp audit.
311,958
The user’s architectural instinct is right, but the labels are wrong:

- This is not an “easy D3D9 port.”
- It is not AOT-recompiling GPU draws.
- It is a BD-specific, runtime D3D9-Xbox HLE renderer/framegraph, optionally fed by AOT-recompiled PPC later.

If rendering—GPU execution or renderer submission CPU—is on the critical path, option A is the only proposal with credible 3× upside. If unrelated guest CPU work already exceeds 33.3 ms, A is a detour and a perfect renderer cannot deliver 30 fps.

## Q1: Is Xbox 360 D3D9 easily portable to Vulkan?

No. The concepts are portable; the existing execution boundary is not.

PC DXVK receives a stable, high-level `d3d9.dll` interface: resource creation, surface handles, render-target binding, state changes, draws, and copies. It replaces that DLL and retains the semantic information needed to manage Vulkan resources. [DXVK’s own installation model explicitly replaces `d3d9.dll`](https://github.com/doitsujin/DXVK).

Xbox 360 breaks that model in three precise places:

1. **There is no stable host-visible API boundary.**

   The XDK’s D3D9-like runtime is linked into the XEX and becomes ordinary PPC game code. Xenia’s own GPU documentation describes it as part of the executable, directly interacting with the hardware. By the time Xenia normally sees work, calls such as `SetRenderTarget`, `Resolve`, and `DrawIndexedPrimitive` have already been lowered into PM4 registers and command buffers. [Xenia eDRAM architecture deep dive](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html).

   Byte signatures can recreate an API boundary, as this fork’s `SetupExtern` mechanism demonstrates, but signatures will vary with XDK version, compiler/linker output, inlining, title updates, and game wrappers. A signature database is feasible, not magically universal. The current interception infrastructure is real and proven in [processor.cc](<C:/Users/leanerdesigner/Documents/New project 8/xenia-thor/src/xenia/cpu/processor.cc:1290>).

2. **PM4 has discarded logical resource identity.**

   At the hardware level there are no `IDirect3DSurface9`-style handles—only EDRAM address, pitch, format, MSAA state, registers, and resolve destinations. Xenia explicitly notes that hardware-level render-target identity must be inferred from those properties, and that height is not even directly specified. The same EDRAM range may be reused for multiple unrelated surfaces or reinterpreted under another format. [Xenia’s explanation is especially explicit here](https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html).

