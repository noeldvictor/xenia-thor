# Thor paradigm candidates: ARM64, unified memory, the tiler, NEON, threads

Date: 2026-09-23. Five code surveys, one per axis of the paradigm matrix
(`docs/research/20260920-port-paradigm-matrix.md`). Each item names the desktop assumption, the
Thor-native change, and how to check it. The PC checks correctness; the Thor judges speed.
File references are from the survey reports; read the code again before a change.

## Done on 2026-09-23 (this commit)

| id | change | check |
|---|---|---|
| D1 | `platform.h` did not define `XE_COMPILER_HAS_GNU_EXTENSIONS` / `..._MSVC_EXTENSIONS` (lost in a merge). On the NDK's clang `XE_FORCEINLINE` was a plain `inline` (the kernel-export trampoline, `SpinLoopHint`), `XE_NOINLINE` was empty, and every `swcache` prefetch compiled to nothing. Restored. | Windows and Android build; the Android `.so` now has `prfm pstl1strm` (8) |
| D2 | `CommandProcessor::pending_fns_`: a `std::queue` filled from the UI thread without a lock. TSO hid it; on ARM the worker can see a half-built `std::function`. Now a mutex plus an atomic count for the worker's spin. | PC Banjo run |
| D3 | Zero-copy skipped the GPU-trace memory records (uploads and the buffer-watch skip). Traces taken with zero-copy on missed data. | build |
| D4 | `gpu_uma_direct_upload_barrier` (default true = unchanged): the direct path's HOST->read barrier after each upload ends the render pass (a GMEM store and reload each). By the Vulkan spec, host writes before `vkQueueSubmit` need no barrier. The direct path had GPU hangs in 2026-05, so the Thor decides. | device A/B |

## CPU: the ARM64 JIT (LLVM runs about 89% of guest execution on Android)

1. **Every LLVM guest call enters through `HostToGuestThunk`** (`llvm_backend.cc:290-339`,
   `a64_backend.cc:1301-1366`): 10 paired saves and restores and an unconditional `msr FPCR`
   per call, plus the write-back of every mirrored guest register. The code calls it the "~13%
   dispatch lever". First step, low risk: skip the `msr` when FPCR already holds the mode. Check:
   `testing/llvm_backend_test.cc` under qemu-aarch64, `tools/qemu/residency_abi_probe.c`.
2. **LLVM guest memory accesses are volatile; vector loads are four LDRs** (`cpu_llvm_vector_qload`
   off) because the MMIO fault decoder (`mmio_handler.cc:272`) accepts only single LDR/STR - an
   x64 decoder habit. Teach it LDP/STP/LDR Q (fixed-width encodings), then drop volatile. Check:
   a decoder unit test on the PC; one Thor launch.
3. **vmaddfp: about 190 functions fall back to a64**, whose VMX float ops stage operands the SSE
   way (moves, stores, a store-to-load forward). Levers exist, off. Check:
   `tools/qemu/vmx_nan_arith_differential.c`, `ppc/testing/instr_vmaddfp.s`.
4. **VMX pack/unpack and vrsqrte are C helper calls from LLVM code** (`llvm_backend.cc:444-767`):
   the vector goes through the stack and a switch on a compile-time constant. Inline IR, copying
   the a64 NEON sequences. Check: all 65,536 halves / all 2^32 inputs against the helpers on the PC.
5. **FPCR mode switches at VMX block edges** (MXCSR model). Restore FPU mode at block exit.
   Check: `a64_fpcr_switch_census`, `tools/qemu/fpcr_single_mode_equiv.c`.
6. **32-bit indirection table; LLVM functions are never installed**, so an a64 indirect call into
   LLVM code runs `ResolveFunction` each time. 64-bit entries, install LLVM entries.
7. **`GuestToHostThunk` saves all 28 Q registers + `msr FPCR` per host call**; `mftb` adds a
   global mutex and a `udiv`. Save live registers only; `mrs cntvct_el0` for mftb.
8. `dmb ish` before every lwarx - blocked until the reservation rework.
Not levers: the 7-GPR budget (0.1 spills per function, measured), eieio.

## Threads, memory model, OS

1. **One condition variable for every guest sync object** (`threading_posix.cc:462-519`): every
   `KeSetEvent` wakes every parked thread (the audio thread always sits in `WaitAny`). Banjo: 69%
   of a worker's kernel time is `pthread_cond_broadcast`. `threading_per_object_condvar` exists
   (ran on the Thor without a hang). Check: `base/testing/threading_test.cc` in WSL.
