# Unified memory, zero-copy: guest physical RAM and the GPU buffer as one allocation

Design note, 2026-09-23. User: "get UMA working? unified memory". Nothing here is built or
measured yet. Each stage below ends with one short device check (ask the user first; heat stop).

## What exists (measured)

- `gpu_uma_direct_shared_memory` (on by default on Android since 2026-05): the shared-memory GPU
  buffer is a 512 MB `HOST_VISIBLE | DEVICE_LOCAL` allocation. A guest page the GPU needs is copied
  with `memcpy` straight into it: no staging buffer, no `vkCmdCopyBuffer`, no transfer barrier.
  Measured 2026-05-29 (Gears 3 menu): about 0.44 MB copied per frame, 11 transfer copies removed.
- `gpu_uma_smart_sync`: before a direct write, wait for the last closed submission that read (or
  wrote) the buffer - the fix for a GPU hang where a CPU write overwrote pages a deferred (tiler)
  draw still had to read.
- Per page the shared memory keeps a valid bit and a write watch: the page is write-protected with
  `mprotect` in the physical views; a guest write faults, the handler clears the valid bit, the
  next `RequestRange` copies the page again and protects it again.
- `gpu_shared_memory_stats` (2026-09-22): per frame, requests, lock-free answers, uploads, KB, copy
  and watch time, lock wait, watch hits. Gears of War steady state on the PC: 2,573 requests, 20
  uploads, 156 KB, 5 watch hits per frame. The device's 30 ms per frame in residency was waiting
  for the global lock, not copying (fixed: the lock-free valid check, no per-draw lock).

## What zero-copy adds

One allocation is both the guest's physical RAM and the Vulkan buffer the GPU reads:

- no copy of guest pages into the GPU buffer, and no write watch for pages that only buffers use;
- GPU writes (resolves, memexport) land in guest RAM directly, so the CPU sees them without a
  readback - xenia today keeps GPU-written data only in the GPU buffer;
- 512 MB of RAM saved (the separate shared-memory buffer goes away).

What it does NOT fix: Gears' measured cost. Its steady-state copy and watch are small (156 KB and 5
hits per frame). Zero-copy pays off for titles with heavy per-frame uploads or GPU readbacks - read
a title's `GPU shmem/frame` line before choosing it as the test title.

## How (Android)

Guest memory is one file mapping of 0x11FFFFFFF bytes (`Memory::Initialize`): 4 GB of virtual
space, then the 512 MB of physical memory at file offset 0x100000000. The physical part is mapped
four times (`map_info` in memory.cc): 0xA0000000 (64 KB pages), 0xC0000000 (16 MB pages),
0xE0000000 (4 KB pages, +0x1000), and the raw physical base (`physical_membase_`).

1. Allocate the physical 512 MB as an `AHardwareBuffer` (format BLOB, width 0x20000000, usage
   `CPU_READ_OFTEN | CPU_WRITE_OFTEN | GPU_DATA_BUFFER`).
