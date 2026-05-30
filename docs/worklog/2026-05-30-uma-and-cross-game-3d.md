# Worklog 2026-05-30 — UMA fence/TDR + cross-game black-3D

Running worklog. Append entries; commit+push often.

## Goal
1. UMA experiment (a): isolate whether the Adreno GPU TDR is caused by the buffer
   being NON-SPARSE vs HOST-VISIBLE-DEVICE-LOCAL.
2. Cross-game black-3D + low-fps fix: in-game 3D world not reaching frontbuffer
   (Blue Dragon ~2fps flat-blue + HUD; Burnout in-race black). Suspect MSAA
   render-target resolve / EDRAM->frontbuffer path on Adreno.

## State at start (HEAD 8a5dfd8fa)
- UMA hang = real Adreno TDR, ~50%, frozen VdSwap ~1200. Ruled out: coherency,
  budget, CPU-write-vs-deferred-read race (serialize experiment refuted it),
  maxStorageBufferRange. Remaining structural diff: staging buffer is SPARSE,
  UMA buffer forced NON-SPARSE host-visible-device-local.
- Stable speed path = UMA off, ~60fps to menus.

## Entries

### E1 (start) — expose vulkan_sparse_shared_memory to launch intent
The sparse path is gated by `vulkan_sparse_shared_memory && !gpu_uma_direct_shared_memory`
(vulkan_shared_memory.cc:94). So experiment (a) variant "non-sparse + device-local +
staging copy" = launch with gpu_uma_direct_shared_memory=false AND
vulkan_sparse_shared_memory=false. That cvar wasn't in the Android allow-list; added
it (EmulatorActivity.java). Needs a build. Test matrix once built:
- baseline sparse staging: uma=false, sparse=true (default) -> expect STABLE (known)
- NON-SPARSE staging:      uma=false, sparse=false        -> if it TDRs, NON-SPARSE
  alone is the trigger; if stable, host-visible-device-local memory is the trigger.
- UMA direct (ref):        uma=true                       -> ~50% TDR (known)

### E2 — Experiment (a) RESULT: non-sparse is NOT the trigger; host-visible-device-local IS
Ran NON-SPARSE + DEVICE_LOCAL + staging copy (uma=false, sparse=false) x5 on Burnout:
ALL 5 RUNNING ~61fps, zero hangs (nonsparse_msg=1 confirms the non-sparse buffer was
actually used). 
=> The single 512MB NON-SPARSE allocation is fine. The TDR is NOT caused by
non-sparse, NOT by buffer size, NOT by the storage-buffer split.
=> The ONLY remaining difference vs UMA-direct (~50% TDR) is the MEMORY TYPE:
HOST_VISIBLE|DEVICE_LOCAL + persistent CPU mapping + CPU writes that the GPU then
reads. That is now the pinned cause class.
Combined with E (serialize) refuting the timing race, the leading mechanism is:
Adreno GPU reads from HOST_VISIBLE|DEVICE_LOCAL memory have a coherency/visibility
requirement our barrier+flush isn't satisfying for the GPU's MMU/cache, intermittently
faulting. Even though the type reports HOST_COHERENT, Adreno's GPU-side cache view of
host-written pages in this heap may need an explicit invalidate/availability op the
desktop path never needed.
NEXT (experiment b): keep UMA buffer host-visible BUT strengthen the host->device
visibility: try (b1) a full vkDeviceWaitIdle is overkill - instead (b2) widen the
host-write barrier to srcStage=HOST + a VK_ACCESS_MEMORY_READ over the WHOLE buffer
with dstStage=ALL_COMMANDS, and (b3) test whether a non-coherent host-cached type +
explicit vkFlushMappedMemoryRanges behaves differently than the host-coherent type.

## Cross-game black-3D investigation (Blue Dragon, UMA off)

### E3 — render target PATH = fbo (kHostRenderTargets), NOT fsi
Device log: "VulkanRenderTargetCache: render_target_path_vulkan='' selected=fbo".
So 3D is drawn into host framebuffer images; bug is in EDRAM-RT -> sampleable
texture/frontbuffer resolve on the fbo path (matches upstream notes: fbo path has
limited pixel-format support; "couldn't see the world" w/ EDRAM sharing).

### E4 — UNSUPPORTED FORMAT lead (concrete, cross-game candidate)
Device log at GPU init:
  "VulkanTextureCache: Format k_2_10_10_10 (signed) is not supported by the device
   (preferred Vulkan format is 65)"
  "... k_2_10_10_10_AS_16_16_16_16 (signed) is not supported ..."
k_2_10_10_10 is a VERY common Xbox360 color/RT/vertex format. In-game Blue Dragon
renders into 320x8192 / 160x4096 MSAA color+depth RTs (EDRAM-tiled layouts) with
"guest format 0" and "guest format 3". If the world is rendered into a
k_2_10_10_10 target the Adreno can't represent, the resolve/sample yields nothing
=> black 3D while the simpler-format HUD shows. Need to verify how the code handles
an unsupported RT format (proper fallback vs silent drop). Vulkan format 65 =
VK_FORMAT_A2B10G10R10_UNORM_PACK32 (unsigned) is the preferred substitute; the
SIGNED 2_10_10_10 has no direct unorm equivalent -> likely the gap.
NEXT: read VulkanTextureCache / render-target format fallback for signed 2_10_10_10.

