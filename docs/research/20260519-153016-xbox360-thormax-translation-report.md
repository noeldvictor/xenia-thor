# Xbox 360 to AYN Thor Max Translation Research Report

Date: 2026-05-19 15:30 EDT
Target: Xbox 360 emulation/translation on AYN Thor Max, with Blue Dragon Disc 1 as the concrete test title.
Scope: Both the general Xbox 360-to-Android ARM64 problem and the current `xenia-thor` Blue Dragon state.

## Executive Answer

The Xbox 360 is old, but it is not an easy target. Its Xenon CPU is a
custom, in-order, triple-core PowerPC design with SMT and VMX128. Its Xenos GPU
is a custom ATI unified-shader GPU with a 10 MiB EDRAM render backend, unusual
formats, Xbox-specific Direct3D 9-era behavior, and tight CPU/GPU interaction.
The AYN Thor Max is much faster in raw terms: Snapdragon 8 Gen 2, Adreno 740,
LPDDR5x, active cooling, Android 13. The gap is translation complexity, not
just compute.

The techniques that work are hybrid translation techniques:

- CPU: dynamic binary translation from PowerPC/HIR to AArch64, with a code
  cache, direct block chaining, hot-path specialization, precise guest state,
  endian-aware memory lowering, VMX128-to-NEON lowering, and helper calls only
  for rare or complex cases.
- GPU: command stream interpretation plus shader microcode translation to
  SPIR-V/Vulkan, with render-target/eDRAM tracking, resolve/copy correctness,
  format-specific decode/encode logic, and Adreno capability fallbacks.
- System: HLE for kernel, input, storage, audio, timing, and device services,
  because full hardware-level simulation would be too slow on a handheld.
- Research workflow: trace-heavy correctness runs for localization, then
  trace-off performance runs. Mixing the two gives misleading speed answers.

For Blue Dragon specifically, this fork has already reached a useful proof
point: title screen, language selection, and opening scene can be reached on
Thor using scripted nop HID. The current evidence says the immediate speed wall
is CPU/JIT/audio/debug overhead first, GPU second. Adreno/Vulkan is rendering
real frames; the main guest CPU thread and XMA decoder dominate early samples.
This is progress, not a compatibility or playability claim.

## Hardware Comparison

### Xbox 360 Guest Hardware

The Xbox 360 CPU/GPU pair was designed for console game workloads rather than
general desktop compatibility.

CPU, Xenon, from Andrews and Baker's IEEE Micro architecture paper:

- 3 PowerPC cores at 3.2 GHz, with two hardware threads per core.
- In-order execution, 32 KiB L1 instruction and 32 KiB L1 data per core, shared
  L2, large 128-byte cache lines, and streaming-oriented cache behavior.
- VMX128 vector unit extensions, including graphics-oriented packed formats and
  D3D compressed data support.
- Big-endian PowerPC guest behavior, exposed through game code, ABI details,
  memory operations, vector element order, and MMIO-facing data.

GPU, Xenos, from the same architecture paper and Xenia's local Xenos model:

- ATI custom GPU at 500 MHz.
- 48 combined vector/scalar shader ALUs dynamically shared between vertex and
  pixel work.
- 16 texture fetch engines, 16 programmable vertex fetch engines, hardware
  interpolators, and a render backend capable of high fill rates.
- 10 MiB EDRAM on a separate die with dedicated alpha blend, depth/stencil, and
  antialiasing logic. The architecture paper gives the EDRAM interface as 256
  GB/s for render backend operations.
- Render-to-texture requires flushing/resolve behavior between EDRAM and main
  memory, which is exactly the kind of behavior a Vulkan renderer must emulate
  carefully.

Translation implication: CPU and GPU correctness are intertwined. A game can
run guest threads correctly and still show black frames if Xenos resolves,
formats, or render-target aliasing are wrong. Conversely, the GPU can render
real frames while CPU/JIT/audio limits make the game unusably slow.

### AYN Thor Max Host Hardware

