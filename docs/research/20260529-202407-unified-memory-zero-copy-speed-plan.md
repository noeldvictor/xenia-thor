# Unified-Memory Zero-Copy Speed Plan (Adreno 740 / SD8 Gen2)

## Status

Design + code-reading note. **No device proof yet.** Every speed claim below is a
hypothesis until built on `arm64-v8a` and measured on AYN Thor `c3ca0370` with a
before/after capture. Authored from the sandbox, which cannot build, deploy, or
run ADB; the build/deploy/measure steps are owner/automation actions.

## User goal

"RAM on AYN Thor has CPU/GPU in the same place, so we could get interesting
techniques to boost speed." This is correct, and the current code is leaving that
exact win on the table.

## What the SoC actually is

AYN Thor / Thor Max is a **unified memory architecture (UMA)** integrated GPU
(Snapdragon 8 Gen 2 class, Adreno 740). On UMA, the Vulkan driver advertises a
memory type that is simultaneously `DEVICE_LOCAL` **and** `HOST_VISIBLE`. The CPU
can write it and the GPU can read it with no transfer. Khronos and AMD both call
this out as the preferred resource path on mobile/integrated GPUs: "no need to
create a staging buffer and the transfer overhead is greatly reduced... a memory
type may be available that is both HOST_VISIBLE and DEVICE_LOCAL, and it is likely
the best choice." (Vulkan Guide / VMA usage patterns.)

Adreno is also **tile-based deferred** (TBDR): per-tile color/depth lives in
on-chip GMEM, which is far cheaper than system RAM. Bandwidth, not raw ALU, is the
usual mobile bottleneck — so cutting copies and cutting attachment load/store
traffic is where speed comes from.

## The waste in the current code

`src/xenia/gpu/vulkan/vulkan_shared_memory.cc`:

1. The guest shared-memory GPU buffer `buffer_` is allocated **device-local only**.
   It selects the memory type with
   `buffer_memory_requirements.memoryTypeBits & vulkan_device->memory_types().device_local`
   (lines ~78 and ~131). `host_visible` is never considered.
2. `UploadRanges()` (lines ~375-450) therefore does a **double copy** for every
   dirty guest page range:
   - `std::memcpy` from guest physical RAM into a `HOST_VISIBLE` staging page from
     `upload_buffer_pool_`, then
   - `command_buffer.CmdVkCopyBuffer(...)` from staging into the device-local
     `buffer_`, gated behind a `SubmitBarriers` ordering barrier.

On a discrete GPU that staging dance is mandatory. On this UMA SoC it is pure
overhead: a redundant memcpy, a transfer-queue copy, a pipeline barrier, and 2x
the memory footprint for the shared-memory mirror — paid on every CPU write the
guest makes to GPU-visible RAM. Burnout and Banjo both push a lot of guest memory
traffic, so this tax is on the hot path.

The infrastructure to fix it already exists: `VulkanDevice::MemoryTypes` already
exposes `device_local`, `host_visible`, `host_coherent`, `host_cached` bitmasks
(`src/xenia/ui/vulkan/vulkan_device.h:264`). We just never ask for the
intersection.

## Three techniques, ranked by risk

### 1. Direct-write shared memory (UMA buffer) — recommended first slice

When `memory_types().device_local & memory_types().host_visible` is non-empty
**and** the device is integrated, allocate `buffer_` from that intersected type and
persistently map it. Then `UploadRanges()` becomes a single `memcpy` straight into
the mapped GPU buffer — delete the staging `RequestPartial`, the `CmdVkCopyBuffer`,
and the transfer barrier. Keep the existing staging path as the fallback for any
device where the intersection is empty.

- Win: removes one full copy + a barrier per upload, halves the shared-memory
  mirror footprint.
- Risk: low-moderate. Needs a `HOST_COHERENT` type or explicit
  `vkFlushMappedMemoryRanges` after each write; must respect
  `nonCoherentAtomSize`. Gate behind a cvar (e.g. `gpu_uma_direct_shared_memory`,
  default off) so it can be A/B'd against the staging path on-device.

### 2. True zero-copy via `VK_EXT_external_memory_host` — higher payoff, higher risk

