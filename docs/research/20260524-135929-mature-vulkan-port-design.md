# Mature Vulkan Port Design Notes

Date: 2026-05-24

Task: research how mature emulator Vulkan ports are built and upgrade the
xenia-thor GPU design guidance. This is a docs-only design pass; Thor device
visibility was still checked, and future runtime changes should prefer Thor
proof.

## Sources

- Dolphin Emulator, "Ubershaders: A Ridiculous Solution to an Impossible
  Problem", accessed 2026-05-24:
  https://ca.dolphin-emu.org/blog/2017/07/30/ubershaders/?nocr=true
- RPCS3, "Rewriting Vertex Processing for Massive Performance Gains", accessed
  2026-05-24:
  https://rpcs3.net/blog/2017/08/16/rewriting-vertex-processing-for-massive-performance-gains/
- RPCS3, "Eliminating Stutter with Asynchronous Shader Implementation",
  accessed 2026-05-24:
  https://rpcs3.net/blog/2018/08/08/eliminating-stutter-with-asynchronous-shader-implementation/
- Cemu Wiki, "Release 1.19.0", accessed 2026-05-24:
  https://wiki.cemu.info/wiki/Release_1.19.0
- PPSSPP, "1.15.x release process", accessed 2026-05-24:
  https://www.ppsspp.org/blog/
- PPSSPP, "Vulkan Driver Bugs", accessed 2026-05-24:
  https://www.ppsspp.org/docs/articles/vulkan-driver-bugs/
- Khronos Vulkan Guide, "Pipeline Cache", accessed 2026-05-24:
  https://docs.vulkan.org/guide/latest/pipeline_cache.html
- Khronos Vulkan Guide, "Tile Based Rendering Best Practices", accessed
  2026-05-24:
  https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html
- Android Developers, "Tools and advanced features", accessed 2026-05-24:
  https://developer.android.com/games/develop/vulkan/tools-and-advanced-features

## Pattern From Mature Emulator Ports

Mature Vulkan ports are not just a renderer rewrite. They become mature by
building reproducible evidence loops around the renderer:

1. Shader and pipeline compilation is treated as a first-class runtime system.
   Dolphin's ubershader work, RPCS3 shader cache work, Cemu async pipeline
   compilation, and PPSSPP parallel Vulkan pipeline creation all attack the
   same issue: stutter and correctness risk from discovering pipeline state at
   draw time.
2. Async compilation is never a free win. Cemu explicitly documents the tradeoff
   that draw calls may be skipped while pipelines compile, which can break games
   with GPU-side semantics. Dolphin rejected simple async shaders for the same
   reason in favor of an interpreter-like ubershader fallback.
3. Renderer speedups come from moving console-GPU-shaped work to the host GPU
   when the evidence proves it is safe. RPCS3's vertex processing rewrite moved
   CPU-side preprocessing into GPU-side fetch behavior, but only after measuring
   where the frame time was going and then fixing frame-to-frame waiting.
4. Device profiles matter, especially on Android. PPSSPP keeps a living record
   of driver bugs and workarounds, and Android/Vulkan guidance assumes
   validation, RenderDoc, GFXReconstruct, AGI, and OEM profilers are separate
   tools with different perturbation costs.
5. Mobile Vulkan performance is bandwidth and synchronization sensitive.
   Khronos TBR guidance says to keep data on-chip when possible, use clear
   load/store intent, avoid unnecessary external-memory round-trips, and keep
   pipeline barriers specific enough to allow overlap.

## xenia-thor Design Upgrade

For this fork, "mature Vulkan port" should mean a ladder, not a single feature:

1. Device profile and smoke tests.
   - Keep `WindowDemoActivity`, Vulkan trace viewer, and feature/format logging
     as the first gate.
   - Record Adreno 740 driver version, extensions, format support, and known
     fallbacks in dated notes before relying on optional Vulkan behavior.
   - Reject software Vulkan devices and missing mandatory paths explicitly.

