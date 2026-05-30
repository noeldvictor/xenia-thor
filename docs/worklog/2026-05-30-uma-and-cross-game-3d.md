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

### E7 — "hybrid UMA" decision: the proposed hybrid ALREADY EXISTS as the staging path
Read RequestRange (shared_memory.cc:341-411): it already builds upload ranges of
ONLY changed/invalid pages (scans system_page_flags_.valid) and the Vulkan staging
path copies just those into a DEVICE_LOCAL buffer via submission-ordered
vkCmdCopyBuffer. That IS "device-local + changed-page-only upload" - exactly the
hybrid I proposed, and exp (a) proved it 5/5 stable. So there's nothing new to build
there; the only thing pure-UMA-direct removed was the staging memcpy, which is the
part that TDRs on Adreno.
=> The ONLY genuinely-new zero-copy hybrid is VK_EXT_external_memory_host: import
the guest's own RAM pages as Vulkan memory (no copy AND not host-visible-device-local
BAR memory - it's plain host system RAM, so likely no GMU fault). Not referenced in
our code; device support unknown from docs. Added a diagnostic that logs ALL
supported device extensions (vulkan_device.cc) to check on-device. Build + capture
"Vulkan device supported extension:" lines, grep for external_memory_host. If
present -> prototype the import path; if absent -> staging is the ceiling on this
device and UMA-direct is a dead end here.

NOTE: avoid `git add -A` - it normalized CRLF on the protected dirty files
(kernel_flags.*, xam_ui.cc, xboxkrnl_io.cc). Verified NO content lost (those mods
were already in HEAD; diagnostics intact, 15 refs in xboxkrnl_io.cc). Use targeted
`git add <path>` only.

### E8 — DEVICE EXTENSION PROBE RESULT: VK_EXT_external_memory_host NOT supported
Dumped all 113 supported device extensions on the Thor Adreno. VK_EXT_external_memory_host
is NOT present (exact grep: NOT PRESENT). So the only genuinely-new zero-copy hybrid
(import guest RAM directly as Vulkan memory) is UNAVAILABLE on this hardware. Hard
constraint, not codeable-around.
Memory-import-relevant extensions that ARE present:
- VK_KHR_external_memory, VK_KHR_external_memory_fd
- VK_ANDROID_external_memory_android_hardware_buffer (AHardwareBuffer - the Android
  native CPU+GPU shared-memory path; standard zero-copy on Adreno)
- VK_KHR_external_semaphore/fence (+fd variants)

## CONCLUSION on UMA (solution space fully mapped)
Pure zero-copy UMA on this Thor Adreno is a DEAD END via the two routes we have:
1. host-visible-device-local mapped buffer (gpu_uma_direct_shared_memory) -> ~50%
   GPU TDR; ruled out coherency, race, non-sparse, size as causes; it's the memory
   type itself the driver faults on under sustained read. REFUTED as shippable.
2. VK_EXT_external_memory_host (import guest RAM) -> NOT SUPPORTED by the driver.
The staging path (DEVICE_LOCAL buffer + changed-page-only vkCmdCopyBuffer) is the
PERFORMANCE CEILING here and is STABLE (5/5). It already eliminates re-uploading
unchanged pages. The remaining theoretical win (skip the changed-page memcpy) is
exactly what faults.
=> UMA-direct stays DEFAULT OFF (it already is). Staging is the right default.
A future AHardwareBuffer-backed shared-memory redesign COULD give true zero-copy,
but it's a large new subsystem (allocate guest RAM as AHB up front, import as both
host ptr and Vulkan memory) - flag for design, not a quick fix.
Net for SPEED: UMA is NOT the current lever on this device. The real visible-speed
blocker is the cross-game black-3D + low-fps (EDRAM resolve on fbo path). Pivot
there.

## CROSS-GAME BLACK-3D investigation (Blue Dragon, UMA off)

### B1 — EDRAM resolve WORKS; scene IS resolved to RAM (overturns "never resolves")
With vulkan_trace_resolve: abundant correct resolves fire in-game, incl. full
1280x720 Full32bpp scene resolves, a 1024x1024 render-to-texture (dest_format=22),
and 672x720/640x360/320x184 buffers. dest_base values cluster in 0x1C-0x1F range
(1DC14000 x26, 1C2C8000 x15, ...). So EDRAM->RAM resolve is NOT the break.