The Thor Max target in this project is the Snapdragon 8 Gen 2 model. AYN's Thor
manual lists the Base/Pro/Max group as 8 Gen 2, 4 nm, one GoldPlus core at 3.2
GHz, four Gold cores at 2.8 GHz, three Silver cores at 2.0 GHz, Adreno 740 at
680 MHz, LPDDR5x at 4200 MHz, UFS 4.0 storage, active cooling, Wi-Fi 7, and
Android 13. Qualcomm's Snapdragon 8 Gen 2 brief describes a 64-bit Kryo CPU
with 1 prime, 4 performance, and 3 efficiency cores, plus Adreno GPU support
for Vulkan 1.3, OpenGL ES 3.2, and OpenCL 2.0 FP.

Observed local device baseline in this fork:

- Model reports as `AYN Thor`, platform `kalama`, Android 13 / SDK 33.
- GPU is Adreno 740.
- Xenia logs have shown Vulkan 1.3 instance/device path and Qualcomm
  proprietary Adreno Vulkan driver.
- The Vulkan window demo and Blue Dragon render path have produced visible
  output on-device.

Translation implication: Thor Max has enough raw GPU feature level to be a real
target, but it is a mobile SoC with heterogeneous CPU cores, Android scheduling,
thermal limits, driver-specific format support, and no x86/x64 host backend.
The AArch64 CPU backend is the center of gravity.

## Why Xbox 360 Translation Is Hard

### 1. The CPU Is PowerPC, Not AArch64

PowerPC and AArch64 are both RISC-like, but that does not make direct mapping
trivial. The hard parts are:

- Big-endian guest memory on a little-endian host.
- PowerPC condition register fields, carry/overflow behavior, link/count
  registers, exceptions, and synchronization instructions.
- Guest virtual memory, MMIO, page protections, and precise exception recovery.
- Six guest hardware threads mapped onto Android host threads with different
  core classes and scheduler behavior.
- VMX128 semantics that are close enough to NEON to tempt mistakes but different
  enough to break games: lane order, NaN behavior, denormal behavior,
  saturating packs, magic-float pack/unpack, and custom Xenos half/packed
  formats.

The current code reflects these issues. The A64 backend pins guest context and
memory base in registers, emits host-to-guest and guest-to-host thunks, saves
NEON state, implements byte-swapped loads/stores, has MMIO-aware paths, and
contains explicit Xenos/VMX pack/unpack conversion logic.

### 2. The GPU Is Not Normal Direct3D 9

Xenos is Direct3D 9-era, but not a PC Direct3D 9 GPU. It has:

- Unified shader microcode with Xenos-specific instruction packing.
- EDRAM color/depth render targets, resolves, MSAA layout, wrapping, and
  tile-based addressing.
- Render target formats such as `2_10_10_10_FLOAT` with 7e3-like behavior.
- Format signedness and swizzle behavior that must survive texture cache,
  resolve, and shader fetch translation.
- CPU-visible memory effects through command processor, shared memory, and
  guest-visible registers.

The fork already hit this directly in Blue Dragon: a wrong texture fetch result
exponent source produced black output. Switching result exponent adjustment
from fetch constant dword 4 to dword 3 let Blue Dragon reach the visible title
screen without the old global exponent-bypass proof knob.

### 3. Android Adds Runtime Constraints

Android gives Vulkan and AArch64, but it also adds:

- APK/JNI lifecycle and activity surface management.
- Driver-specific Vulkan behavior on Adreno.
- Scoped file access and storage paths.
- Logcat overhead if tracing is excessive.
- Thermal and governor behavior that can collapse long performance runs.
- Input/audio backends that are not the same as desktop Xenia's normal path.

The handheld target therefore needs a dedicated "fast lane" for profiling:
minimal logging, no shader dumps, no broad checksums, no trace firehose, and
only required compatibility knobs.

## Translation Techniques That Work

### CPU Translation

The academic literature and practical emulator designs agree on the main
shape: interpret cold code only long enough to identify or translate blocks,
then execute cached host code.

QEMU's 2005 dynamic translator paper is the classic retargetable baseline. It
splits target instructions into simpler operations, emits translated blocks,
stores host code in a translation cache, reuses translated code, and applies
direct block chaining to avoid returning to the dispatcher after every block.
It also calls out the hard system-emulation problems: code cache management,
register allocation, condition code optimization, memory management,
self-modifying code, exceptions, interrupts, and user-mode/system-mode
boundaries. This maps closely to what an Xbox 360 PowerPC-to-AArch64 backend
must do on Thor.