Xenia's guest physical memory is already a host allocation. With
`VK_EXT_external_memory_host` we can **import that pointer directly** as the
backing memory of `buffer_` (`VkImportMemoryHostPointerInfoEXT`), so the guest's
own RAM *is* the GPU buffer — zero copies, even the memcpy disappears.

- Constraints from the spec/registry: import pointer and size must be multiples of
  `minImportedHostPointerAlignment`; the same host allocation generally can't be
  imported twice on one physical device; app owns CPU/GPU sync. Khronos explicitly
  names emulators as the use case but warns it "may have significant performance
  costs" if forced onto every allocation, so scope it to the shared-memory mirror
  only.
- Action: probe `VK_EXT_external_memory_host` support on the Thor's driver first
  (it's a Qualcomm/Turnip-dependent extension). If present, prototype behind its
  own cvar after technique 1 is proven.

### 3. Tile-friendly render passes (attacks bandwidth, helps black-frame games too)

Independent of uploads: make sure render passes use `LOAD_OP_CLEAR`/
`LOAD_OP_DONT_CARE` and `STORE_OP_DONT_CARE` wherever the guest doesn't need the
prior/!future contents, and use `LAZILY_ALLOCATED` transient attachments for
depth/MSAA so they live in GMEM and never touch system RAM. On Adreno TBDR,
needless `LOAD`/`STORE` forces full-framebuffer GMEM<->sysmem resolves every pass —
the single biggest mobile bandwidth sink. Audit `vulkan_render_target_cache.cc`
load/store ops as a follow-up slice.

## How this connects to Burnout and Banjo

Neither is a crash anymore (see notes 20260528-192000 ... 20260529-091500):

- **Burnout (454107DC):** XAM-keyboard JNI fix reaches the World Tour menu. Live
  failure is a **no-present stall** — guest `VdSwap` stops advancing (FPS OSD
  correctly reads 0), or a static-frontend stall. That's a guest-execution / wait
  / render-target question, not primarily an upload-speed bug. Speed work won't
  "fix" it; the next slice is still A64 thread snapshots + `XboxkrnlThreadWaitTrace`
  + a capture across the transition into no-present.
- **Banjo N&B (4D5307ED):** audio + continuing `VdSwap` but black frame. This is a
  **render-target / frontbuffer** class, and technique 3 (and a RT/frontbuffer
  probe) is the relevant lane, not the upload path.

So: the UMA upload work (techniques 1-2) is the real lever for the general
"faster" goal and should be measured on a title that is already presenting and
progressing. Burnout/Banjo need their own presentation/guest-progress probes
before any "full speed" or "loading" claim.

## Proposed next slices (each ends in a device capture)

1. Add `gpu_uma_direct_shared_memory` cvar + UMA buffer path in
   `vulkan_shared_memory.cc` (technique 1). Build NativeCore + ApkShellDeploy,
   capture a fixed route on a presenting title with FPS/frame-time before/after.
2. If win confirmed, probe + prototype `VK_EXT_external_memory_host` (technique 2).
3. Separate slice: audit render-pass load/store + transient attachments
   (technique 3); also serves the Banjo black-frame lane.
4. Burnout/Banjo: guest-progress + RT/frontbuffer probes per existing notes,
   unrelated to the speed cvars.

## Sources

- Vulkan Guide — Memory Allocation (UMA = DEVICE_LOCAL | HOST_VISIBLE):
  https://docs.vulkan.org/guide/latest/memory_allocation.html
- VMA recommended usage patterns (host-visible+device-local best for UMA upload):
  https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html
- AMD GPUOpen — Using Vulkan Device Memory:
  https://gpuopen.com/learn/vulkan-device-memory/
- VK_EXT_external_memory_host (host pointer import / emulator use + caveats):
  https://registry.khronos.org/vulkan/specs/latest/man/html/VK_EXT_external_memory_host.html
- Turnip / Adreno TBDR + GMEM tiling (bandwidth, load/store cost):
  https://deepwiki.com/bminor/mesa-mesa/2.5-turnip-(qualcomm-vulkan-driver)
- Qualcomm Adreno tiling best practices:
  https://developer.qualcomm.com/sites/default/files/docs/adreno-gpu/developer-guide/gpu/best_practices_tiling.html