2. Console GPU milestone classification.
   - Continue classifying `VdSwap`, `PM4_XE_SWAP`, Vulkan `IssueSwap`,
     frontbuffer content, resolve candidates, and presenter output separately.
   - A black screen is not one bug class. It can be guest video service, ring
     dispatch, render-target/resolve content, texture fetch, or presentation.

3. Deterministic trace and replay before broad GPU edits.
   - Keep Xenia GPU trace viewer and Android log/capture metadata as the base.
   - Add a future Android trace/replay lane for Vulkan API behavior only when a
     normal route capture proves GPU-side work is the wall.
   - Use RenderDoc/AGI/GFXReconstruct as focused tools, not default speed proof.

4. Shader/pipeline cache maturity.
   - Current `VulkanPipelineCache` has in-process shader and pipeline maps.
     Mature behavior needs a persistent pipeline-cache plan keyed by app build,
     driver/device identity, shader translator flags, render-target path, and
     guest shader hashes.
   - Add pipeline creation timing, cache hit/miss, and first-use stutter
     counters before attempting async compile or prewarm behavior.
   - Any async or approximate path must be cvar-gated, correctness-logged, and
     safe against console GPU side effects such as EDRAM copies, conditional
     writes, predication, query/writeback behavior, and render-to-texture use.

5. EDRAM/resolve/texture-cache correctness first.
   - Treat Xenos EDRAM as console state, not ordinary host images.
   - Every resolve optimization needs source surface, format, pitch, mask,
     copy/resolve sequence, texture-source consumer, and final presenter proof.
   - For Adreno, prefer exact format/feature probes and per-chain fallbacks over
     broad format hacks.

6. Explicit synchronization and frame pacing.
   - Measure queue submit, queue wait, fence wait, swap acquire/present, device
     idle, and CPU-side command construction separately.
   - The existing `VulkanGPUCompletionTimeline` fence path should become the
     anchor for GPU wait attribution before "push more GPU" work.
   - Do not chase GPU occupancy while the quiet profiler still shows the main
     CPU/JIT thread near one full core and GPU Commands in the low-teens.

7. Regression evidence.
   - Keep validation, RenderDoc, AGI, trace replay, and quiet FPS captures as
     separate lanes.
   - Every GPU behavior patch needs a normal route capture plus a trace-off
     speed/control capture if the claim is performance.
   - Title-specific GPU cvars stay default-off until a generic invariant or a
     multi-title smoke lane proves they are safe.

## Current Blue Dragon Implication

As of the latest 2026-05-24 quiet evidence, Blue Dragon on Thor is still
CPU/JIT-bound. The all-three `stvewx` fastpath A/B reached matching opening
frames, kept GPU Commands around 11.5%, and did not prove route-wide speed.
That means the next Blue Dragon full-speed slice should stay on the A64/JIT
state/vector/FPR lane unless a fresh route-clean capture shows high GPU wait,
high resolve/copy cost, shader/pipeline stalls, or present pacing as the wall.

The Vulkan design should still be upgraded now so that when the CPU wall drops,
the GPU lane is ready and does not regress into ad hoc RenderDoc pokes.

## Repo Changes From This Design Pass

- Added this research note.
- Added a compact mature Vulkan port ladder to `AGENTS.md`.
- Updated `.agents/skills/xenia-vulkan-adreno-renderdoc/SKILL.md` so future GPU
  work starts from device profile, milestone classification, cache/trace/sync
  evidence, and mobile/Adreno constraints.

No build, deploy, or Thor capture was required for this docs-only design pass.
User correction after the first edit: unrelated Windows-only PS3/Eternal Sonata
tests are not a xenia-thor active slice by themselves. Future xenia-thor runtime
changes should still prefer Thor proof unless a real same-device, same-repo,
same-git, or host-saturation conflict exists.

Validation:

- `git diff --check` passed with only the existing LF-to-CRLF warnings.
- Repo-local `xenia-vulkan-adreno-renderdoc` skill validation passed.
- `adb devices -l` showed Thor `c3ca0370` online as `AYN_Thor`.