Dynamo's PLDI 2000 paper adds an important performance idea: use hot traces and
a fragment cache so frequently executed dynamic paths become contiguous cached
host code. It also emphasizes that the runtime optimizer has to pay for itself:
profiling and optimization overhead must be lower than the execution time saved.

HQEMU extends the idea with multicore host machines: translation and heavier
optimization can happen on helper threads, improving retargetable DBT
performance over plain QEMU on SPEC workloads. This is relevant to Thor Max
because the host has multiple cores, but the emulator must not spend its main
guest CPU thread compiling or logging when it should be running the game.

For `xenia-thor`, the best CPU path is:

1. Keep a simple, reliable HIR-to-AArch64 backend as the base.
2. Minimize guest-to-host transitions in hot blocks.
3. Add or verify direct block chaining and fast indirect branch target lookup.
4. Keep guest context and memory base pinned in host registers where possible.
5. Lower common integer, load/store, branch, and VMX128 ops inline.
6. Use helpers for rare operations, but measure every helper fallback in Blue
   Dragon.
7. Add per-function/per-block counters that are cheap enough for trace-off
   runs.
8. Consider hot trace stitching only after block translation and helper
   fallbacks are under control.

What likely will not work:

- A pure interpreter for Blue Dragon performance.
- Whole-game static recompilation without dynamic discovery, because Xbox 360
  titles use indirect branches, dynamic code/data patterns, imports, and runtime
  OS interaction.
- Treating VMX128 as "just NEON"; the pack/unpack and floating-point edge cases
  are correctness-critical.
- Leaving debug tracing enabled while judging speed.

### GPU Translation

The GPU path is not "translate Direct3D to Vulkan" in the normal API-layer
sense. It is closer to hardware command processor emulation plus shader and
render-target translation:

1. Parse Xbox 360 GPU command packets and register writes.
2. Track Xenos state and guest memory ranges.
3. Analyze Xenos shader microcode.
4. Translate shader behavior to host shader IR/SPIR-V.
5. Compile/cached Vulkan pipelines.
6. Emulate EDRAM render target layout, resolves, MSAA, depth/stencil, blending,
   and format conversions.
7. Present the selected guest output through Android Vulkan.

Xenia's own GPU notes describe why render target cache design matters. Treating
EDRAM render targets like unrelated PC render targets breaks basic cases,
especially where games alias EDRAM memory, resolve depth/stencil through color,
or use tiled/MSAA patterns. Vulkan fragment shader interlock can model some
ordered per-fragment behavior, but it is not a magic performance fix and is not
uniformly ideal on all desktop/mobile GPUs.

For Thor/Adreno, the techniques that work are:

- Feature probing at startup and capability-specific fallback selection.
- Shader-hash focused tracing rather than broad logging.
- Format-specific decode paths for Xenos formats that Vulkan/Adreno does not
  natively expose in the needed way.
- Texture/render-target source checksums only in proof runs, not perf runs.
- Persistent shader/pipeline cache discipline so every frame does not pay
  compile cost.
- Tight handling of descriptor set limits and stage linkage quirks on Adreno.

What likely will not work:

- Relying on optional Vulkan formats without checking Adreno support.
- Treating Xenos EDRAM as normal host images with no alias/resolve model.
- Using fragment interlock or ROV-style strictness everywhere if a render target
  cache path can preserve correctness more cheaply.
- Debug shader overrides as compatibility fixes. They are proof tools.

### System And HLE Translation

Xbox 360 emulation also needs OS/device translation:

- XEX loading, imports, kernel/XAM calls, thread scheduling, timers, events,
  file systems, storage devices, and title paths.
- XInput/HID translation from Android controls.
- Audio/XMA decoding or a cheap bring-up path.
- GPU command processor interrupts and guest-visible register mirrors.

Blue Dragon has already shown that small HLE/timing details matter. The README
and worklogs note KTHREAD timing and GPU ring pointer visibility as progress
points before graphics could move further.

## Blue Dragon Current State

Local project evidence as of 2026-05-19:

- Vulkan window/demo path renders on Thor.
- Blue Dragon Disc 1 launches through `EmulatorActivity`.
- The active ARM64 CPU path is an aX360e/Edge-style A64 backend import, not the
  earlier helper-backed mini-JIT scaffold.
- Blue Dragon reaches the visible `press START` title prompt after the SPIR-V
  fetch exponent source fix from dword 4 to dword 3.
- Sequenced nop HID pulses can press START, select English, and reach the
  opening scene.
- The remaining `vulkan_force_signed_2101010_unorm_fallback=true` knob is still
  part of validated proof runs.
- Trace-off samples still feel slow.
- Early thread sample from the trace-off process showed roughly:
  - Main guest CPU thread: 81.4 percent CPU
  - XMA decoder: 44.4 percent CPU
  - GPU command thread: 14.8 percent CPU
  - Draw thread: 3.7 percent CPU
- No native fatal, AndroidRuntime crash, `VK_ERROR_DEVICE_LOST`, or GPU-hung
  lines were found in that trace-off sample.

Interpretation:

Blue Dragon's immediate rendering wall moved from "black frame" to "real frames
but very slow." The next serious work should not be another broad GPU trace by
default. It should be a no-trace performance workflow, CPU/A64 dispatch and
helper-fallback profiling, XMA/audio cost reduction for bring-up, and only then
deeper Adreno/Vulkan profiling.

## Recommended Technical Roadmap

### Phase 1: Establish A Clean Performance Lane

Create one canonical Blue Dragon speed command:

- No GPU trace/checksum flags.
- No shader dumps.
- No broad log spam.
- `HideAndroidOsd=true`.
- Required compatibility knobs only.
- Scripted input sequence to title/language/opening scene.
- Capture screenshot, meta, process id, cvars, `top -H`, and crash/device-lost
  grep.

Success metric: every speed note records build hash, APK hash, cvars, run
duration, thermal/battery state if available, screenshot, and thread sample.

### Phase 2: CPU/A64 Hot Path

Measure before optimizing:

- Count guest function/block entries and dispatch misses.
- Count helper calls by opcode/category.
- Count guest-to-host transitions and thunk entries.
- Count code cache lookups, direct links, indirect branch misses, and
  recompiles.
- Track top guest PCs for Blue Dragon title/opening scene.

Likely optimization targets:

- Direct block chaining and faster indirect branch target lookup.
- Inline common PPC condition/branch paths.
- Reduce VMX helper fallbacks in animation, decompression, and graphics
  preparation code.
- Keep endian swaps fused with loads/stores and vector pack/unpack operations.
- Remove compiled-call tracing and debug disassembly from speed builds.
- Verify AArch64 instruction cache flush/protection transitions are not
  happening too often.

### Phase 3: Audio Bring-Up Cost

The XMA decoder is already visible in early CPU samples. For graphics/input
bring-up, test:

- `apu=nop` performance comparison.
- A lower-cost XMA decode path or decode throttling for non-audio research
  runs.
- Separate "correct audio" and "graphics performance" test lanes.

This is not a proposal to ship broken audio. It is a way to stop audio from
masking CPU/GPU translation bottlenecks during early Android bring-up.

### Phase 4: GPU Correctness Cleanup

The GPU path is alive, so shift from broad probing to narrowing:

- Replace broad `vulkan_force_signed_2101010_unorm_fallback=true` with a
  narrower Adreno/format/title-chain condition if possible.
- Add a small focused test for `2_10_10_10_FLOAT` render-chain resolve/fetch
  semantics.
- Keep shader-hash filters for B02/title-chain probes.
- Compare host image format support and linear filtering on Adreno 740.
- Run AGI/RenderDoc only after trace-off CPU overhead is under control.

### Phase 5: Input And Longer Progression

Blue Dragon is past title/language with scripted nop HID. Next:

- Real Android controller mapping for Thor controls.
- Repeatable menu/opening scene scripts.
- Save/load behavior validation.
- Longer run stability with thermal notes.
- Only then talk about "gameplay" milestones.

## Practical Ranking Of Techniques

Highest value now:

1. Trace-off speed lane with strict run metadata.
2. A64 backend block dispatch and helper-fallback profiling.
3. XMA/audio shortcut for bring-up.
4. Narrow the remaining 2101010 Adreno fallback.
5. Real Android input mapping.