### B2 — present reads a different address than the scene resolves to
VdSwap frontbuffer ptr = FCDB3000/FCA1B000 (alternating double-buffer, 0xFC range).
Scene resolves land in 0x1C-0x1F. So the 3D scene must be SAMPLED BACK as a texture
into the composited frontbuffer - the classic "sample resolved RT as RAM texture"
path - and that's where it's lost.

### B3 — format link: scene resolves use dest_format=7 = TextureFormat k_2_10_10_10
xenos.h: TextureFormat k_2_10_10_10 = 7 (and k_24_8 = 22 = depth). Device reports
"k_2_10_10_10 (signed) is not supported" (preferred Vulkan fmt 65 =
A2B10G10R10_UNORM). So scene textures resolved as 2_10_10_10 hit the unsupported
path. There are ALREADY two cvar fallbacks for this (vulkan_texture_cache.cc:1874,
1897), both default-off + in the Android allow-list (built earlier for this triage).

### B4 — tested BOTH fallbacks on device (read frames). Partial effect, NOT a fix:
- vulkan_force_signed_2101010_unorm_fallback=true: confirmed active. Background
  shifted blue->darker, a "loading" spinner appeared, but 3D world STILL not
  visible; reverted to flat bg+HUD. NOT the fix.
- vulkan_force_2101010_rgba8_fallback=true: confirmed active. Background flat-blue
  -> BLACK, loading spinner + HUD now render CRISPLY (sharp dots/circle), fps
  2.0 -> 3.9. So the 2_10_10_10 path DOES affect real rendering (the flat-blue was a
  broken constant-decode of a 2_10_10_10 texture; RGBA8 decodes UI correctly). But
  the 3D GEOMETRY is STILL missing.
=> CONCLUSION: black-3D has (at least) TWO parts: (a) a 2_10_10_10 texture decode
issue (real, affects UI/overlay; partially changed by fallbacks) and (b) the 3D
world geometry itself not appearing in the composite - NOT fixed by either format
fallback. The geometry-missing part is the main blocker and is still unexplained.
NEXT: trace draws in-game (vulkan_trace_draw_state) to see if world geometry draws
are issued at all and into which RT; and whether the composite samples the scene RT
base (0x1Cxxxxxx). Distinguish "geometry not drawn" vs "drawn but not composited".
~2-4fps throughout = separate perf problem (lots of resolves/frame, likely the
many 1280x720 Full32bpp resolves = resolve thrash).

### B5 — draw-state trace: geometry IS drawn; world RTs are k_2_10_10_10_FLOAT
vulkan_trace_draw_state in-game: 249 draw lines. Distribution at one in-game moment:
- ps_hash: 50x zero (no pixel shader), 6x 2E372EA28CC404B7 (a real world shader)
- color_mask: 100x 0000 (no color write = depth/setup/occlusion passes), 6x FFFF
  (full color), 6x 000F
- rt color0 fmt: 26x fmt=0, 24x fmt=12 (k_2_10_10_10_FLOAT_AS_16_16_16_16), 6x fmt=3
  (k_2_10_10_10_FLOAT)
=> The world IS being drawn (real shaded color draws exist) into k_2_10_10_10_FLOAT
render targets (fmt 3/12). GetColorVulkanFormat maps k_2_10_10_10_FLOAT ->
R16G16B16A16_SFLOAT (supported), so the RT renders. The suspect is now the
RESOLVE+SAMPLE-BACK of a k_2_10_10_10_FLOAT surface as a TEXTURE (the float variant,
NOT the signed-int k_2_10_10_10 I tested in B4 - different format path). Verify the
texture-cache host format for k_2_10_10_10_FLOAT and whether resolve writes the
float bits in a layout the sample-back decodes.

### B6 — SEPARATE perf finding: Android swapchain dequeue/queue stall (the ~2fps)
The draw trace run also logged Android "OpenGLRenderer: Davey!" frames with
DequeueBufferDuration up to 696ms and QueueBufferDuration up to 516ms per frame.
So a big chunk of the ~2fps is NOT guest GPU work - it's the Android surface
buffer dequeue/queue stalling for hundreds of ms. Likely swapchain present-mode /
buffer-count / vsync interaction on the Thor compositor. This is an independent,
high-value perf lever (could lift fps a lot even before the 3D shows). NEXT for
this: check the Vulkan presenter swapchain present mode + image count
(vulkan_presenter.cc) - prefer MAILBOX/IMMEDIATE + >=3 images if not already.