2. **Guest interrupt-disabled sections share the global lock with the GPU and memory code**
   (`ppc_frontend.cc:302`). A separate guest-interrupt lock; `global_lock_spin` default (A/B +3.5
   fps, not confirmed).
3. **Guest `Sleep(0)` becomes `sched_yield`** - Gears about 650,000 per second; Android timer
   slack makes short sleeps about 50 us. Futex park after a streak (`thor_sleep0_backoff_us`).
4. **A contended guest spinlock never spins** (`xboxkrnl_threading.cc:2043`);
   `rtl_critical_section_min_spin` default (A/B +5 fps, not confirmed).
5. **About 3,000 idle wakeups per second**: vsync 1 ms sleep loop, timer queue 1 ms with no
   timers, XMA 1 ms poll, command processor yields. Deadline sleeps and events.
6. **Write watches: one mprotect per run on 3 views**, VMA count against 65,530 not monitored, a
   failed mprotect only logged (a lost watch = a stale texture).
7. Thread placement (big/little, ADPF) - the Thor only.
8. `Semaphore::Release` / `Mutant::Release` read state outside the lock; allocations per wait.

## GPU: the tiler (Gears PC frame: breaks=26, resolve_copies=17, 49 MB resolved per frame)

1. **`vulkan_direct_host_resolve`** (exists, off): resolve from the render target straight to
   guest memory in one compute pass instead of two through the EDRAM buffer - about 2 x 49 MB per
   frame less traffic. Check: `tools/pc/trace_ab.py <gears trace> base
   "dhr:vulkan_direct_host_resolve=true"`.
2. **Resolve clears run as their own LOAD/STORE pass**; defer them to the next pass as loadOp
   CLEAR (keeps Turnip's LRZ). New code; medium risk.
3. **EDRAM ownership transfers as single-attachment passes** (29 calls per frame):
   `gpu_vulkan_inpass_edram_transfers` (off).
4. **Texture loads and uploads end the pass mid-frame** (`SubmitBarriers(true)`):
   `vulkan_hoist_shmem_uploads` (off); preload a pass's textures before BeginRenderPass.
5. **Every pass LOAD/STORE**: `gpu_vulkan_retro_depth_none` and relatives (off).
6. RT-as-texture (`gpu_rt_as_texture`, off) - high risk, no redraw check.
Caveat from the device record: the frame was ALU-bound and GMEM never beat Turnip's autotune;
the gain is per-pass fixed cost more than bytes.

## GPU: unified memory

1. The direct-upload barrier (D4 above).
2. **Smart sync uses one "last read" submission for the whole buffer**: stalls the CPU on the
   first upload of a submission and leaves later uploads unguarded. Per-page tracking (reuse the
   hazard check's `hazard_page_use_`).
3. **Texture loads untile into a scratch buffer, then copy to the image**; write the image
   directly through a storage view (UBWC risk on the A740 - per format).
4. Texture loads end the pass and serialize on one scratch buffer.
5. Readback helpers copy through a new buffer and drain the GPU (off the hot path).
6. Memory type is the first match; A/B cached-coherent against write-combined for the upload
   rings.

## Host SIMD / NEON (most hot paths are already native NEON; no sse2neon in `src/`)

1. LLVM pack/unpack and vrsqrte helpers (CPU item 4).
2. **Fetch-constant compare** (`vulkan_command_processor.cc:2112-2142`, on by default on
   Android): scalar, per draw. `vrev32q_u8` + `vceqq_u32`. Check: PC unit test, qemu.
3. `SwapDmaIndexBufferToNative` scalar (only with `gpu_hw_vertex_fetch`).
4. `RtlComputeCrc32` byte table -> `__crc32d` (the same polynomial). Load time.
5. XeCryptSha/AES in software -> ARMv8 crypto instructions. Load time.

## Next, in order (PC-verifiable first)

1. `vulkan_direct_host_resolve` trace A/B on a Gears frame (existing lever, image check only).
2. Skip the per-call `msr FPCR` when unchanged (qemu + LLVM backend test).
3. Fetch-constant compare in NEON (unit test + qemu).
4. `threading_per_object_condvar` behavior test in WSL, then default on Android.
5. Queue for the Thor: D4, the spin defaults, `thor_sleep0_backoff_us`, zero-copy per title.
