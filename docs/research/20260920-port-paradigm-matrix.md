# The port crosses several paradigms at once: a matrix for finding the mismatches

Date: 2026-09-20. User idea: xenia was written for x86-64 desktops with a desktop GPU and
Windows. The port to the AYN Thor changes several paradigms at the same time, and most bugs and
most lost performance come from code that still assumes the old paradigm on one axis while the
others changed. The method: name the axes, list the assumption on each, and for each bug ask
which axis it crosses before touching code.

## The axes

| axis | desktop x86 assumption in xenia | the Thor | what breaks when the assumption stays |
|---|---|---|---|
| CPU ISA | x86-64: two-operand destructive forms, 16 GPRs, 16 XMM, AVX2 256-bit | AArch64: three-operand, 31 GPRs, 32 V registers of 128 bits, NEON only | The a64 backend was derived from the x64 backend: 7 allocatable GPRs, spills to `PPCContext`, x86-shaped sequences (directive 1) |
| memory model | TSO: stores visible in order, few fences needed | weak ordering: loads and stores reorder unless fenced | atomics that guard plain buffers race; "the x86 to ARM64 sweep: memory ordering is the bug class" |
| host SIMD | SSE/AVX intrinsics in host code (byte swaps, texture conversion, resolves, hashing) | NEON, often through sse2neon shims | shims are slower and differ on denormals, rounding, saturation; the PPC VMX to NEON lowering decides guest FP correctness |
| GPU architecture | immediate-mode dGPU, dedicated VRAM, PCIe copies, cheap memory bandwidth, expensive readback | Adreno 740: binning tiler with GMEM, UMA, expensive render-pass breaks and barriers, cheap host-visible memory | EDRAM emulated as one big buffer with compute resolves; each resolve or barrier breaks a tile pass; "the frame is ALU-bound, blend is free, GMEM never beats autotune" |
| GPU driver | NVIDIA and AMD Vulkan, forgiving, complete | Mesa Turnip: strict, some features emulated, newer every week; the Qualcomm driver is wrong for this work | validation errors that never fired on desktop; a bare launch runs the wrong driver |
| Xenos model | the 360 is itself a tiler with 10 MB EDRAM and predicated tiling; xenia flattens it for a desktop GPU | Adreno's GMEM is the natural analogue of EDRAM | half a frame missing when a tile pass is not resolved (Banjo, 2026-09-20); Blue Dragon's 734-draw pass runs once per tile |
| OS and scheduling | Windows: fibers, thread priorities, ms timers, one homogeneous CPU | Android: SCHED_OTHER, cgroups, 1+4+3 heterogeneous cores, thermal throttling near 95 C junction | the main guest thread pinned to a little core once; yield loops burn a core and heat; the compile needs a thermal governor |
| memory manager | Windows VirtualAlloc, MAP_FIXED reservations, no map-count limit | Scudo, `vm.max_map_count` 65,530, no root | two VMAs per JIT function killed titles above 32,000 functions |
| storage | NVMe, mmap of the ISO, a writable install directory | flash, content URIs, the app's private dir, a read-only disc image | Banjo's XUI font cache on the read-only disc; the object cache path |
| present | exclusive fullscreen, 60 to 144 Hz, vsync from the driver | SurfaceFlinger, BufferQueue, a panel that sleeps, frame pacing by the compositor | a sleeping panel reads as a render bug; the FPS badge counts `VdSwap` only (Banjo presents another way and reads 0.0) |
| input and audio | XInput, WASAPI | Android gamepad events, AAudio | the debug gamepad broadcast; the ADPF hint session that holds one thread |
| build | MSVC and clang-cl, x64 only | NDK clang, ARM64; an x86_64 ABI that never shipped doubled the builds | UB that MSVC hid; per-file RTTI flags the NDK generator cannot express |

## How to use the matrix

1. For a bug, write the axis or axes it crosses before any code. A half-black frame is the Xenos
   model and GPU architecture axes. A stall with one thread in `sched_yield` is the OS axis. A
   wrong pixel in a VMX-heavy title is the host SIMD axis.
2. Look for the desktop assumption on that axis in the code path, not for a local fix. The
   reference implementations for the target paradigm: XenDroid and xenia-edge (a64 on Adreno),
   re:Blue and ReXGlue (native recompilation; their renderer is a per-game HLE, archived here),
   FEX and Box64 (x86 on ARM64 JITs: ABI, memory ordering), RPCS3 and whatcookie (ARM64 tuning on
   this SoC), Mesa Turnip itself (what the driver does with a render pass).
3. Prefer the change that moves the code to the target paradigm over the change that patches the
   symptom, when the size is similar. Record either way in the ledger.

## Where the axes already met today (2026-09-20)

- memory manager: JIT code in 64 MB slabs, one VMA per slab (fixed).
- OS and scheduling: the LLVM compile serialized behind one lock ran on one core; six JIT units
  compile in parallel; a thermal governor holds them back at 96/100/105 C junction and 47/50 C
  case. Banjo cold: 711 s to 327 s.
- Xenos model: Blue Dragon's own FSAA flag off ends predicated tiling: 9.9 to 15.8 fps.
- present: the FPS badge misses Banjo's present path; the stall detector in `title_probe.py`
  was fooled by it.
- CPU ISA and memory model: the null read at guest 0x24 in Banjo was a console memory-map
  assumption (the readable zero page), now a toggle.

## Open items on the matrix

- Xenos model: Banjo's lower half of the frame black with speckles in the game world. Next: a
  frame capture on the PC trace loop, then the tile and resolve path for its render target
  layout.
- OS: a guest or host thread at 69 % CPU in `sched_yield` during Banjo gameplay. Next:
  `xenia_profile(callgraph=True)` to name the caller; a sleep or a real wait instead of a yield.
- present: count every present path in the FPS badge, not only `VdSwap`.
- host SIMD: audit the sse2neon uses on the hot paths (texture conversion, resolves, swizzles).
