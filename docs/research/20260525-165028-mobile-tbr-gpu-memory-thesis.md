# Mobile TBR GPU Memory Locality Thesis

Date: 2026-05-25 16:50 EDT

## Source Checked

- Diya Joseph, `Improving Memory Access Efficiency for Real-time Rendering in
  Tile-based GPU Architectures`, PhD thesis, Universitat Politecnica de
  Catalunya, April 2024:
  https://people.ac.upc.edu/jmanel/papers/diyajoseph-phdthesis.pdf

## Relevant Claims

The thesis is about mobile tile-based GPU memory efficiency, not binary
translation or CPU JIT quality. It is still useful for the xenia-thor Vulkan
lane because Thor is a mobile SoC target and Xenia has to map Xbox 360/Xenos
rendering patterns onto an Android Vulkan stack.

Important ideas:

- Tile-based rendering improves locality by keeping tile work close to on-chip
  storage and reducing external-memory traffic. This supports our existing
  Vulkan counter direction: measure resolves, render-pass churn, barriers,
  uploads/copies, readbacks, queue waits, and present waits before changing
  renderer behavior.
- TCOR targets tiling-engine / tile-cache efficiency. For xenia-thor this maps
  to measuring binning-style pressure and unnecessary render-pass or resolve
  churn, not to rewriting Xenos geometry handling from a hardware thesis.
- DTexL targets texture-cache locality through warp scheduling and locality
  grouping. For xenia-thor this maps to texture upload, guest texture layout,
  descriptor/cache reuse, and shader sampling locality diagnostics.
- Boustrophedonic Frames targets inter-frame texture reuse. For xenia-thor this
  suggests frame-to-frame cache/reuse counters and avoiding needless texture or
  framebuffer invalidation between adjacent frames.
- WaSP targets memory-latency hiding with warp scheduling. For xenia-thor this
  is mostly a driver/hardware idea, but it reinforces that tiny, unpredictable
  GPU work can be bad. Prefer fewer, larger, coherent GPU batches if counters
  ever prove the GPU path is the wall.

## Xenia-Thor Decision

Do not pivot Blue Dragon to broad Vulkan work from this source alone. The
latest Thor captures still show Main Thread / A64 generated-code pressure as
the wall, with GPU Commands below one full core. This thesis becomes a
GPU-memory-locality checklist for later captures, not permission to move CPU
JIT work to GPU compute.

When a future capture shows GPU Commands, present waits, queue submits,
pipeline creation, barriers, resolves, readbacks, uploads, or texture-cache
traffic overtaking the CPU/JIT wall, the next Vulkan investigation should add
or inspect counters for:

- render-pass count and load/store op behavior;
- color/depth resolve count and bytes;
- texture upload/copy bytes and reuse;
- framebuffer invalidation and layout transitions;
- readback/synchronization stalls;
- descriptor/pipeline churn;
- frame-to-frame resource reuse;
- Adreno/AGI bandwidth and cache counters if available.

Until that happens, the thesis reinforces a conservative rule: keep GPU work
coherent and measured, and keep the active Blue Dragon speed lane on A64
guest-state/register-cache, VMX128 lowering, and edge/function variants.