2. Get its dma-buf fd with public NDK calls only: `AHardwareBuffer_sendHandleToUnixSocket` over a
   `socketpair`, then `recvmsg` with `SCM_RIGHTS` (the native handle's first fd is the dma-buf).
3. `MapViews`: every view whose file target is in the physical range maps the dma-buf fd at
   (target - 0x100000000) instead of `mapping_`. `mmap` of a system-heap dma-buf with
   `MAP_SHARED | MAP_FIXED` gives a cached CPU mapping.
4. `VulkanSharedMemory`: create `buffer_` with `VkExternalMemoryBufferCreateInfo` (handle type
   AHB), read `vkGetAndroidHardwareBufferPropertiesANDROID` for the memory type bits, import with
   `VkImportAndroidHardwareBufferInfoANDROID` + `VkMemoryDedicatedAllocateInfo`, bind. Turnip on the
   Thor exposes `VK_ANDROID_external_memory_android_hardware_buffer` and
   `VK_EXT_external_memory_dma_buf` (device log, 2026-09-21).
5. `UploadRanges` for this buffer copies nothing (the pages are the same memory); it only marks
   them valid. Texture requests keep the watch (the texture cache needs invalidation); a later
   stage drops the watch for pages that only vertex, index and memexport buffers use.

## Hazards

- **Coherence.** A CPU write sits in the CPU cache until written back. If the imported memory type
  is not `HOST_COHERENT` (the Adreno 740 snoops cached memory only when KGSL maps it IO-coherent),
  every submission needs a cache clean of the written ranges (`DMA_BUF_IOCTL_SYNC`), which costs
  what the copy cost. Stage 0 reads the memory type flags; stage 2 checks pixels.
- **Timing.** xenia's command processor runs ahead of the host GPU and writes the guest's GPU
  fences when it processes them, not when the Adreno finishes. With a copy, the data of a draw is
  a snapshot taken at processing time. With zero-copy, a game that reuses a vertex buffer after
  its fence can overwrite data the Adreno has not read yet: flickering or torn geometry. The fix, if
  it shows: write the guest-visible fence values (the EVENT_WRITE / interrupt the game waits on)
  only when the host submission that contains them completes - the real hardware order. That can
  cost CPU/GPU overlap; measure it.
- **Size.** A 512 MB BLOB AHardwareBuffer may exceed the gralloc limit; stage 1 finds out. The
  fallback is a dma-buf from Vulkan export (`VkExportMemoryAllocateInfo`, handle type dma-buf) if
  Turnip on KGSL exports it.

## Path taken (2026-09-23): host-pointer import through our Turnip

The PC path (VK_EXT_external_memory_host) worked first, so the Android path uses the same import
and needs no change to the guest memory allocation. Upstream Turnip does not have the extension.
Our patch `tools/turnip/patches/0001-tu-kgsl-external-memory-host.patch` adds it:
`IOCTL_KGSL_GPUOBJ_IMPORT` with `KGSL_USER_MEM_TYPE_ADDR` (the length goes in `priv_len`), flags
`KGSL_MEMFLAGS_IOCOHERENT` and write-back cache mode, only for the cached-coherent memory types, so
the coherence hazard above does not apply. The kernel pins every page of the range, and a pinned
page must be read-write, so the app imports `Memory::GetPhysicalAlias()`: a separate read-write
map of the physical 512 MB of the guest memory file, apart from the guest views (reserved pages,
write watches). The AHB stages below stay as the fallback if KGSL refuses the userptr import.

Device check (ask the user first): `driver_ab.py gears1` with an arm
`zc:zip=...-xe2.zip,cvars=gpu_uma_zero_copy=true`. Pass: the row says `uma: ACTIVE`, the image is
the same, fps is not lower. Then the timing hazard is the thing to watch.

Seen on the PC (2026-09-23): MagnaCarta 2, one cutscene close-up, both zero-copy frames caught are
cut across the full width at a row that moves; the copy path's frame of the same shot is clean.
This is the first sign of the timing hazard above. If the device shows it too, stage 3 (guest
fences on host completion) comes before any default-on.

## Stages (each: one build, one short device check)

| stage | change | device check | pass |
|---|---|---|---|
| 0 | log the AHB import properties (memory type bits and flags) for a 512 MB BLOB at startup, cvar off by default | one launch to the title, read the log | the type is DEVICE_LOCAL; note HOST_COHERENT |
| 1 | back the physical views with the AHB dma-buf (`gpu_uma_zero_copy=1`), keep the separate GPU buffer and the copies | boot a title to its first scene, scoreboard row | same fps and image as off |
| 2 | import the AHB as the GPU buffer, copy nothing, keep all watches | scoreboard rows, `GPU shmem/frame` (upload_kb near 0), trace A/B of one frame on the PC for the reference image | same image; upload time gone |
| 3 | guest fences on host completion, only if stage 2 shows corrupted geometry | the same rows | the image is correct |
| 4 | no watch for buffer-only pages (built 2026-09-23: `RequestBufferRange`, `gpu_uma_skip_buffer_watches`; PC Gears 0 uploads, 0 invalidations, same image) | `invalidations` near 0 | same image |

Start with the title whose `GPU shmem/frame` shows the most upload KB; run stage 0 and 1 in one
device session.