### B7 — SLOWNESS root cause: CPU-bound on host GPU-emulation threads (NOT swapchain)
CORRECTION to B6: the Android "Davey!" 696ms dequeue stalls were an ARTIFACT of my
heavy vulkan_trace_draw_state logcat flooding. Clean untraced run = still ~2.5fps,
so slowness is real. Present mode is MAILBOX (mode=1, confirmed in swapchain log) -
NOT a FIFO vsync cap, NOT a swapchain problem.
top -H on a clean in-game run: TWO host threads pinned at 100% CPU:
  - "Draw Thread"   100%
  - "GPU Commands"  100%
everything else idle (519%/800% idle). => the ~2.5fps is CPU-bound on host GPU
command translation/recording, not GPU execution and not present. Prime suspect:
the many full 1280x720 Full32bpp EDRAM resolves per frame seen in B1 (resolve-copy
shader dispatches + barriers on every resolve) = resolve thrash on the GPU Commands
thread, plus host Vulkan command recording on the Draw Thread. NEXT: count
resolves+draws per frame and see if resolve count is pathological; check if
render-target/resolve caching is missing so the same surfaces re-resolve every frame.

### B8 — slowness is a 100%-CPU SPIN producing NO GPU work (not draw/resolve volume)
Measured a settled in-game state over 6s windows (clean, then traced):
  - 0 EDRAM resolves, 0 draws (GPU draw trace count = 0 for the whole run), yet
    14-15 VdSwaps (~2.5fps).
  - top -H: "Draw Thread" 100% (R) and "GPU Commands" 100% (R), all else idle.
=> The two host GPU threads are pinned at 100% CPU while the guest issues NO draws
and NO resolves. A 100%-CPU thread producing no output = a SPIN/BUSY-WAIT, not real
work. So the ~2.5fps slowness (at least in this state) is NOT resolve thrash and NOT
draw volume - it's the Draw Thread / GPU Commands threads busy-spinning (polling a
fence / ring buffer / each other) instead of sleeping. This also means the "black 3D"
in this state may partly be that the guest is wedged in a loop issuing no geometry,
not only a sample-back bug. (Contrast B5 where draws DID flow - so the guest moves
between an active-draw state and this spinning state.)
DECISIVE NEXT PROBE: native stack sample of the two hot threads (debuggerd -b <tid>,
or `adb shell cat /proc/<pid>/task/<tid>/stack`, or simpleperf) to see exactly what
they spin on. That names the fix (e.g. a guest ring-buffer wait that should block,
or a host fence poll). Don't guess the fix before reading the stack.
CONFIRMED via wchan (no root needed): /proc/<pid>/task/<tid>/wchan = 0 for BOTH
Draw Thread and GPU Commands while pinned at 100%. wchan 0 = NOT in any kernel wait
=> they spin in USERSPACE, not blocked on a syscall/fence/futex. Pure busy-loop.
(/proc/.../stack needs root - not available; use simpleperf on the debuggable APK
next: `simpleperf record -t <tid> --duration 3` + report, to get the userspace
call stack and name the spin.)

## Session stop point (cross-game black-3D + slowness)
Progress this session:
- UMA: fully mapped + concluded dead-end on this Adreno (host-visible-device-local
  TDR; external_memory_host unsupported); staging is the stable ceiling. Default off.
- Black-3D: EDRAM resolve works, geometry draws (when active) into k_2_10_10_10_FLOAT
  RTs; loss localized to resolve->sample-back of that float format.
- Slowness: NOT swapchain (MAILBOX), NOT resolve/draw volume - it's Draw Thread +
  GPU Commands spinning at 100% CPU with no GPU output in the steady state.
Next levers (bounded): (1) stack-sample the two spinning threads -> fix the spin
(biggest fps win), (2) k_2_10_10_10_FLOAT texture sample-back -> world visibility.
Device left stable UMA-off.

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
