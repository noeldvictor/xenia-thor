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
- CPU ISA and memory model (closed): the `sched_yield` thread was `xeKeKfAcquireSpinLock`
  spinning on a lock word the a64 inline release had decremented instead of zeroed; the inline
  IRQL paths used a private word instead of the KPCR byte. Both inline paths now use the HLE's
  state and encoding. `docs/research/20260920-banjo-spinlock-protocol.md`.
- present (closed): the FPS badge counts Banjo's frames; the 0.0 was the stall.
- host SIMD: audit the sse2neon uses on the hot paths (texture conversion, resolves, swizzles).

## Where the axes met on 2026-09-21

- GPU driver (closed): the fork pushed both texture sets as push descriptors. A pipeline layout
  may hold one push descriptor set layout (VUID-VkPipelineLayoutCreateInfo-pSetLayouts-00293).
  NVIDIA and Turnip dropped the vertex set without a word; every vertex texture fetch read zero
  and Banjo's grass drew black (`61e192fd16`). The desktop assumption was "the driver would say
  so": no validation layer ran on the PC, and the device layer was never switched on. A spec
  rule without validation shows up as a silent driver difference, on both drivers. Rule: run the
  bundled validation layer on the device for every new GPU code path once, and read its lines.
- Xenos model and GPU architecture (open): the device's shadow atlas (a 1024x1024 k_24_8 depth
  target resolved every frame) accumulates casters across frames; the PC clears it each frame.
  A depth clear inside a render pass that a tiler executes per bin, or the ownership transfer of
  a depth image between differently pitched EDRAM-shaped images, keeps old depth on Adreno.
  Measured with `vulkan_trace_resolve_checksum` (`not_far24`): PC under 7000, device to 600274.
  Not the launcher toggles, not `TU_DEBUG=nolrz`. Next: `TU_DEBUG=sysmem` (tiler off) and the
  validation layer; then the render pass around the clear.
- A new column, game side: the recompilations hook the game at fixed addresses and ship the
  switches as options. The same addresses hold in the emulator, so a recomp's hook table is a
  ready list of guest patches (Blue Dragon from re:Blue on 2026-09-20, Banjo from reNut on
  2026-09-21: MSAA, motion blur, screen glow, shadows, contact shadows, LOD, particles). A guest
  patch moves the work out of the frame; an emulator lever only makes the work cheaper. Prefer
  the patch when the recomp shipped it as a user option. What does not port is the recomp's
  renderer: it hooks the D3D calls (`renut_gpu_funcs.toml`) and never emulates PM4 or EDRAM;
  that is the structural reason for its frame rate, and the BD native-HLE bricks were the fork's
  step in that direction.
- OS and scheduling: fourteen device launches in ninety minutes for an A/B series took the
  junction to 92 C and the title screen to 6 fps. The A/B harness must gate on the case
  temperature between runs (directive 8), or its numbers measure the throttle.

## Where the axes met on 2026-09-22

- CPU ISA, a new row inside it - two backends, one ABI: the a64 backend and the LLVM backend
  call each other's machine code raw (`blr x9` through the indirection table one way, a
  `musttail` jump through `xe_llvm_resolve_function` the other). The a64 code holds its
  backend context in x19, treats x22-x28 and the full q8-q15 as scratch, and expects x0 to
  carry the guest return address; LLVM code follows AAPCS64, which preserves x19-x28 and only
  the low halves of v8-v15. The contract was written in comments on one side only. The
  puzzle-transition freeze (a call through a garbage pointer, main thread or a worker) needs
  the LLVM backend and is not removed by reserving x19 or by the clobber barrier alone (each
  showed a 0-of-4 that a later batch contradicted). Open; `stall_study.py` and
  `llvm_bisect.py` are the instruments, eight launches per arm.
- OS and scheduling: the guest's interrupt-disabled sections (`mtmsrd` from r13, inside every
  360 spin lock) take xenia's process-wide recursive mutex, the one the kernel exports take.
  On the console the instruction masks interrupts on one hardware thread. The Banjo title
  profile: 21% of the CPU in the kernel, 3% in bionic's mutex slow path, the game's worker
  threads at 60% of a core each and the main thread and command processor below them; the
  GPU takes 1 to 13 ms of a 150 ms frame. Dropping the mutex for a per-thread depth
  (`cpu_global_lock_mutex=false`) livelocked two guest threads at 100%: the mutex was also
  what parked a waiter so the holder could run. Next: the same lever with the core router
  off (pinned threads sharing a core), and a spin-then-park mutex in place of the futex one.
- Measurement, all axes: a per-frame metric read across live cvar windows must first read the
  lever's current value (six no-ops read as six wins), a presented-fps badge quantized to
  swaps per half second hides a 20% gain, and a batch of four launches cannot separate a fix
  from luck on an intermittent freeze whose rate moved between 0 and 4 of 4 in one night.
- Memory manager, one instrument: an unhandled guest fault now fills the kernel trap record
  (`/trap`, `xenia_stall`), so a parked thread's registers, chain and the memory behind
  r24-r31 come in one call. The two freezes of the night were named that way in minutes.
