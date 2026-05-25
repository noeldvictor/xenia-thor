# Thor Hardware Acceleration Menu

Date: 2026-05-25 17:13 EDT

## Goal

Answer the current speed question directly: what hardware acceleration tricks
are actually plausible on AYN Thor / Snapdragon 8 Gen 2 / Adreno 740 for Blue
Dragon, and which ones are distractions given the measured slowdown?

## Sources Checked

- Arm C Language Extensions 2026Q1:
  https://arm-software.github.io/acle/main/acle.html
- Khronos Vulkan Guide, Subgroups:
  https://docs.vulkan.org/guide/latest/subgroups.html
- Khronos Vulkan Guide, Tile-Based Rendering best practices:
  https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html
- Khronos Vulkan tutorial, Compute Shader:
  https://github.khronos.org/Vulkan-Site/tutorial/latest/11_Compute_Shader.html
- Android GPU Inspector frame profiler:
  https://developer.android.com/agi/frame-trace/frame-profiler
- Qualcomm Snapdragon Profiler:
  https://www.qualcomm.com/developer/software/snapdragon-profiler
- Qualcomm Snapdragon Game Toolkit software page:
  https://www.qualcomm.com/developer/snapdragon-game-toolkit/software
- Qualcomm Snapdragon Game Super Resolution 2:
  https://www.qualcomm.com/developer/blog/2024/10/introducing-snapdragon-game-super-resolution-2
- Partial Cross-Compilation and Mixed Execution for Accelerating Dynamic Binary
  Translation:
  https://arxiv.org/abs/2512.00487
- Box64 v0.4.2 release note:
  https://box86.org/2026/04/new-box64-v0-4-2-released/
- Diya Joseph, `Improving Memory Access Efficiency for Real-time Rendering in
  Tile-based GPU Architectures`:
  https://people.ac.upc.edu/jmanel/papers/diyajoseph-phdthesis.pdf

All sources accessed 2026-05-25.

## Current Blue Dragon Wall

The latest Thor route-clean capture still does not show a GPU wall:

```text
82282490 body_ticks_total=29927708
82281D28 body_ticks_total=7155788
82287788 body_ticks_total=3162326
Main Thread  = 96.1%
GPU Commands = 11.5%
Draw Thread  = 3.8%
```

The current slowdown is A64 generated-code / JIT quality, especially hot PPC
state, VMX128, call-edge, and recursive body work. GPU work remains a parallel
measured lane, not the main fix until counters say otherwise.

## Thor CPU Feature Menu

ADB already confirmed this Thor reports `asimd`, `asimddp`, `i8mm`, `bf16`,
`asimdfhm`, `fcma`, `crc32`, `atomics`, and related features, with no `sve` or
`sve2`.

Useful hardware tricks:

- NEON / Advanced SIMD: best fit for exact VMX128 boolean ops, splats,
  permutes, shifts, compares, min/max, pack/unpack, byte swaps, vector
  load-shift joins, and bulk memory layout transforms.
- Dot product / I8MM: useful only for integer dot/sum-of-products shapes after
  proving signedness, lane grouping, endian order, saturation, and accumulation
  semantics. Do not apply to VMX FP32.
- CRC32: useful for hot checksum/hash paths, texture-cache keys, or guest code
  invalidation checks if profiling finds software CRC pressure. It is not a
  direct Blue Dragon JIT-body fix until measured.
- LSE atomics: useful for kernel locks, reservations, and dispatch structures
  if a source audit finds LL/SC or mutex overhead. Do not weaken guest-visible
  ordering.
- Barriers/ISB: Arm documents DMB/DSB/ISB for ordering and JIT code visibility.
  Use them precisely; do not remove external visibility, stack sync, or
  call-visible writebacks just because a local span is hot.
- Prefetch: consider only for predictable guest memory streams or texture
  staging copies after counters show cache-miss pressure.

Bad near-term assumptions:

- SVE/SVE2 is not available on the tested Thor.
- BF16/FHM/FCMA are not exact replacements for Xbox 360 VMX FP32.
- Fused FP shortcuts can be fast but remain risky for VMX NaN, denormal, FPCR,
  and result-order behavior.

## GPU / Adreno Menu

Use the GPU for Xenos-like bulk graphics work that is already GPU-resident or
can be batched. Do not move branchy PPC/JIT state traffic to Vulkan compute.

Good GPU-offload candidates:

- Xenos EDRAM resolves and format conversions.
- Texture deswizzle, endian conversion, tiled layout transforms, and large
  upload-time conversions.
- Vertex fetch unpack/format conversion if it can stay in GPU buffers.
- Clear/copy/resolve work that can use render-pass load/store ops or compute
  without readback.
- Optional resolution/upscale features later, using Qualcomm's GSR2 lesson:
  small, coherent shader passes can help rendering budget, but only after CPU
  full-speed is close enough that GPU resolution becomes meaningful.

Bad GPU-offload candidates:

- PPC basic blocks, guest state caching, fpscr writeback, call-edge state
  carriers, dynamic branches, and helper-heavy VMX semantics.
- Tiny unpredictable CPU-to-GPU jobs that require immediate CPU readback.
- Broad Vulkan rewrites without queue/present/barrier/upload/pipeline evidence.

## What Mature Emulator / DBT Work Suggests

- Box64's Dynarec and AutoCRC lesson maps to robust invalidation and safe hot
  native blocks, not copied code. For xenia-thor, that means edge/function
  variants, feature gates, normal-entry fallback, and counters.
- Partial cross-compilation maps to selective hot edge/function offload with
  explicit calling channels. For Blue Dragon, this points at
  `82282490:82282598 -> 82287788` caller-local or side-table payload storage.
- Mobile tile-based rendering work reinforces bandwidth/locality discipline:
  fewer round trips, coherent batches, good render-pass load/store intent, and
  GPU profiling before changing renderer behavior.
- AGI and Snapdragon Profiler are the right external validation tools when the
  GPU lane becomes suspect because they expose Vulkan calls, memory, GPU
  performance data, counters, and low-level workloads.

## Prioritized Experiments

1. CPU/JIT first: implement a default-off counter-only caller-local or
   side-table edge-variant payload probe for `82282490:82282598 -> 82287788`.
   This follows the actual current wall.
2. VMX128 hardware audit: rank hot Blue Dragon VMX128 opcodes/spans by exact
   NEON suitability, especially boolean/permutation/load-shift and integer
   dot-product-only candidates. Keep FP fused forms behind strict semantic
   proof.
3. GPU work-to-GPU audit: add or run counters that separate CPU-side graphics
   work from true GPU execution: texture conversion/deswizzle bytes, resolve
   bytes, readback bytes, staging-copy bytes, render-pass load/store choices,
   barrier counts, queue waits, and pipeline creation/cache misses.
4. If the GPU audit finds a bulk CPU graphics transform, add one default-off
   Vulkan compute or render-pass path for that exact task, with no immediate
   CPU readback and route-clean Thor proof.
5. Use AGI or Snapdragon Profiler only once our internal counters say GPU
   Commands, queue/present stalls, barriers, uploads, or resolves are large
   enough to matter.

## Decision

The hardware plan is not "make the GPU emulate Xenon." The plan is:

- keep Blue Dragon mainline speed work on A64 generated-code quality;
- use Thor's NEON/dot/I8MM/CRC/LSE features only where opcode semantics and
  counters justify them;
- move Xenos-native bulk graphics transforms to Adreno only after counters show
  a concrete CPU-side graphics cost; and
- keep all risky experiments default-off, title/function/PC-gated, and
  route-proven on Thor.