### E6 — Experiment (b): gpu_uma_strong_coherency (maximal host->device coherency)
Built a cvar that, on the UMA direct path: (1) ALWAYS vkFlushMappedMemoryRanges even
when the heap is HOST_COHERENT (Adreno host-visible-device-local heaps are often
write-combining; the coherent flag may not fully cover the GPU-side view), and (2)
widens the host-write->guest-read barrier to the WHOLE buffer with
ALL_COMMANDS/MEMORY_READ (in case the span-bounded HOST->VERTEX_INPUT|shader barrier
under-covers what the deferred tiler reads). Added to EmulatorActivity allow-list.
Test plan: launch Burnout uma=true + gpu_uma_strong_coherency=true xN; if the ~50%
TDR disappears -> coherency confirmed, then narrow (flush-only vs barrier-only) to
the minimal fix and consider making it the default for host-visible buffers.

RESULT batch1 (6 runs): 5 RUNNING / 1 HUNG - looked promising but was a lucky streak.
RESULT batch2 (8 runs): 4 RUN / 4 HUNG (runs 9,10,13,14 hung @1190-1197).
COMBINED: 9 RUNNING / 5 HUNG of 14 = ~36% hang. Statistically indistinguishable
from the ~50% baseline. => EXPERIMENT (b) REFUTED. Maximal host->device coherency
(whole-buffer flush even when HOST_COHERENT + ALL_COMMANDS/MEMORY_READ barrier over
the whole buffer) does NOT fix the TDR. So the fault is NOT a host-write
coherency/visibility gap either.

## Elimination summary (what the UMA TDR is NOT)
Proven on device this session:
- NOT a prior-submission CPU-write-vs-deferred-read race (serialize: still hung)
- NOT non-sparse / buffer size / storage-buffer split (exp a: device-local
  non-sparse staging = 5/5 stable)
- NOT a host-write coherency/visibility gap (exp b: maximal flush+barrier = still
  ~36% hung)
- IS specific to HOST_VISIBLE|DEVICE_LOCAL memory (exp a: only the memory type
  differs between the stable staging path and the faulting UMA path)
- ALWAYS freezes at the same VdSwap ~1190-1203 boundary
=> The signature (memory-type-specific + fixed boundary + survives both
serialization and maximal coherency) points away from our Vulkan sync and toward the
Adreno driver/hardware itself faulting on sustained GPU access to a large
persistently-mapped HOST_VISIBLE|DEVICE_LOCAL buffer. Either a driver limitation/bug
with this heap type under continuous read load, or this heap is not actually safe
for the way we use it (whole-guest-RAM buffer read every frame) on this Adreno.

## NEXT directions (pick one next session)
1. HYBRID UMA: keep the buffer DEVICE_LOCAL non-sparse (proven 5/5 stable) but write
   via a SMALL host-visible staging ring + vkCmdCopyBuffer ONLY for changed pages.
   Keeps most of the UMA win (no full re-upload, GPU-timeline-ordered copies) WITHOUT
   the host-visible-device-local read hazard. This is the most promising path to a
   STABLE fast UMA.
2. Try a DIFFERENT host-visible memory type (host-visible + host-cached, NOT
   device-local) for the direct buffer; test if the TDR is specific to the
   device-local+host-visible combo vs host-visible generally.
3. Capture the exact kgsl fault address/status at the ~1190 boundary (needs root or
   a kgsl trace) to confirm driver vs our usage.
Recommendation: (1) hybrid - it sidesteps the proven-bad memory type while keeping
the copy-elimination benefit for unchanged pages.

### E5 — CORRECTION: render-target k_2_10_10_10 is FINE; texture-sample format is a separate concern
Read GetColorVulkanFormat (vulkan_render_target_cache.cc:1690-1692): RT color format
k_2_10_10_10 / k_2_10_10_10_AS_10_10_10_10 maps to VK_FORMAT_A8B8G8R8_UNORM_PACK32 -
a universally supported 8bpp format. So the RENDER TARGETS render fine. The
"k_2_10_10_10 (signed) not supported" log was from VulkanTextureCache (sampling
textures), NOT render targets. So that is NOT obviously the black-3D cause - do not
chase it as the RT bug. (Caught before over-claiming.)
=> The black-3D cause is NOT RT-format mapping. It's deeper: either the EDRAM
ownership-transfer / resolve-to-texture step, or the guest draws into the RT but the
result isn't sampled back as the scene texture. Localizing this needs draw-level
tracing (--ez vulkan_trace_draw_state true / vulkan_trace_resolve true) and reading
the resolve + texture-load path - a multi-cycle investigation of its own. Flagged,
not yet root-caused. fbo path + EDRAM tile store is the area (per upstream notes).
