# Mature Vulkan Port Speed Design Notes

Date: 2026-05-24 16:57 EDT

Branch: `master`

Starting commit: `9d24d9307`

## Goal

Answer the user's concern that other emulator projects have shipped mature
Vulkan ports, and turn that external evidence into concrete xenia-thor design
work without confusing the current Blue Dragon evidence. The latest Thor Blue
Dragon captures still show a CPU/JIT wall: Main Thread is about one full core
while GPU Commands is low-to-modest. This note is the GPU plan for when Vulkan
or presenter cost becomes the wall, not a replacement for the active A64 work.

## Sources Checked

- Dolphin's Ubershaders writeup:
  <https://dolphin-emu.org/blog/2017/07/30/ubershaders/>
- RPCS3 asynchronous shader implementation:
  <https://blog.rpcs3.net/2018/08/08/eliminating-stutter-with-asynchronous-shader-implementation/>
- PCSX2 performance counters and OSD:
  <https://pcsx2.net/docs/troubleshooting/performance/>
- PCSX2 2.0 Vulkan/blending overview:
  <https://pcsx2.net/blog/2024/pcsx2-2-release/>
- Khronos Vulkan pipeline-cache sample:
  <https://github.khronos.org/Vulkan-Site/samples/latest/samples/performance/pipeline_cache/README.html>
- Android Frame Pacing library:
  <https://developer.android.com/games/sdk/frame-pacing>
- Android Vulkan frame pacing integration:
  <https://developer.android.com/games/sdk/frame-pacing/vulkan/add-functions>
- Arm GPU Best Practices Developer Guide:
  <https://documentation-service.arm.com/static/67a62b17091bfc3e0a947695>

## Patterns Worth Borrowing

### 1. Treat GPU perf as counters, not vibes

PCSX2 asks users to capture OSD metrics because the counters identify the
actual hardware-renderer bottleneck. Its hardware-mode OSD names draw calls,
barriers, render passes, readbacks, texture copies, texture uploads, VRAM,
target/source counts, hash cache, and pooled resources. It also ranks likely
impact as readbacks, uploads, copies, barriers, render passes, and then draws.

For xenia-thor, add equivalent low-overhead counters before a Vulkan rewrite
slice:

- host draw calls;
- command-buffer submissions;
- render-pass / dynamic-rendering begins;
- image layout transitions and barrier count by source/destination class;
- readbacks and blocking waits;
- texture uploads and bytes;
- texture copies and bytes;
- pipeline creations and total create time;
- descriptor allocations, updates, and set binds;
- framebuffer/EDRAM resolve count and bytes;
- swapchain acquire/present wait time;
- queue idle / fence wait time.

### 2. Do shader and pipeline work asynchronously where correctness allows it

RPCS3 reduced shader stutter by decoupling decompilation, recompilation, and
linking from the main renderer thread. Dolphin rejected plain async shader skip
as a default because missing objects can create false compatibility issues, but
its Ubershader design shows the stronger pattern: have a correctness fallback
that can render now while specialized shaders/pipelines are prepared.

For xenia-thor:

- keep a correctness-first fallback for missing shader/pipeline variants;
- make shader translation and pipeline creation jobs backgroundable;
- count first-use pipeline stalls and compile time;
- avoid hiding graphics bugs behind a permanent skip-render mode;
- allow a temporary "skip while compiling" debug cvar only for profiling.

### 3. Persist and validate pipeline caches

Khronos documents that pipeline creation can cost enough to cause frame-time
spikes, and the pipeline cache must be saved/reused between runs to avoid
repeat shader compilation. Roblox's pipeline-cache writeup highlights the
fragility of driver-provided cache blobs and the need for app-side validation.

For xenia-thor:

- hash all `VkGraphicsPipelineCreateInfo` / `VkComputePipelineCreateInfo`
  ingredients into a stable Xenia pipeline key;
- persist a Thor-specific pipeline cache under app-private storage or the
  existing Xenia cache path;