Medium value:

- Hot trace stitching inspired by Dynamo/HQEMU, after the simple block JIT is
  measured.
- Background optimization threads, if compile cost shows up in samples.
- Pipeline/shader cache tuning after CPU/audio costs are lower.

Low value right now:

- More broad GPU trace flags in speed samples.
- Cosmetic UI work.
- Premature AGI deep dives before CPU thread cost is understood.
- Claiming title/opening scene as compatibility.

## Open Decisions

- Should speed bring-up use `apu=nop` as a standard comparison lane?
- Should the first measurable milestone be "stable title/opening at trace-off"
  or "controllable gameplay state"?
- How much title-specific instrumentation is acceptable before it must be
  generalized?
- Do we want a tiny local benchmark/homebrew XEX lane to validate A64 backend
  changes before Blue Dragon runs?

## Sources

External:

- Jeff Andrews and Nick Baker, "Xbox 360 System Architecture," IEEE Micro
  26(2), 2006. DOI: 10.1109/MM.2006.45. Open PDF mirror:
  https://acg.cis.upenn.edu/milom/cis501-Fall09/papers/xbox-system.pdf
- AYN Thor manual/spec sheet, showing Base/Pro/Max 8 Gen 2, Adreno 740 at
  680 MHz, LPDDR5x, UFS 4.0, Android 13:
  https://manuals.plus/m/7e96f29e93e4e571eb4e2ee5f2220a98db9ab0a295678d69157e99ddfc948028.pdf
- Qualcomm Snapdragon 8 Gen 2 product page:
  https://www.qualcomm.com/smartphones/products/8-series/snapdragon-8-gen-2-mobile-platform
- Qualcomm Snapdragon 8 Gen 2 product brief:
  https://www.qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/Snapdragon-8-Gen-2-Product-Brief.pdf
- Fabrice Bellard, "QEMU, a Fast and Portable Dynamic Translator," USENIX ATC
  2005:
  https://www.usenix.org/conference/2005-usenix-annual-technical-conference/qemu-fast-and-portable-dynamic-translator
- Vasanth Bala, Evelyn Duesterwald, Sanjeev Banerjia, "Dynamo: A Transparent
  Dynamic Optimization System," PLDI 2000:
  https://people.cs.umass.edu/~emery/classes/cmpsci691s-fall2004/papers/bala00dynamo.pdf
- Ding-Yong Hong et al., "HQEMU: A multi-threaded and retargetable dynamic
  binary translator on multicores," CGO 2012:
  https://experts.umn.edu/en/publications/hqemu-a-multi-threaded-and-retargetable-dynamic-binary-translator/
- Toshihiko Koju et al., "Optimizing indirect branches in a system-level
  dynamic binary translator," SYSTOR 2012:
  https://research.ibm.com/publications/optimizing-indirect-branches-in-a-system-level-dynamic-binary-translator
- Khronos Vulkan Guide, "HLSL in Vulkan":
  https://docs.vulkan.org/guide/latest/hlsl.html
- Vulkan specification, fragment interlock scope:
  https://docs.vulkan.org/spec/latest/chapters/shaders.html
- Xenia GPU/render-target cache article, "Leaving No Pixel Behind":
  https://xenia.jp/updates/2021/04/27/leaving-no-pixel-behind-new-render-target-cache-3x3-resolution-scaling.html
- Xenia Vulkan GPU implementation issue:
  https://github.com/xenia-project/xenia/issues/2028

Local project references:

- `README.md`
- `docs/porting/android-arm64-thor-plan.md`
- `docs/worklogs/20260519.md`
- `docs/research/20260519-022333-blue-dragon-title-exp-adjust-probe.md`
- `docs/research/20260519-133516-blue-dragon-dword3-title-fix.md`
- `docs/research/20260519-144858-blue-dragon-nop-hid-sequence.md`
- `docs/research/20260519-151305-blue-dragon-performance-triage.md`
- `src/xenia/cpu/backend/a64/`
- `src/xenia/gpu/shader_translator.cc`
- `src/xenia/gpu/xenos.h`
- `src/xenia/gpu/spirv_shader_translator_fetch.cc`
- `src/xenia/gpu/vulkan/vulkan_texture_cache.cc`