- validate device vendor, device ID, pipeline cache UUID, driver version, app
  build hash, shader translator version, and render-state schema;
- discard corrupt or mismatched cache data without failing boot;
- add capture metadata for cache hit/miss, cold/warm run, and pipeline creates.

### 4. Minimize barriers, readbacks, uploads, and render-pass churn

PCSX2's Vulkan progress explicitly calls out blending work where barriers,
render passes, and readbacks received focused attention because too many of
them make performance fall off badly. This maps closely to Xenos/EDRAM work:
the danger is not just draw count, but synchronization and framebuffer feedback.

For xenia-thor:

- instrument every host readback and block-on-readback path;
- classify barriers by reason: texture upload, EDRAM resolve, feedback loop,
  render-target transition, present transition, or conservative safety;
- add a "barrier avoidable?" audit mode that records redundant or same-layout
  transitions;
- batch EDRAM resolves and texture uploads where correctness allows it;
- preserve exactness for games that need framebuffer feedback, but expose the
  cost per frame.

### 5. Treat Android frame pacing as a first-class renderer component

Android's Frame Pacing library exists because presenting as fast as possible can
stuff the SurfaceFlinger/display queue, adding stutter and latency. Its Vulkan
path can wrap `vkQueuePresentKHR` through `SwappyVk_queuePresent`, set the
`ANativeWindow`, and auto-adjust swap interval/pipeline mode.

For xenia-thor:

- do not judge FPS only from Android UI frame stats;
- add present-time and compositor queue diagnostics;
- evaluate AGDK Swappy for Vulkan present pacing once the game reaches a
  repeatable route;
- record whether a speed run is CPU-bound, GPU-bound, present-bound, or
  queue-stuffed.

### 6. Keep mobile descriptor work off hot paths

Arm's Vulkan guide says descriptor allocation and inefficient descriptor layouts
can increase CPU cost, and advises not allocating descriptor sets on
performance-critical code paths. It recommends packed layouts, reusing
preallocated descriptor sets, avoiding unnecessary updates, and considering
descriptor indexing/bindless approaches when available.

For Adreno/Thor, verify the same principle directly with AGI/Perfetto before
assuming Arm GPU behavior maps one-to-one, but the process is useful:

- count descriptor allocations and updates per frame;
- pool and recycle descriptor sets;
- avoid rebuilding descriptor state for identical texture/buffer bindings;
- prefer stable bind layouts for the Xenos translated pipeline.

## Proposed Xenia-Thor Vulkan Work Packages

1. `thor_vulkan_frame_report`: parse logcat/trace rows into per-frame CPU/GPU
   renderer counters: draws, submissions, barriers, render passes, readbacks,
   uploads, copies, pipeline creates, descriptor updates, present waits.

2. `arm64/vulkan speed capture mode`: add a Thor capture preset that keeps A64
   profiler off and instead records Vulkan/presenter counters plus final
   screenshots, APK SHA, cvars, and fatal-marker search.

3. `pipeline cache audit`: source-review current Xenia Vulkan pipeline cache
   behavior on Android, then add metadata in capture output for cold/warm
   pipeline count and `vkCreate*Pipeline` time.

4. `barrier/readback audit`: add default-off counters around Xenos resolve,
   texture cache, readback, and image transition paths. Rank by cost before
   changing synchronization behavior.

5. `Android frame pacing audit`: evaluate Swappy integration points for the
   Android Vulkan presenter, but only after CPU/JIT no longer dominates the
   route or when capture evidence shows present queue stuffing.

## Decision

Do not pivot the current Blue Dragon sprint away from A64 yet: the latest route
evidence still says CPU/JIT is the active wall. But do upgrade the Vulkan design
lane now:

- add PCSX2-style GPU counter categories to Thor reports;
- add pipeline-cache cold/warm metadata;
- add readback/barrier/upload/present diagnostics;
- use Android frame pacing only after present timing is measured;
- keep Vulkan changes evidence-gated by Thor captures, not desktop intuition.
