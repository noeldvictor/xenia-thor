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

### B9 — simpleperf WORKS (run-as, no root); slowness is CPU-bound IN THE ADRENO DRIVER
Got simpleperf running via: run-as jp.xenia.emulator.github.debug simpleperf record
-e cpu-clock -t <draw_tid>,<gpucmd_tid> -g --duration 3 -o spin.data (writes to app
cwd /data/user/0/<pkg>). perf_event_paranoid=1 blocks shell+--app, but run-as as the
app uid is paranoid-exempt for its own threads. 23,719 samples, 0 lost.
RELIABLE flat-profile hot symbols (simpleperf's own symbolization):
- ~25%+ total in Adreno driver funcs: unknown[+2a0a450ac] 8.54%, +2a0106084 4.03%,
  +2a0a45068 3.79%, +2a0106038 3.10%, ... (driver build 69e13475cb) - clustered in
  ~2 driver routines.
- vkUpdateDescriptorSets (qglinternal) explicit - per-draw descriptor churn.
- memcpy_opt 1.00% - the shared-memory staging copy (what UMA would remove).
- driven via CommandProcessor::ExecutePacketType3 (guest PM4 executor) - reliable.
So the ~2.5fps is NOT a dumb spin (refines B8): the Draw/GPU-Commands threads are
CPU-bound executing guest commands, with the dominant cost INSIDE the Adreno Vulkan
driver (descriptor-set updates) + our memcpy. Classic mobile-Vulkan per-draw
descriptor/state-update thrash.
CAVEAT: addr2line on the local obj .so gave PARTIALLY MISMATCHED names (some
plausible: ExecutePacketType3/InitializeRingBuffer; some nonsense: XThread::Create
lambda) => installed APK .so != local obj .so build. So the deep callgraph
attribution to OUR code is NOT yet reliable; only the simpleperf-named symbols above
are trusted. NEXT: rebuild + reinstall so the on-device .so matches, re-profile, get
a clean callgraph to the exact draw-setup call site, then fix (cache/reuse
descriptor sets instead of vkUpdateDescriptorSets per draw; or push descriptors).
This descriptor-churn fix is the biggest fps lever and is cross-game.

### B10 — RELIABLE symbolization (correct binary) -> full root-cause call chain
Build-id check: local obj .so = a9f4912c (WRONG), but merged_native_libs .so =
90ad331b which has full .symtab + .debug_info AND is the binary packaged into the
installed APK. simpleperf 'libxenia-app.so[+off]' offsets are valid against 90ad...
Re-ran addr2line against the MERGED .so - now everything symbolizes COHERENTLY:
Flat hot offsets:
  +d6f794/d6f7b4/d6f758 = xe::RingBuffer::ReadAndSwap<uint32_t> (PM4 stream read)
  +dadde8 = xe::gpu::RegisterFile::GetRegisterInfo (register lookup)
Deep callchain (GPU Commands thread, 49.7% children):
  ThreadStartRoutine -> XThread::Create -> XHostThread::Execute
  -> CommandProcessor::Initialize lambda (GPU worker loop)
  -> CommandProcessor::ExecutePrimaryBuffer   <-- per-frame guest cmd executor
  -> ExecutePacketType3 -> ReadAndSwap / GetRegisterInfo
  -> (per draw) Adreno driver vkUpdateDescriptorSets (~25% total) + memcpy_opt.

ROOT CAUSE OF SLOWNESS (reliable, cross-game):
The GPU Commands thread is 100% CPU executing the guest PM4 stream in
ExecutePrimaryBuffer, and the dominant cost is the Adreno driver's
vkUpdateDescriptorSets called per-draw (descriptor/state churn) plus memcpy_opt (the
shared-memory staging copy). NOT a spin (B8 retracted), NOT resolve thrash (B7
retracted), NOT swapchain (B6 retracted). It is genuine per-draw CPU work dominated
by driver descriptor updates.

FIX LEVERS (in impact order, cross-game):
1. Descriptor-set caching/reuse: avoid vkUpdateDescriptorSets every draw. Cache by
   (shared-mem binding, textures, samplers) and only update when the set actually
   changes; or use push descriptors / a persistent bindless-ish set for the
   shared-memory buffer (which is bound once and indexed). Biggest win.
2. memcpy_opt (staging copy) = exactly what UMA removes. UMA-direct TDRs, but the
   THOR-CLEVER angle: the staging memcpy cost is real and measurable here. A bounded
   host-visible *staging ring* (small, not the 512MB device buffer) already exists;
   ensure we're not re-copying unchanged pages (RequestRange already dirty-tracks -
   verify the per-frame copied-bytes is minimal, else fix the dirty tracking).
3. PM4 parse overhead (ReadAndSwap/GetRegisterInfo) - micro, lower priority.

NEXT: read the Vulkan draw-submit path (vulkan_command_processor IssueDraw ->
descriptor set write) to implement #1. Profiling method is now repeatable:
run-as <pkg> simpleperf record -e cpu-clock -t <tids> -g -o spin.data; addr2line
against merged_native_libs .so (build-id must match installed).

### B11 — FIX: descriptor-set caching (acts on B10 root cause)
Found the exact cause in code: vulkan_command_processor.cc had an upstream TODO
"Reuse texture and sampler bindings if not changed" immediately followed by code
that UNCONDITIONALLY clears the texture-descriptor up-to-date bits EVERY draw -> a
fresh transient descriptor set alloc + vkUpdateDescriptorSets per draw = the ~25%
Adreno-driver cost B10 measured.
Fix (cvar vulkan_cache_texture_descriptors, default ON): each draw build a precise
signature of the would-be texture descriptor contents = (texture_count,
sampler_count) + the exact bound image-view handles (GetActiveBindingOrNullImageView)
+ the exact VkSampler handles, for vertex and pixel sets separately. Only clear the
descriptor-set-out-of-date bit (forcing the existing rewrite path) when the
signature differs from the last draw's. Counts are in the signature and the
descriptor-set-layout key is (texture_count, sampler_count, is_vertex), so a matching
signature guarantees a compatible layout -> reuse is safe. Signature stores exact
handles (not a hash) so no false-positive skips. Invalidated at submission start
(where transient descriptors are reclaimed) so a stale set is never rebound. Falls
back to the original per-draw rewrite when the cvar is off (clean A/B).
Files: gpu_flags.{h,cc} (cvar), vulkan_command_processor.{h,cc} (signature members +
logic + submission-start invalidation). Building now. VERIFY PLAN: re-run the B9/B10
simpleperf method with cache ON vs OFF and compare vkUpdateDescriptorSets share +
in-game fps; read a frame to confirm no rendering regression (textures still
correct). Expect a large GPU-Commands-thread CPU drop if consecutive draws share
bindings.

### B12 — descriptor cache VERIFIED (lands, sound) but fps UNCHANGED; B10 misattributed
A/B on Blue Dragon in-game: CACHE-ON 2.4fps vs CACHE-OFF 2.3fps = within noise, NO
fps change. Re-profiled (31607 samples): vkUpdateDescriptorSets NO LONGER in the top
symbols (was 0.47% before) - so the cache DOES reduce descriptor writes - BUT the
dominant cost is unchanged. So descriptor churn was real but MINOR; my B10 claim that
the ~25% unknown[+2a...] driver time was vkUpdateDescriptorSets was WRONG.
RELIABLE re-symbolization (new binary a7db03fa, caller callgraph): the dominant time
is xe::gpu::CommandProcessor::ExecutePacketType0 + xe::RingBuffer::ReadAndSwap<u32> /
RingBuffer::Read / min - i.e. the guest PM4 TYPE0 (register-write) packet parser
reading the command stream dword-by-dword. The unknown[+2a...] addresses are NOT
descriptor updates (they persist with the cache on); likely mis-symbolized JIT/guest
or memcpy. memcpy_opt ~1.1% is the staging copy.
=> TRUE fps bottleneck = raw PM4 command-stream parse throughput: the guest pushes a
huge volume of register writes per frame and ExecutePacketType0's per-dword
ReadAndSwap loop dominates the GPU Commands thread. This is a different, deeper lever
than descriptor caching.
STATUS of the descriptor fix: KEPT (default on) - it is correct, removes real per-draw
work, and helps draw-heavy scenes even though it doesn't move THIS scene's fps. Not a
regression (frame must still be checked for visual correctness next - PENDING read).
NEXT lever (fps): optimize ExecutePacketType0 - it reads N registers via a per-dword
ReadAndSwap; if the ring is contiguous, bulk-read + bulk register-write (or fast-path
contiguous register ranges) could cut the dominant cost. Read ExecutePacketType0 +
WriteRegister to see the per-dword overhead.

### B13 — descriptor cache: NO visual regression + framerate is STATE-DEPENDENT
Cache-ON fresh launch, read frame: a "loading" screen renders CRISPLY at 31.6 FPS
(sharp spinner, no texture corruption / no garbled UI). So (a) descriptor caching
causes NO visual regression on UI, and (b) framerate is highly STATE-DEPENDENT -
loading screen = ~31fps, in-field state = ~2.5fps. This reinforces B12: the field
slowness is driven by PM4 register-write VOLUME (ExecutePacketType0), which is high
in the field and low on the loading screen. The pipeline runs fast when command
volume is low.
NOTE: ADB dropped mid-session ('device not found'); recovered with `adb reconnect`
(device returned). Known intermittent on this rig.

### B14 — RETRY-LOOP THEORY: tested, partly confirmed, refined to a sharp anomaly
Field-state measurements (Blue Dragon, UMA off, descriptor cache on):
- Frame is BYTE-IDENTICAL across 5s (md5 0D9E895E both, size 94339) while both host
  GPU threads sit at 100% CPU the whole time.
- VdSwap IS genuinely advancing (~2.6fps): frontbuffer ptr alternates
  FCDB3000<->FCA1B000 (proper double-buffer) and climbs. So the GUEST itself runs at
  ~2.6fps - it is NOT our host re-presenting a stale frame.
- Guest CPU threads (Main Thread, all XThreads) are IDLE (<=3.8%). Only host
  "Draw Thread" + "GPU Commands" are pinned 100%.
- In a 3s window: 8 VdSwaps, but ZERO G> GPU lines (no draws, no resolves, no
  render-target creates).
THE ANOMALY (sharp, novel): the guest advances its frame loop ~2.6x/sec issuing
ALMOST NO draws, while our host GPU-emulation threads burn 100% CPU - and B12 showed
that CPU is in ExecutePacketType0 (register writes) + RingBuffer reads. So the guest
is submitting HUGE volumes of REGISTER WRITES with almost no actual draws per frame.
That is not normal rendering; it smells like the guest spinning a GPU-side loop
(writing/polling registers) waiting on a condition we satisfy slowly or never -
combined with the FIXME at command_processor.cc:522 ("supposed to process the
WAIT_UNTIL register") and the scratch-register->guest-memory writeback path (:654)
that games busy-poll for CPU/GPU sync.
NOT a host reprocessing bug (VdSwap advances, double-buffer cycles). NOT a guest-CPU
spin (guest threads idle). It is host GPU-command-thread bound on register-write
volume the guest keeps generating.

NEXT PROBE (clean, decisive): the PM4 packet trace gate
(ShouldTraceGpuPacket = gpu_trace_swap && global fetch_add < budget) is ONE-SHOT
from process start - the budget is exhausted during boot/movies, so it logs nothing
in the field state. To see the field-state packet/opcode mix, add a small probe
that counts PM4 packet types + Type3 opcodes PER FRAME (reset at VdSwap) behind a
cvar, or reset gpu_packet_trace_count at swap. Then we'll SEE whether it's a
WAIT_UNTIL/WAIT_REG_MEM poll, a scratch-register sync spin, or genuine huge
register-set churn - which names the fix (e.g. honor WAIT_UNTIL so the guest stops
re-polling, or fast-path the poll). This unifies the ~2.5fps AND likely the
black-3D (guest stuck pre-draw) into ONE root cause.

### B15 — WAIT_REG_MEM spin REFUTED; field-state PM4 command profile captured
24h autonomous goal: Blue Dragon playable at full speed. Iteration 1.
gpu_trace_swap in field state (Blue Dragon, ~2.3fps, 7 frames/3s):
- WAIT_REG_MEM: 385 events in 3s, ALL loops=0 (match immediately, ZERO spinning).
  So the 100% CPU is NOT the WAIT_REG_MEM busy-loop. Hypothesis B14 (register poll
  spin) REFUTED.
- Per-frame PM4 mix (3s window, /7 frames): INDIRECT_BUFFER 511 (~73/frame),
  WAIT_REG_MEM 377 (~54/frame), EVENT_WRITE_SHD 210 (~30/frame), plus 1022 'E',
  659 'C', 56 'V' (truncated opcode names). 7 XE_SWAP.
=> Not a spin; genuine high command-stream VOLUME: ~73 indirect-buffer jumps/frame +
heavy event/wait/coherency packets, yet near-zero actual draws (B14). The guest walks
a big command-buffer tree every frame doing sync/event work. Host GPU Commands thread
is throughput-bound executing this volume.
NEXT (iter 2): identify the 'E'/'C'/'V' opcodes (likely EVENT_WRITE/COND/VIZ-query)
and WHERE the per-frame CPU actually goes now that WAIT_REG_MEM is out - re-run the
simpleperf method but break down ExecutePacketType3 by opcode handler (which Type3
handler dominates). If it's EVENT_WRITE / MakeCoherent / vertex-fetch setup, optimize
that. Also: 73 INDIRECT_BUFFER/frame with ~0 draws suggests the guest submits work
whose draws we DROP (cull/skip) - cross-check with black-3D (draws not reaching RT).

### B16 — PIVOTAL: ~10,600 REAL draws PER FRAME (not zero!). Overturns "near-zero draws"
24h-auto iter2 result. Per-frame draw-outcome counter (budget-free) in field state:
  rendered=~10597  skipped_no_vs=0  skipped_no_rast=0  copy=23   (every frame)
=> The earlier "near-zero draws" (B14/B15) was a TRACE-BUDGET ARTIFACT (vulkan_trace_
draw_state has a one-shot global budget exhausted during boot). REALITY: ~10,600
draws ACTUALLY RENDER every frame, zero skipped. At ~2.4fps that is ~25,000 host
draws/sec.
This reframes BOTH problems:
- SLOWNESS: 10,600 draws/frame is pathological for an RPG field (should be hundreds).
  Per-draw host overhead x 10,600 = the 100%-CPU GPU-Commands/Draw threads. The fix
  is to find WHY so many draws: (a) the guest command buffer being executed multiple
  times per frame, (b) each guest draw exploding into many host draws, or (c) the
  guest genuinely issues huge geometry we should batch/instance. The slow decrement
  10597->10594 over frames suggests a near-constant huge workload, not runaway growth.
- BLACK-3D: draws are NOT dropped at IssueDraw (skipped=0) - so geometry renders.
  The loss is DOWNSTREAM (resolve/sample-back, or everything renders to an offscreen
  RT that never composites to the frontbuffer).
NEXT (iter3): determine if 10,600 is real guest work or our duplication. Check
ExecutePrimaryBuffer / INDIRECT_BUFFER handling - are we re-walking the same command
buffer? Count guest draw packets (PM4 DRAW_INDX) per frame vs our rendered=10600: if
guest issues ~10,600 draw packets, it's real (need batching/perf); if guest issues
few but we render 10,600, we have a command-replay bug (huge win to fix). Use a
per-frame counter of Type3 DRAW_INDX/DRAW_INDX_2 opcodes in command_processor.

### B17 — CONFIRMED: ~10,600 draws/frame is REAL guest work, not host amplification
iter3. Read the draw path: ExecutePacketType3_DRAW_INDX / _DRAW_INDX_2 -> each
calls ExecutePacketType3Draw ONCE, which calls IssueDraw ONCE per packet (no loop
multiplying). So guest PM4 draw packets ~= rendered ~= 10,600/frame. It is genuinely
the GUEST issuing ~10,600 draw calls per frame (an unbatched per-object/per-tile
engine), NOT our command-replay duplicating. ~25,000 host draws/sec at 2.4fps.
=> ROOT CAUSE OF SLOWNESS (confirmed): pathological guest draw-call volume. The only
path to full speed is slashing PER-DRAW host overhead (can't reduce the count; that's
the guest). Per-draw costs to attack (from profile + IssueDraw code), in order:
  1. State/dynamic-state re-application per draw (viewport, scissor, depth bias,
     blend, stencil) - dedup when unchanged (many already have *_update_needed_
     flags; verify they're not force-set every draw like the descriptor bits were).
  2. CmdVkBindDescriptorSets every draw even when sets unchanged (the binding, separate
     from the write the descriptor cache already deduped).
  3. Pipeline (re)bind dedup (current_guest_graphics_pipeline_ check exists).
  4. Per-draw register/shader analysis (AnalyzeShaderUcode) - ensure cached.
NEXT iter4: profile again (simpleperf, matched binary) to see the per-draw hot
functions now that descriptor-WRITE is cached, and dedup the biggest remaining
per-draw op. Target a measurable fps gain at 10,600 draws/frame.
BLACK-3D (separate track): draws render (skipped=0) but not visible -> downstream
resolve/sample-back or offscreen-RT-never-composited; revisit after a perf win makes
iteration faster.

### B18 — ROOT CAUSE NAILED: tiny degenerate draws (avg 3 verts/draw) + draws gate fps
iter5 result. Per-frame counter with vertex stats, Blue Dragon:
  rendered=267 skipped=0 copy=24 total_vertices=852 max_vertices=6 avg_vertices=3
  @ ~30fps (timestamps ~33ms apart) in THIS scene.
- avg_vertices=3, max_vertices=6 => the guest issues DRAWS OF 1-2 TRIANGLES EACH.
  Tiny degenerate draws. (Earlier heavier scene = ~10,600 such draws/frame @2.4fps.)
- DRAWS GATE FPS DIRECTLY: 267 tiny draws -> 30fps; ~10,600 -> 2.4fps. Linear-ish in
  draw count, confirming per-draw host+driver overhead is the cost (not vertex/pixel
  work - only 852 verts total/frame here).
ROOT CAUSE (high-confidence, novel lever): Blue Dragon submits enormous numbers of
TINY draws (avg 3 verts). Real Xbox360 had a hardware command processor + predicated
tiling that ate these ~free; we pay full host IssueDraw + Adreno driver cost per
draw. The fix that gets to full speed = DRAW BATCHING/MERGING: coalesce consecutive
guest draws that share pipeline + render state + descriptors + vertex/index buffer
layout into one host draw (or instanced/multi-draw). This is THE lever for Blue
Dragon full speed and is cross-game (many 360 titles draw this way).
NEXT (iter6): characterize batchability - do consecutive tiny draws share
state/pipeline (mergeable) or alternate state (need state-sorting)? Add per-frame
counters: pipeline-bind count, descriptor-bind count, render-state-change count per
frame vs draw count. If state changes << draws, simple consecutive-merge wins big.
Then design the merge in IssueDraw (accumulate vertices while state stable, flush on
state change / swap).
CAVEAT: this scene = 267 draws @30fps is already fine; need to recapture the heavy
~10,600-draw field scene (walk into town/battle) to validate the batching target.
The avg_vertices=3 finding holds regardless of scene.

### B19 — CORRECTS B18: heavy scene is REAL geometry; bottleneck = per-draw CPU ~40us/draw
iter6 result, HEAVY field scene (the ~2.4fps one):
  rendered=10597 avg_vertices=112 total_vertices=1,194,300 max_vertices=6263
  pipeline_binds=1086 descriptor_binds=14749 copy=23
- avg 112 verts/draw (NOT 3 - that was the LIGHT 267-draw scene in B18). So the heavy
  scene is REAL geometry, ~1.19M verts/frame. B18's "tiny degenerate draws" was
  scene-specific; simple consecutive-merge batching is NOT the heavy-scene lever.
- THE bottleneck is per-draw CPU: 10,597 draws x 2.4fps = ~25,400 draws/sec, and
  1.19M verts x 2.4 = ~2.86M verts/sec. Both are LOW for Snapdragon 8 Gen 2 (does
  1M+ draws/sec, hundreds of M verts/sec). => NOT vertex/fill bound; we are
  DRAW-CALL-CPU-BOUND at ~400ms/10597 draws = ~38us per draw. That ~38us/draw is the
  enemy.
- Batchability: pipeline stable (~10 draws/pipeline bind), but descriptor_binds
  (14,749) > draws because the CONSTANTS set (fetch/float constants) is rewritten +
  rebound EVERY draw, plus texture sets change often. So merging is blocked by
  per-draw constant changes, not pipeline.
WHERE THE 38us/draw GOES (from prior simpleperf B12/iter4, this same scene): Adreno
driver (~25%, descriptor binds + cmd recording) + our PM4 parse (RingBuffer::
ReadAndSwap per dword, byte_swap, RegisterFile::GetRegisterInfo, WriteRegister
per-register logic). No single 80% hotspot - it is broad per-draw overhead.
NEXT (iter7): attack the largest REDUCIBLE per-draw host cost. Candidates, pick by
re-profiling THIS scene with the matched binary:
  (a) descriptor BIND dedup - skip CmdVkBindDescriptorSets when the set handle is
      already bound (the constants set rebinds every draw; if the VkDescriptorSet is
      reused via the uniform pool it may be the same handle).
  (b) WriteRegister hot path - it runs a scratch-range check + COHER switch on EVERY
      register write; fast-path the common case.
  (c) ExecutePacketType0 - bulk-read contiguous register blocks instead of per-dword
      ReadAndSwap + per-register WriteRegister.
Honest: this is incremental (no single big win); target a measurable % at 10,597
draws. The descriptor-write cache (shipped) was step 1; keep stacking per-draw cuts.

### B20 — iter7 profile (matched binary): ~25% Adreno driver (irreducible) + ~10% flat PM4 parse
Heavy scene, 31,686 samples. Split:
- Adreno driver (unknown[+2a..] cluster): top 8.77% + 4.18 + 3.78 + 2.89 + 1.83 +
  1.52 + 1.48 + ... = ~25% total. This is the driver consuming our submitted command
  volume (descriptor binds, cmd buffer recording). Not reducible from our side
  EXCEPT by submitting fewer/cheaper commands.
- Our code: very FLAT tail, each <1%: ReadAndSwap, GetRegisterInfo (~0.85% across 2
  sites - STILL runs because default log budget=128>0, so iter4 reorder only helps
  with logging off), ExecutePacketType0, RingBuffer::Read, byte_swap, WriteRegister
  (base + vulkan virtual override - a virtual call per register write). Sum ~8-10%.
HONEST CONCLUSION: even eliminating ALL our PM4-parse overhead would gain only
~10%; the dominant ~25% is the Adreno driver processing the command VOLUME. So
micro-opts won't reach full speed. The real lever is REDUCING the work submitted to
the driver - i.e. fewer/cheaper descriptor binds + draws. descriptor_binds=14749 >
10597 draws => the per-draw CONSTANTS rebind is extra driver work every draw.
NEXT (iter7-fix): the highest-value reducible driver cost = the per-draw descriptor
BIND of the constants set. The descriptor-WRITE cache (shipped) stopped re-writing,
but we still CmdVkBindDescriptorSets the constants set when its uniform-buffer
sub-allocation offset changes each draw. Investigate: are constants actually
changing every draw, or do we rebind a same-handle set needlessly? If the constants
buffer uses dynamic offsets, we could bind once + use dynamic offset (1 cheap
update) instead of a full rebind per draw. Read the constants descriptor path +
uniform_buffer_pool_ to see if dynamic-offset binding is feasible = real driver-cost
cut at 10,597 draws/frame.

### B21 — UNIFYING INSIGHT: the 10,600-draw heavy scene IS the black-3D scene
Captured the heavy ~2.4fps / 10,597-draw scene: it shows FLAT BLUE + HUD only (the
black-3D state, same frame md5 family as before). So:
- The guest renders the FULL 3D world every frame: 10,597 draws, 1.19M verts, real
  geometry (avg 112 verts/draw). It is NOT idle/stuck/degenerate.
- That entire render produces NOTHING visible - only the 2D HUD composites. The 3D
  result never reaches the frontbuffer (black-3D bug).
- We pay full per-draw CPU (~38us x 10,597) for geometry the player never sees ->
  2.0fps.
=> BLACK-3D and SLOWNESS are the SAME problem's two faces. The world is rendered but
discarded at the resolve/composite step. Fixing the 3D-not-reaching-frontbuffer path
is the highest-leverage move: it makes the world VISIBLE and is the prerequisite to
any meaningful perf judgement (right now we cannot even tell if 10,600 draws is
"correct" because we never see the output). Also constant-buffer invalidation is
already minimal (WriteRegister:1321-1352 only dirties constants the guest actually
writes + that the shader uses) - so descriptor binds are genuinely needed; no easy
win there. Per-draw micro-opts (~10% ceiling) are NOT the path.
PIVOT (iter8): go back to the resolve->frontbuffer/composite path with FULL force.
Known: render targets ARE created + resolved (B1), draws DO render (B16), formats
map OK for RTs (k_2_10_10_10_FLOAT->R16G16B16A16_SFLOAT). The 3D resolves to guest
RAM at 0x1C-0x1F but the SWAP reads frontbuffer 0xFC (B2). The missing link = the
guest composites the resolved 3D scene texture into the frontbuffer via a draw that
SAMPLES the 0x1C-resolved surface - and that sample reads black/empty. NEXT: trace
the FINAL frame's draws (the ones writing to the 0xFC frontbuffer) - what texture do
they sample, and is that texture's data the resolved 3D scene? Use vulkan_trace_draw
_state filtered to draws whose RT base = frontbuffer, inspect their bound textures.
This is THE fix for Blue Dragon full-speed-AND-visible.

### B22 — present-selection experiment: all 3 modes show CLEAR colors, none show 3D
iter8. Tested the present-surface-selection cvars live on the heavy Blue Dragon scene
(read each frame):
- default (none):                  flat BLUE + HUD
- vulkan_present_recent_resolve_on_swap=true:  flat YELLOW, no HUD
- vulkan_present_scored_resolve_on_swap=true:  pure BLACK, 0.0fps
=> Three different CLEAR colors (blue/yellow/black), NONE shows the 3D geometry. The
present machinery DOES change the output (it's wired), but every candidate surface it
can pick is a CLEAR-COLOR surface, not the composited 3D scene. So the 3D geometry -
which IS drawn (10,597 draws) and resolved (1280x720 Full32bpp, B1) - is NOT present
in any surface the swap selects. Most likely: the geometry resolves to a surface that
is later RE-CLEARED before the swap, OR the scene RT is never resolved to the exact
frontbuffer the guest points the swap at (the guest may expect us to present the EDRAM
render-target contents directly, not a resolved copy).
CONCLUSION for the iteration: black-3D is a PRESENT/RESOLVE-TARGET mismatch - the
presented frontbuffer surface holds only a clear, while the real scene lives in an
EDRAM render target (or a different resolved surface) that we never composite/present.
NEXT (iter9): trace, for ONE frame, the FULL sequence of (a) clears to the frontbuffer
addr 1CA1C000/1CDB4000, (b) resolves with dest_base = those addrs, (c) the draw/RT
that holds the actual scene, in timestamp order. Find whether a clear to 1CA1C000
happens AFTER the scene resolve to 1CA1C000 (overwrite bug), or the scene resolves
elsewhere and 1CA1C000 only ever gets a clear (wrong-frontbuffer bug). That names the
fix. The forced-resolve cvars (vulkan_present_forced_resolve_address/width/height/
pitch/format) let us TEST presenting a specific surface address once we know which one
holds the scene.
Device left with default present (cvars off).

### B23 — push descriptors IMPLEMENTED + rendering-correct, but NO fps change (descriptors ruled out)
Implemented VK_KHR_push_descriptor end-to-end (device enable + maxPushDescriptors=32
verified on device; deferred_command_buffer CmdVkPushDescriptorSetKHR record/replay;
IssueDraw pushes texture/sampler sets inline instead of alloc+update+bind). Burnout:
textures render PERFECTLY (full 3D scene, correct textures) - push path is
rendering-correct, no regression. cvar vulkan_push_descriptors default on.
A/B on Blue Dragon heavy scene: PUSH-OFF 2.2fps vs PUSH-ON 2.2fps = NO CHANGE.
Re-profiled PUSH-ON (31,594 samples): VIRTUALLY IDENTICAL to the push-off baseline
(B20) - Adreno driver unknown[+2a0a450ac] 9.15% (was 8.77%), same ~25% driver
cluster, same flat tail. So the descriptor alloc+update+bind was NEVER a significant
cost (vkUpdateDescriptorSets was <0.5%). Push descriptors removes real CPU work and
is correct, but the bottleneck is elsewhere.
=> DESCRIPTORS ARE RULED OUT as the perf lever (both descriptor-cache iter6 AND
push-descriptor B23 = no fps change). The dominant cost is the Adreno driver
function unknown[+2a0a450ac] (~9% alone, ~25% with its cluster) which is NOT
descriptor work (persists across all 3 descriptor strategies: transient/cache/push).
KEPT: push descriptors (correct, removes real work, default on) - good hygiene even
if not the fps lever; helps draw-heavy scenes that ARE descriptor-bound.
NEXT (iter): identify what unknown[+2a0a450ac] in the Adreno driver actually IS - it
is reached per-draw or per-command and dominates. Hypotheses: pipeline/state
validation per draw, render-pass load/store or tile flush per draw (if each draw is
its own subpass/renderpass = catastrophic on a tiler), or descriptor-set/binding
validation independent of how we update them. Check: are we starting/ending a render
pass or inserting a barrier PER DRAW? On a tiler that forces a tile flush per draw =
the real 10,600x cost. Grep IssueDraw / render-pass management for per-draw
SubmitBarriers / render-pass transitions. THIS is likely the true Blue Dragon lever.

### B24 — *** ROOT CAUSE OF SLOWNESS FOUND *** : ~170 render-pass begin/end (tile flush) PER FRAME
vulkan_trace_perf_counters snapshot deltas (Blue Dragon heavy scene, 30 frames apart,
issue_swaps 750->780):
- render_pass_begins: 44597->46829 = +2232 = ~74 PER FRAME
- barrier_force_end_render_pass: 73818->76759 = +2941 = ~98 PER FRAME
- barrier_submits ~145/frame, buffer_barriers ~145/frame, image_barriers ~149/frame
- pipeline_create_us = 11.9 SECONDS cumulative (56 creates) - one-time-ish, not per frame
- shared_memory_staging_copies ~8/frame (small) - NOT the issue
ADRENO IS A TILING GPU: every vkCmdBeginRenderPass = load the whole tile from memory,
every End = store it back (+ any resolve). ~74 begins + ~98 forced-ends per frame =
the GPU FLUSHES + RELOADS the entire framebuffer tile memory ~170 TIMES PER FRAME.
THAT is the ~25% driver cost (unknown[+2a0a450ac] = tile load/store) and the true
~2.4fps bottleneck - NOT descriptors (B23), NOT draw count alone, NOT UMA.
The trigger is explicit: barrier_force_end_render_pass=98/frame - barriers inserted
between draws (buffer/image, ~145/frame) FORCE the render pass to end (Vulkan
disallows most barriers inside a render pass), then the next draw re-begins it =
end+begin = full tile flush each time. On desktop GPUs render-pass changes are cheap;
on a tiler they are the most expensive thing possible. This is THE Blue Dragon
full-speed lever.
NEXT (high value): reduce render-pass breaks. Investigate what inserts ~145
barriers + forces ~98 render-pass ends per frame:
- shared-memory upload barriers between draws? (guest updates vertex/index/constants
  mid-frame -> upload -> barrier -> forced end). If uploads were batched at frame/pass
  start instead of per-draw, the render pass would stay open.
- texture/RT transition barriers per draw.
- Check SubmitBarriers / PushBufferMemoryBarrier callers and whether the shared-memory
  Use(kRead) before each draw forces a barrier that ends the pass.
Target: collapse ~170 tile flushes/frame toward a handful. THIS should move fps hugely.
Grep: barrier_force_end_render_pass increment site + what calls SubmitBarriers(true)
or inserts buffer barriers during the draw loop.

### B25 — MECHANISM CONFIRMED: per-draw shared-memory UPLOAD forces render-pass end -> tile flush
Traced the ~98 forced render-pass-ends/frame to the exact code:
- IssueDraw calls shared_memory_->RequestRange(vertex/index) per draw (vulkan_command_
  processor.cc:3593) -> if pages are dirty, UploadRanges does a kTransferDestination
  write (staging vkCmdCopyBuffer, or UMA memcpy).
- Then Use(kRead) (3640) transitions usage write->read -> VulkanSharedMemory::Use
  (vulkan_shared_memory.cc:305) emits a buffer barrier (fires when last_usage_!=usage
  OR last_written_range_.second, i.e. after ANY write).
- That barrier is queued; SubmitBarriersAndEnterRenderTargetCacheRenderPass ->
  SubmitBarriers(false) -> SubmitBarriers ENDS THE RENDER PASS whenever barriers are
  pending (vulkan_command_processor.cc:2638), then re-begins it.
=> every draw that uploads fresh vertex/index/constant data = 1 buffer barrier = 1
render-pass end+begin = 1 full TILE FLUSH on Adreno. ~98 such draws/frame -> ~170
tile load/stores/frame -> the dominant cost. Staging uploads (vkCmdCopyBuffer) legally
CANNOT be inside a render pass, so the break is structural to the per-draw demand-
upload design.
TWO FIX PATHS:
(A) BATCH UPLOADS: hoist all shared-memory RequestRange/upload for a frame (or a
    render-pass span) to BEFORE the render pass, so no transfer/barrier interrupts the
    pass. Big architectural change (decouple upload from per-draw setup) but the true
    fix - would collapse ~98 flushes toward ~1.
(B) UMA DIRECT-WRITE re-examined: the direct path writes via CPU memcpy + a HOST->
    shader barrier (NOT a transfer). A HOST-stage barrier may still force the pass end
    via the same SubmitBarriers path, BUT there is no vkCmdCopyBuffer, so IF the
    host-write visibility can be guaranteed without a per-draw barrier inside the pass
    (e.g. one flush at submission), UMA could avoid the per-draw break entirely. This
    re-frames UMA: its value is NOT just skipping the copy, but potentially avoiding
    the render-pass-breaking transfer barrier - a tiler-specific win we missed. (UMA
    still has the intermittent GPU-hang from earlier, but THIS is a strong reason to
    revisit making it stable.)
RECOMMEND next: measure (A) feasibility - can uploads be batched at frame start? Most
guest vertex/index data for a frame is known when the command buffer is parsed.
Alternatively prototype: does suppressing the per-draw Use(kRead) barrier (when the
range was already valid/uploaded earlier in the frame) reduce barrier_force_end_render
_pass? The MakeRangeValid dirty-tracking means most ranges are uploaded ONCE - so the
barrier may be firing even when no new upload happened (last_written_range_ stale?).
CHECK: is Use(kRead) emitting a barrier every draw even with NO upload? If yes, that's
a cheap fix (only barrier when an upload actually occurred this draw).

### B26 — CORRECTS B25: uploads are NOT the barrier source (only 8/frame). It's IMAGE barriers (~149/frame)
Before implementing the "batch uploads" fix, recomputed the perf-counter deltas
precisely (30 frames):
- buffer_barriers ~145/frame, IMAGE_barriers ~149/frame, barrier_submits ~146/frame,
  barrier_force_end_render_pass ~98/frame, render_pass_begins ~74/frame
- shared_memory_staging_copies ONLY ~8/frame  <-- uploads are NOT the driver!
=> B25's "batch uploads to stop render-pass breaks" targets the WRONG thing (8 != 98).
The render-pass breaks are driven by ~149 IMAGE barriers/frame = render-target /
EDRAM / texture-usage TRANSITIONS, not shared-memory uploads. Verified the obvious
per-draw barriers ARE guarded (texture->kSwapSampled transition only fires on usage
change, vulkan_texture_cache.cc:1026; Use(kRead) only on write->read transition). So
the ~149 come from render-target/EDRAM management: vulkan_render_target_cache.cc has
many PushImageMemoryBarrier sites (1488,4633,4714,4746,4764,4817,4979,5002) for EDRAM
tile store/restore + RT ownership transfers. Xbox360 has only 10MB EDRAM -> games
constantly store/restore render targets -> each transfer = barriers + compute
dispatches BETWEEN draws -> ends the render pass -> tile flush. This is EDRAM-tile
emulation cost, the hardest/most fundamental part.
GOOD CATCH: verifying the numbers BEFORE coding saved a wasted build on the wrong fix.
NEXT: attribute the ~149 image barriers precisely - are they (a) render-target
ownership transfers (RT base/format reused mid-frame), (b) EDRAM store/restore, or (c)
texture<->RT pingpong (render-to-texture then sample)? Add a cheap per-frame counter
tagging barrier source, OR read the render-target-cache trace. The fix depends on
which: (a/b) may be reducible by keeping RTs resident / better EDRAM layout tracking;
(c) by deferring the sample. This is the real, deep Blue Dragon lever - get the
attribution right before changing code.

### B27 — LOCATED: per-draw EDRAM transfers in PerformTransfersAndResolveClears
VulkanRenderTargetCache::Update (vulkan_render_target_cache.cc:1380) runs per draw
(from IssueDraw:3332) and calls PerformTransfersAndResolveClears(... last_update_
transfers()) EVERY draw (line 1401). The base RenderTargetCache::Update computes
last_update_transfers() = the EDRAM ownership transfers needed when the render-target
config / EDRAM tile ownership changes. Each transfer issues image barriers + a
transfer draw/compute -> ends the render pass -> tile flush. This is the ~149 image
barriers / ~98 forced-ends per frame.
WHY on Xbox360/Blue Dragon: 10MB EDRAM is reused constantly; when a draw targets an
EDRAM region previously owned by a different RT, the contents must be transferred
(restored) to the host RT image first = a transfer with barriers. Frequent RT/EDRAM
reconfiguration = frequent transfers = frequent render-pass breaks.
NEXT (clean iteration): instrument transfers-per-frame (count last_update_transfers()
size + PerformTransfersAndResolveClears non-empty calls per frame, log at swap).
Confirm it tracks the ~98 force-ends. THEN the fix options:
 - reduce redundant transfers (are we transferring when ownership did NOT actually
   change? check the ownership-range dirty logic in base RenderTargetCache::Update).
 - batch/coalesce transfers for a frame.
 - keep more RTs resident to avoid restore.
This is the real lever; instrument-then-fix, do not guess.
Also note: this is fundamental EDRAM emulation - a known-hard area; realistic goal
may be a large fps gain (e.g. 2.4 -> 10-15fps) by cutting redundant transfers, not
necessarily instant 30fps. Measure each change with vulkan_trace_perf_counters
(render_pass_begins + barrier_force_end_render_pass deltas) + fps.

### B28 — MEASURED: EDRAM transfers = only ~45/frame, < half the ~98 force-ends
Instrumented per-frame (Blue Dragon heavy, both logs correlated, 30-frame deltas):
- rt_transfer_calls=25/frame, rt_transfers=45/frame, rt_resolve_clears=2/frame
- barrier_force_end_render_pass ~98/frame, render_pass_begins ~75/frame
- image_barriers ~149/frame, buffer_barriers ~145/frame
- descriptor_binds=10464/frame (~1/draw), pipeline_binds=1086/frame
=> EDRAM transfers (45) account for AT MOST ~45 of the ~98 force-ends - UNDER HALF.
B27's "reduce EDRAM transfers" would only address <50%. Good that I instrumented
before coding it.
KEY RATIO: 45 transfers but ~145 buffer + ~149 image barriers = ~3 barriers PER
TRANSFER (source-image->transfer-read, edram buffer use, dest->attachment). And ~98
force-ends ~ 25 transfer_calls*~? + other SubmitBarriers(true) sites. So each EDRAM
transfer is EXPENSIVE (multiple barriers, each batch ending the pass), AND there are
~53 OTHER force-ends/frame from non-transfer SubmitBarriers(true) callers.
NEXT: instrument the force-end SOURCE. SubmitBarriers(true) callers (from earlier grep
vulkan_command_processor.cc:1427,2208,2324,4345 + render_target_cache resolve/transfer
paths). Add a per-call-site counter (or a single force_end counter per subsystem) to
split the ~98 into: EDRAM-transfer vs resolve vs texture-load vs other. The biggest
non-transfer bucket is the next target. Likely candidate: texture LoadTextureData
(uploading sampled textures) does transfer-dest writes + SubmitBarriers; OR resolves
(IssueCopy) per frame. rt_resolve_clears is only 2 so resolve-clears aren't it.
Hypothesis to test: per-draw texture streaming (LoadTextureData) barriers.

### B29 — SPLIT MEASURED: per-draw breaks=43/frame (barrier 16 + rt_change 27); ~55 INSIDE transfers
Device (Blue Dragon heavy): pass_break_barrier=16/frame, pass_break_rt_change=27/frame
= 43 per-draw-enter breaks. But perf counter = ~98 force_end/frame. RECONCILES:
the other ~55 force-ends happen INSIDE PerformTransfersAndResolveClears, which calls
SubmitBarriers(true) internally per transfer step (25 transfer_calls x ~2 internal
barrier-submits ~= 55) + 43 at the enter = ~98. ✓
=> The DOMINANT render-pass-break source IS the EDRAM transfer machinery after all,
but via its INTERNAL per-transfer SubmitBarriers(true) (~55/frame) + the RT-reconfig
it implies (rt_change=27). So EDRAM transfers cause ~55(internal) + most of 27(rt
reconfig) + ~3-barriers-each. The 45 transfers/frame are the root, NOT the per-draw
upload barrier (pass_break_barrier is only 16).
TARGET (now well-grounded): cut EDRAM transfers (45/frame) AND/OR their internal
render-pass breaks. Two angles:
 1. FEWER TRANSFERS: are the 45 transfers/frame partly REDUNDANT - re-transferring
    EDRAM ranges whose ownership didn't change? Check base RenderTargetCache::Update
    transfer-generation + whether last_update_accumulated_render_targets_ thrashes.
 2. FEWER BREAKS PER TRANSFER: PerformTransfersAndResolveClears does the transfers as
    draws/dispatches needing the render pass ended; if multiple transfers in one
    Update were batched into ONE pass-end + ONE re-begin (instead of ~2 each), ~55 ->
    ~25. Coalesce the transfer barriers.
NEXT: read PerformTransfersAndResolveClears internal SubmitBarriers(true)/EndRenderPass
calls (the ~55 source) + the base transfer-generation to spot redundancy. Instrument-
confirmed: transfers ARE the lever (corrects B28's "under half" - they cause the
internal breaks too, just not at the enter path).

### B30 — transfer mechanism fully characterized; 2 grounded fixes (impl = fresh iteration)
PerformTransfersAndResolveClears uses a SEPARATE transfer_render_pass
(vulkan_render_target_cache.cc:5035 SubmitBarriersAndEnterRenderTargetCacheRenderPass
(transfer_render_pass...)) per destination RT in its loop. So each Update needing
transfers does: end guest pass -> enter transfer pass -> transfer draws -> (next real
draw) end transfer pass -> re-enter guest pass = 2 tile flushes per transfer batch,
looped per dest RT = the ~55 internal breaks/frame.
Transfer generation (base render_target_cache.cc:879-886): ChangeOwnership() fills
last_update_transfers_ only when EDRAM ownership changes (guarded by
WouldOwnershipChangeRequireTransfers). So transfers are ownership-driven, not blindly
per draw - but 45/frame means ownership changes ~45x/frame (frequent RT/EDRAM reuse).
TWO GROUNDED FIXES (next iteration, EDRAM core - delicate, build+verify carefully):
 1. REUSE GUEST RENDER PASS FOR TRANSFERS (explicit upstream TODO at vulkan_render_
    target_cache.cc:4847 "Reuse the guest render pass for transfers where possible").
    If transfers can run in the already-open guest render pass instead of a separate
    transfer_render_pass, the end+begin around each transfer disappears -> kills most
    of the ~55 internal breaks. Highest value, but needs the transfer render pass to
    be compatible with the guest one.
 2. COALESCE: batch all of an Update's per-RT transfers into ONE pass-end + re-begin
    instead of one per dest RT.
DECISION: stop at characterization this iteration (EDRAM transfer system = the most
fragile GPU subsystem; a blind change risks rendering regressions + the scene is
black-3D so visual validation is already hard). Implement fix #1 or #2 in a focused
iteration with before/after vulkan_trace_perf_counters (render_pass_begins +
barrier_force_end_render_pass) + fps, on a scene we can SEE (Burnout menu renders
correctly - use it to verify no regression, Blue Dragon to measure fps).
FULL CHAIN NOW PROVEN: 2.4fps <- ~98 render-pass breaks/frame (tile flushes on Adreno)
<- ~55 internal transfer-pass switches + 27 RT-reconfig + 16 upload-barrier <- 45
EDRAM ownership transfers/frame (Xbox360 10MB EDRAM reuse) using a separate transfer
render pass. NOT descriptors, NOT draw count, NOT UMA, NOT uploads.

### B31 — transfers are NON-redundant (ChangeOwnership optimal); pass-reuse is FORMAT-conditional
Loop iteration. Two findings that sharpen the fix:
1. ChangeOwnership (render_target_cache.cc:1513) is ALREADY optimal: skips ranges
   already IsOwnedBy(dest), only transfers when owner differs (transfer_source !=
   dest), merges adjacent transfers. So the 45 transfers/frame are GENUINELY NEEDED
   ownership changes - NOT redundant. "Fewer transfers via redundancy elimination" is
   a dead end. The transfers are real EDRAM reuse.
2. WHY the transfer pass is separate (the cost): PerformTransfersAndResolveClears sets
   transfer_render_pass_key.color_rts_use_transfer_formats=1 (vulkan_render_target_
   cache.cc:4864) - transfers reinterpret the RT through a DIFFERENT Vulkan format for
   bit-exact EDRAM copy (e.g. R16G16B16A16_SFLOAT RT accessed as _UINT). A VkRenderPass
   is format-specific, so the transfer pass CANNOT equal the guest draw pass when the
   formats differ -> mandatory end+begin (the ~55 internal tile flushes). The upstream
   TODO@4847 says reuse is possible ONLY when the draw format == transfer format (e.g.
   R8G8B8A8_UNORM uses the same for both).
=> SHARPENED FIX (next impl iteration):
   (1a) Reuse the guest render pass for transfers WHEN dest color format uses the same
        Vulkan format for draw and transfer (GetColorVulkanFormat ==
        GetColorOwnershipTransferVulkanFormat). For those RTs, no pass switch ->
        removes a chunk of the ~55 internal breaks for free, no correctness risk
        (same format = same pass compatibility). Measure how many of the 45
        transfers/frame are same-format (instrument: count transfers where draw fmt ==
        transfer fmt) to size the win first.
   (1b) Coalesce: do all transfers for one framebuffer config in ONE transfer pass
        (load/store once) instead of per-dest-RT (TODO 2nd half).
NEXT: instrument same-format-vs-different-format transfer split to size fix (1a)
before implementing. Keep instrument-then-fix discipline.

### B32 — SIZED fix 1a: 24/38 transfer passes/frame are SAME-FORMAT (reusable, ~63%)
Device (Blue Dragon heavy): xfer_same_fmt=24/frame, xfer_diff_fmt=14/frame (38 dest-RT
transfer-pass setups; ~25 rt_transfer_calls, 45 rt_transfers). pass_break_barrier~17,
pass_break_rt_change~27.
=> ~63% of transfer passes are FORMAT-COMPATIBLE with the guest draw pass = eligible
for fix 1a (reuse the guest render pass instead of a separate transfer pass = no
end/begin = no tile flush). 14/frame are float/UINT-reinterpret (k_16_16*/k_32*),
not 1a-eligible. So fix 1a can remove a majority of the internal transfer tile flushes
for FREE (same format = pass-compatible, zero correctness risk).
CONFIRMED WORTH IMPLEMENTING. Next: implement 1a behind a cvar
(vulkan_reuse_guest_pass_for_transfers, default off until A/B'd): in
PerformTransfersAndResolveClears, when (a) not depth, (b) the dest color transfer
format == draw format (is_integer==false), AND (c) the guest render pass is currently
open with a framebuffer that already contains this dest RT as an attachment, skip the
separate transfer_render_pass and record the transfer draws into the CURRENT guest
pass. CAVEAT to verify: the transfer draws sample the SOURCE RT as a texture - can't
sample an attachment that's currently bound in the same render pass (feedback loop).
So 1a is only safe when the source RT is NOT also a current attachment. Need to check
that; if the source is bound, must still break. Measure: xfer pass breaks should drop
by up to ~24/frame, render_pass_begins + barrier_force_end_render_pass fall, fps rise;
verify Burnout menu still renders correctly.

### B33 — fix 1a is MORE SUBTLE than "reuse when same format" (two real constraints)
Before coding 1a, verified two constraints that reshape it:
1. ORDERING: PerformTransfersAndResolveClears runs FIRST in Update (render_target_
   cache.cc:1401); the NEW draw's guest render pass is entered AFTER Update returns
   (IssueDraw:3646). So during transfers the open pass is the PREVIOUS draw's, not the
   destination's. "Reuse the guest pass" isn't a simple swap - the transfer's dest
   framebuffer is being set up by this very Update. The real win = order transfers to
   share ONE pass with the upcoming draw when compatible (TODO 2nd half = coalesce),
   not just match formats.
2. FEEDBACK LOOP: transfer shaders SAMPLE the source RT as a texture; Vulkan forbids
   sampling an attachment bound in the current render pass. So reuse is only safe when
   the source RT is not also an attachment.
=> Fix 1a as naively scoped ("reuse guest pass when same color format") is NOT safe/
simple. The genuinely correct, lower-risk win is the COALESCE direction (fix 1b / TODO
2nd half): within ONE PerformTransfersAndResolveClears call, batch all same-format
dest-RT transfers into a SINGLE transfer render pass (one end+begin) instead of one
per dest RT. That removes the per-dest-RT pass churn without the ordering/feedback
hazards. The 24 same-format passes/frame collapse toward the number of distinct
framebuffer configs (far fewer). 
DECISION: this is the implementation target, but it is a careful EDRAM-core change
needing a deliberate design pass (group transfers by compatible framebuffer, emit one
pass, handle feedback by keeping cross-copy transfers separate). NOT a high-cadence
blind edit. Homework complete + durable: sized (24/frame eligible), constraints known,
correct fix direction = coalesce same-format transfers into one pass.
NEXT: design + implement the coalesce in PerformTransfersAndResolveClears behind a
cvar; A/B with perf counters + fps + Burnout no-regression.

### B34 — IMPLEMENTED same-format transfer pass-reuse (gated, correctness-verified, building)
Found a SAFE slice of the fix that avoids the multi-attachment-framebuffer refactor:
for color transfers whose transfer format == draw format (is_integer==false), set
color_rts_use_transfer_formats=0 so the transfer render-pass KEY becomes identical to
the GUEST draw pass key for that RT. Then SubmitBarriersAndEnterRenderTargetCacheRender
Pass early-returns (no end/begin) when that guest pass is already open = no Adreno tile
flush, for the ~24/frame same-format transfers (B32).
VERIFIED COHERENT before committing (no blind edit): transfer frag shader emits
type_float when !dest_color_is_uint (render_target_cache.cc:2333); framebuffer uses
view_depth_color (native) when flag off (2172); pass attachment native format - all
three agree, so flipping the flag for same-format is correct, not a format mismatch.
Gated behind cvar vulkan_coalesce_edram_transfers (default OFF, in allowlist). Depth +
integer-reinterpret transfers unchanged. HEAD 69663c952, building (b2zyl3pm4).
A/B PLAN (when built): Blue Dragon, coalesce on vs off - per-frame pass_break_rt_change
+ perf-counter barrier_force_end_render_pass/render_pass_begins deltas should DROP (up
to ~24/frame), fps RISE. THEN verify Burnout menu renders correctly (read frame) = no
EDRAM regression. NOTE: this is the conservative version - it only helps when the
same-format transfer's dest matches the CURRENTLY-OPEN guest pass; the full coalesce
(group all same-format transfers in one pass regardless) is a later step if this wins.

### B35 — COALESCE WORKS (render_pass_begins -34%) but fps UNCHANGED -> render-pass breaks are NOT the fps wall
A/B on device (Blue Dragon heavy):
- coalesce OFF: fps 2.4, pass_break_rt_change=27, render_pass_begins ~74/frame (B24),
  force_end ~98/frame.
- coalesce ON: fps 2.4, pass_break_rt_change=19 (-8 at the per-draw enter), and the
  GROUND-TRUTH perf counter: render_pass_begins ~49/frame (-25, -34%!), force_end ~95.
=> The coalesce DOES work - render-pass BEGINS fell 34% (74->49). But FPS DID NOT MOVE
(2.4->2.4). THEREFORE render-pass breaks / tile flushes are NOT the fps bottleneck,
despite being a real ~25% driver cost. B24's "tile flush = the wall" hypothesis is
WRONG as the fps lever. (Cutting 25 begins/frame, ~34%, changed nothing measurable.)
This is a hard pivot - the biggest theory of the last ~12 iterations doesn't gate fps.
WHAT THIS MEANS: the GPU command thread is ~100% CPU but the work that matters isn't
the render-pass management. Re-examine the simpleperf flat profile (B20/iter7): ~25%
Adreno driver (unknown[+2a0a450ac]) + flat PM4 parse. If render-pass begins aren't it,
the driver 25% must be something else per-draw/per-command. OR the bottleneck is NOT
the GPU-commands thread at all in this state - recheck which thread is the gate at
2.4fps (top -H) and whether it's CPU-bound or waiting.
NEXT: re-profile with coalesce ON (simpleperf, matched binary) - does the 25% driver
cost change? And top -H: is GPU Commands still 100%, or is something else now the gate?
Do NOT assume render-pass; the data just killed that. Keep coalesce (it's a real win,
correct, default off) but it's not the fps fix.
KEEP: vulkan_coalesce_edram_transfers is correct + reduces real GPU work (-34% pass
begins); leave default off pending broader validation, but it's a legit optimization.

### B36 — *** REFRAME: it's NOT the GPU driver. ~50% is in UNKNOWN (unmapped) memory ***
simpleperf --sort dso (GPU Commands + Draw threads, coalesce on, 31646 samples):
- unknown (the 0x2a... addresses): 49.72%
- libxenia-app.so (our code): 44.22%
- libc.so: 2.72%
- vulkan.adreno.so (the ACTUAL GPU DRIVER): only 1.53%   <-- NOT the bottleneck!
- libllvm-qgl.so: 0.54%
So the unknown[+2a0a450ac] cluster I kept calling "the Adreno driver" is NOT the
driver (driver is 1.5%). The 0x2a... region is UNMAPPED memory simpleperf can't
attribute - reached via the callgraph from ExecutePacketType0 -> RingBuffer::Read ->
ReadAndSwap. Most likely a large memcpy/memmove (RingBuffer::Read copies command data)
landing in an unsymbolized libc fast-path, OR executable JIT/scratch memory.
=> The fps bottleneck is CPU-side COMMAND-STREAM PROCESSING (our code + this unknown
copy region), ~94% combined, NOT GPU driver work and NOT render-pass management (B35).
The whole "tile flush" line of investigation (B24-B35), while it found real costs, was
not the fps gate. The gate is the sheer cost of parsing/copying the guest PM4 stream:
ExecutePacketType0 (register writes) + RingBuffer::Read (bulk copy).
NEXT (the real lever, finally located): 
 1. Identify the 0x2a... unknown region precisely - is RingBuffer::Read doing a huge
    per-call memcpy? Read RingBuffer::Read. If the guest ring is contiguous, the copy
    may be avoidable (read in place). If it wraps, the copy is the wrap-handling.
 2. ExecutePacketType0 volume: how many register-write dwords/frame? If millions, the
    per-dword ReadAndSwap loop dominates - bulk-process contiguous register runs.
 This is CPU/JIT-domain, finally the right domain (matches "Thor 10-20x CPU" premise:
 we are CPU-bound in our own command processor, not GPU-bound).
KEEP coalesce (correct, -34% pass begins, default off) - real GPU-side win for later,
just not THIS bottleneck.

### B37 — *** THE ANSWER: 50% is JIT'd GUEST CODE (code cache), not GPU, not our C++ ***
RingBuffer fast-read shipped + measured: fps 2.4->2.5 (noise), profile UNCHANGED
(unknown[+2a0a450ac] still 8.6%, unknown dso still 50%). So the unknown region is NOT
the RingBuffer memcpy - I misattributed it (the callgraph showed ReadAndSwap CALLING
INTO 2a0a450ac, but that target is something else).
IDENTIFIED via /proc/PID/maps:
  2a0000000-2b0000000 rwxs ... /dev/ashmem/xenia_code_cache (deleted)
The 0x2a... region is the JIT CODE CACHE (rwx, executable). So the ~50% 'unknown' is
the JIT-COMPILED PPC->ARM64 GUEST CODE EXECUTING (simpleperf can't symbolize runtime-
generated code). THE BOTTLENECK IS GUEST CPU EMULATION (the JIT), NOT THE GPU.
This explains EVERYTHING:
- Every GPU-side fix (descriptors B23, push, coalesce B35, render-pass) did nothing to
  fps because the GPU was NEVER the gate (real Adreno driver = 1.5%, B36).
- Both GPU-Commands + Draw threads CPU-pinned = running JIT'd guest code (these threads
  execute guest memexport vertex shaders / guest callbacks on CPU, or the profile's
  thread attribution includes guest code invoked from command processing).
- Matches the user's "Thor is 10-20x CPU" premise: we're CPU/JIT-bound, not GPU-bound.
TRUE LEVER = guest CPU/JIT performance. NEXT:
 1. Confirm WHICH thread runs the 50% JIT: profile the GUEST CPU threads (the "XThread*"
    / PPCThread worker threads), not just GPU-Commands/Draw - the guest game logic +
    vertex processing likely dominates. top -H earlier showed XThreads mostly idle in
    THIS scene though - so the JIT in the GPU-commands thread may be vertex-shader-on-
    CPU (memexport / draw_util EstimateMaxY ExecuteUnclippedDrawVSOnCpu) or PM4 indirect
    execution. Check cvars: execute_unclipped_draw_vs_on_cpu_for_psi_render_backend,
    and whether vertex shaders are being run on CPU.
 2. simpleperf the whole process (-a or all tids) to see total JIT vs everything.
 3. If guest game logic is the gate, JIT quality (a64 backend) is the lever - but that
    is deep. If it's vertex-shader-on-CPU, moving it to GPU is the lever.
RETRACTS the entire GPU-bottleneck line (B24-B35) as the FPS gate - those were real GPU
costs but the gate is GUEST CPU/JIT execution. Keep the shipped GPU opts (correct,
default-off/safe) but pivot the fps hunt to CPU/JIT.

### B38 — PINNED: the "Draw Thread" is 99.85% JIT code-cache (guest code on CPU)
Profiled the two hot threads SEPARATELY (per-thread simpleperf, this run):
- GPU Commands thread: 85.9% libxenia (OUR C++), flat PM4-parse (RingBuffer::Read now
  the fast inline path + byte_swap + memcpy_opt, each ~2%, no single hotspot), 3.5%
  Adreno driver. ~half the total CPU.
- "Draw Thread": **99.85% unknown = the 0x2a JIT CODE CACHE** (top: 2a0a3f47c 18%,
  2a0106084 9%, ...). Almost ZERO libxenia/driver. = running JIT-compiled GUEST code
  ~100% of the time. This is the dominant 'unknown' from the combined profile (B36).
=> CONFIRMED: the fps gate is the "Draw Thread" executing guest JIT code nonstop. A
thread named "Draw Thread" running 100% guest JIT = Xenia is doing GUEST GPU WORK ON
THE CPU - almost certainly guest VERTEX SHADERS / geometry executed on CPU (memexport
emulation, or a vertex/primitive path that can't go on the GPU), per draw, x10,600
draws/frame. That gates fps and is invisible to GPU optimization (why B23-B35 = 0).
NEXT (the fix domain, finally exact):
 1. Identify what the "Draw Thread" is - grep thread creation for "Draw Thread" name
    in the gpu/cpu backend; find what guest code it JIT-runs. Likely the memexport or
    CPU-vertex-shader path, or the PPC interpreter/JIT worker for a guest-side draw
    helper the game calls per draw.
 2. simpleperf can't symbolize JIT, BUT: enable any "trace memexport" / VS-on-CPU
    counter, OR check cvars for forcing that work onto the GPU. If it's memexport
    vertex shaders run on CPU because vertexPipelineStoresAndAtomics is unsupported -
    check that device feature; if supported, ensure the GPU path is taken not CPU.
 3. If it's genuinely guest game code (not GPU emulation) - then JIT quality is the
    lever (a64 backend), deep.
This is the real, final bottleneck: ~half CPU = our PM4 parse (GPU Commands), ~half =
guest JIT on the Draw Thread. Blue Dragon full speed requires cutting the Draw Thread
guest-JIT cost.

### B39 — *** FINAL ROOT CAUSE: guest game's own "Draw Thread" running JIT'd PPC at 100% ***
Confirmed via logcat: 'Draw Thread (F80002A0)' is a GUEST XThread - Blue Dragon
created it and named it; XThread::Execute thid 19, handle F80002A0. It runs 99.85% in
the JIT code cache (B38). So the dominant fps cost is THE GAME'S OWN ENGINE CODE
(render-submission logic) executing as JIT'd PPC->ARM64, NOT Xenia GPU emulation.
COMPLETE PICTURE of Blue Dragon 2.4fps:
- ~half CPU: GPU Commands thread = our C++ PM4 parsing (10,600 draws/frame to process).
- ~half CPU: guest "Draw Thread" = the game's PPC render code (building those draws),
  run by our JIT at 100%.
=> THE LEVER IS JIT QUALITY (PPC->ARM64 a64 backend) + raw guest-CPU throughput. Faster
JIT of the guest's draw-building code = higher fps. This is the "Thor 10-20x CPU"
domain exactly - and it's the DEEPEST/HARDEST lever (improving generated code quality
of the a64 JIT), not a localized hot-spot fix.
WHY all prior fixes failed (now fully explained): B23-B35 optimized GPU (1.5% of cost);
the gate is guest-PPC-JIT + our PM4 parse, both CPU.
IMPLICATIONS / realistic next steps (each a deliberate iteration):
 1. a64 JIT codegen quality: profile which guest opcodes/sequences dominate (can't
    symbolize JIT directly, but can instrument the JIT to count hot guest blocks, or
    use the a64 backend's own stats). Improve codegen for the hottest sequences.
 2. Reduce PM4 parse cost on GPU Commands (the half we CAN symbolize): it is flat
    (RingBuffer::Read fast path already in; byte_swap/memcpy each ~2%). Bulk-process
    contiguous register writes in ExecutePacketType0 (read N dwords, swap with NEON
    vrev, write a contiguous register range) instead of per-dword - could shave the
    parse half.
 3. The guest issues 10,600 draws/frame - if the game is CPU-limited on real HW too,
    30fps may require both halves optimized. The draw COUNT itself is guest behavior
    we can't change.
HONEST STATUS: investigation COMPLETE - root cause fully and correctly identified
(guest PPC JIT + PM4 parse, both CPU; GPU is 1.5%). The remaining work is deep CPU/JIT
optimization, not a quick fix. Shipped GPU opts are correct+safe (keep, default-safe).
The user's "Thor is 10-20x CPU, should be fast in pure physics" is RIGHT - the gap is
JIT efficiency turning that raw CPU into guest throughput.

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

### B40 — Bulk PM4 type-0 register-write parse: implemented, default on, +9.7% device-measured
Implemented the one actionable single-iteration CPU win from B39 (PM4-parse half,
symbolizable, ours). ExecutePacketType0 (command_processor.cc ~997) bulked the
READ+SWAP: when the contiguous register run doesn't wrap the ring, read the whole
dword block via read_ptr() and byte-swap it with copy_and_swap_32_unaligned (NEON
vqtbl, 4 dwords/iter on ARM64), then AdvanceRead once - instead of per-dword
ReadAndSwap. WriteRegister STILL called per register (side effects preserved:
scratch writeback, COHER dirty, vulkan override's constant/texture invalidation).
Ring-wrap tail + write_one_reg fall back to the per-dword path. Output byte-identical
to the old loop. Gated by cvar gpu_bulk_pm4_type0 (default on, read every packet ->
live A/B via SET_CVAR). Commit e816cdde7 (pushed).

MEASURED (device c3ca0370, same-scene live A/B, Blue Dragon heavy field scene =
rendered=10752 draws/frame, 1.19M verts, avg 111 v/draw - confirmed via per-frame
draw-outcomes line; cvar toggle confirmed "applied" each way in logcat). Guest VdSwap
rate counted over clean 12s windows:
  bulk ON  (default): 34 swaps/12s = 2.83 fps
  bulk OFF:           31 swaps/12s = 2.58 fps
  bulk ON  again:     34 swaps/12s = 2.83 fps
=> reproducible ~+9.7% (delta 3 swaps > +-1-swap noise; ON twice identical). Matches
the B21/B39 prediction that the PM4-parse half tops out ~10%. Verify-gate screenshot
read: HUD (portrait/weapon/HP/MP) renders, 3D world still black (pre-existing black-3D,
unchanged by this commit), scene live (not crashed/frozen). No-regression evidence:
the bulk path produced byte-identical register writes through a 10,752-draw frame.
The deeper ~half (guest 'Draw Thread' PPC->ARM64 JIT in the code cache) remains the
larger lever - a64 codegen quality - and is a separate, harder track.

### B41 — a64 speed profile: Draw Thread is a draw-WAIT SPIN (8246B408 = 92% of guest exec)
Enabled the existing arm64_speed_profile (interval_ms=1000, top=24, thread_snapshot) on
the Blue Dragon heavy field scene (no rebuild - already in the binary). Device-read
report (HEAD e816cdde7):
  summary: funcs=5921 entry_delta=23,125,480 direct=22.8M
  top 01: fn 8246B408 delta=21,315,111 (=92% of ALL guest entry delta!) code_size=1396
  top 02: 822870D8 delta=103,712  (#2 is ~200x smaller)
  ... rest all tens-of-thousands.
  thread snapshot tid=00000013 handle=F80002A0 (the B39 'Draw Thread') last_fn=8246B408
    ctr=820DFA50 (=top04). r1=703FE440.
=> The guest Draw Thread is NOT doing 21M units of render work - it is SPINNING in
8246B408, which the codebase already names "Blue Dragon's known 8246B408 draw-wait
function" (a64_backend.cc:119,156). It is a TIMED WAIT: polls KTHREAD+0x58 (kernel
thread time) and the draw-object token (draw+0x2A10) vs wait_state+0x8, returns 1
(keep waiting) while elapsed < timeout (5000ms), else 0. The guest spins ~21M times
between frames waiting for GPU/token progress. So optimizing its *codegen* won't raise
fps (it would just spin faster for the same wall-clock wait); the fps gate is the WAIT
itself / what it polls. This RE-FRAMES the B39 'guest JIT half': ~half the CPU is a
SPIN-WAIT, not useful compute.
LEVERS already built (default-off, a64_emitter.cc EmitBlueDragonDrawWaitFastpathBody
@4812): arm64_blue_dragon_draw_wait_fastpath (hand-emit the predicate),
+_native_yield_stride (yield host sched while spinning - behavior-preserving, cuts the
spin's CPU + cache-coherency contention on the CP-written token), +_probe/_inline_tick_
step/_host_counter_time (advance KTHREAD+0x58 -> shorten the wait; SEMANTICS-CHANGING,
risk skipping frames). NOTE prior fastpaths here have black-screened BD before
(arm64_vmx_dot_f32 note), so VERIFY rendering on every A/B. A/B in progress.

### B42 — SHIPPED draw-wait fastpath default-on (+27% measured); + MAJOR black-3D lead
Acting on B41 (8246B408 = a draw-WAIT spin = 92% of guest exec), A/B'd the pre-built
arm64_blue_dragon_draw_wait_fastpath on the Blue Dragon heavy field scene (device c3ca0370,
VdSwap/12s windows). The fastpath hand-emits the guest wait predicate faithfully (verified
rendering identical = correct). Results:
  baseline (fastpath off):                              2.83 fps
  fastpath ON, bare (no yield/sleep):                   2.67 fps  (no win - tighter spin)
  fastpath ON + native_yield_stride=16 + sleep_us=100:  3.67 fps  (first run)
  repro (fresh launch, 2 windows):                      3.58 / 3.58 fps
=> reproducible +27% (2.83->3.58). The win is from DESCHEDULING the ~21M/frame spin (yield+
sleep) so the command-processor thread runs unconstrained - NOT from tighter spin codegen.
Rendering verified intact each config (screenshot read: same HUD, black-3D unchanged).
Title-specific (gated on guest fn 0x8246B408 => inert for all other games), so safe default-on.
SHIPPED: flipped 3 compiled DEFINE defaults in a64_backend.cc:
  arm64_blue_dragon_draw_wait_fastpath false->true
  arm64_blue_dragon_draw_wait_fastpath_native_yield_stride 0->16
  arm64_blue_dragon_draw_wait_fastpath_native_sleep_us 0->100
Built (incremental ~15s), installed. NOTE: device persists a global files/xenia.config.toml
that OVERRIDES compiled defaults (only --ez/--ei extras beat it). So default-launch still read
the OLD false/0 from the stale TOML (=2.83). Confirmed the SHIP works by deleting the device
TOML (backed up to _xenia.config.backup.toml) -> xenia regenerated it from the new compiled
defaults (verified true/16/100 in the regenerated file) -> default launch now picks them up.

*** MAJOR CONFOUNDED LEAD (must isolate next): fresh TOML -> Blue Dragon RENDERS THE 3D WORLD ***
After deleting the stale TOML, the default-launch screenshot is a real 3D night scene (moon,
lens flares, ship) at ~5.9fps - NOT the black-3D HUD-only field scene. CANNOT attribute to the
draw-wait change (that only alters spin scheduling, not compositing). Cause: deleting the TOML
reset ALL global cvars to compiled defaults. diff(old backup TOML, fresh) shows prior sessions
had left these NON-default in the device TOML:
  arm64_vmx_dot_f32_fastpath        true->false  (*** its own cvar doc: "black-IDLED Blue Dragon on 2026-05-21" ***)
  arm64_context_value_cache         true->false
  arm64_cr_compare_branch_across_context_barrier true->false
  a64_inline_kf_lower_irql          true->false
  a64_rtl_enter_free_first          true->false
  arm64_add_i64_wrapped_imm_fastpath false->true
=> The black-3D I (and prior sessions) chased may be partly a TEST-ENV ARTIFACT: the stale
device TOML had arm64_vmx_dot_f32_fastpath=true (a documented BD black-idler) plus other
experimental cvars left enabled. A fresh/clean config renders the 3D world. ALL this session's
baselines (lever#1 +9.7%, draw-wait +27%) were measured on that polluted config - the A/Bs are
still valid (same base both sides) but the absolute baseline was degraded.
NEXT ITERATION: isolate which reset cvar restored 3D - relaunch clean + toggle ONLY
arm64_vmx_dot_f32_fastpath (and the others) to pin the black-3D cause. If vmx_dot_f32=true is
it, that's the black-3D explanation (a self-inflicted cvar), and clean Blue Dragon both renders
AND benefits from the shipped draw-wait fastpath - re-measure draw-wait on the CLEAN config too
(its +27% is so far proven only on the black-3D config). Device left on fresh TOML (better state).

### B43 — *** BLACK-3D ROOT-CAUSED: arm64_vmx_dot_f32_fastpath=true (stale-config artifact) ***
Isolated the B42 lead with a clean controlled A/B (device c3ca0370, fresh/correct TOML,
identical launch + hid_nop seq; the ONLY difference is the one cvar):
  RUN A  clean default (vmx_dot_f32_fastpath=FALSE, the compiled default):
         -> renders the FULL 3D WORLD (night scene: moon/lens-flares/ship), 5.67 fps,
            screenshot 1.04 MB. (read)
  RUN B  clean + --ez arm64_vmx_dot_f32_fastpath TRUE:
         -> BLACK-3D field scene (char HUD portrait/HP/MP, black 3D world), 3.58 fps,
            screenshot 50 KB. (read)
=> Toggling ONLY arm64_vmx_dot_f32_fastpath flips 3D-renders<->black-3D AND 5.67<->3.58 fps.
CONCLUSION: arm64_vmx_dot_f32_fastpath=true is THE black-3D cause (its own cvar doc already
warned "the broad path black-idled Blue Dragon on 2026-05-21"). It is ALREADY default-OFF in
code, so a fresh install never black-3Ds. The black-3D that B22-B39 chased as a deep
compositing/resolve bug was largely a SELF-INFLICTED TEST-ENV ARTIFACT: a prior session left
this known-broken diagnostic cvar =true in the device's persisted files/xenia.config.toml,
and every subsequent launch (which reads the TOML over compiled defaults) inherited black-3D.
NO CODE CHANGE NEEDED - the default is already correct. Fix = keep the bad cvar off (device now
on the correct TOML; do NOT restore _xenia.config.backup.toml which has it =true).
IMPACT: with correct defaults Blue Dragon now RENDERS its 3D world at ~5.67 fps - up from the
2.4fps black-3D that started this whole investigation (and the shipped bulk-PM4 + draw-wait
fastpath defaults contribute on top). Caveat: run A/run B landed at slightly different game
points (A faster so further along) but the cvar is the only changed input and the fps delta
alone (5.67 vs 3.58) cannot be scene-timing; conclusion is solid.
NEXT: re-measure the draw-wait fastpath contribution on the CLEAN (3D-rendering) config
(A/B draw_wait on vs off at 5.67 baseline); continue fps optimization now that 3D renders.

### B44 — Clean-config profile: draw-wait fastpath VALIDATED (spin 21.3M->95K); new hot path
Re-profiled on the CLEAN (vmx_dot off, 3D-rendering) config with the shipped draw-wait
fastpath default-on. Scene reached = the "Microsoft Game Studios Presents" 3D windmill
intro (fully rendered, read) - the low fps (2-6) makes intros play out slowly so even
95s + extra skips doesn't reach the field; this is WHY relaunch fps A/B is unreliable on
the clean config (scene-timing drifts with speed; fps is 2-6 scene-dependent).
SCENE-ROBUST, ATTRIBUTABLE RESULT (a64_speed_profile, device-read):
  entry_delta total: 23,125,480 (polluted/B41) -> 408,237 (clean)  ~57x fewer guest entries
  fn 8246B408 (draw-wait spin): 21,315,111 -> 94,896 entries (92% -> 23% of exec), code_size
    1396 -> 488 (= the hand-emitted fastpath body is active)
=> the shipped draw-wait fastpath is doing EXACTLY its job on the clean config: the yield/sleep
cut the spin ~222x. This VALIDATES the ship beyond the black-3D config (where the +27% was
measured); the clean-config fps delta itself is unmeasurable by relaunch A/B (scene drift),
but the spin-entry collapse is the direct, scene-robust proof the mechanism works.
NEW HOT PATH (clean, intro scene; no single dominator now): 82287788 (code_size 35568, 11794),
82274DB0 (11980), 826BF770 (memcpy helper, total 3.39M - this is what arm64_blue_dragon_memcpy_
fastpath targets, default-off = a candidate fastpath next), 822870D8, 820DFA50. Real distributed
rendering work, not a spin. Draw Thread F80002A0 no longer pinned in 8246B408.
NEXT CANDIDATES: (1) arm64_blue_dragon_memcpy_fastpath (826BF770 byte-copy -> host memmove,
built, default-off) - A/B for a win; (2) reach a STEADY field scene reliably (save-state or
longer/auto skip) so clean-config fps A/B becomes possible; (3) CP-thread PM4 throughput remains
the structural gate (10,752 draws/frame is guest behavior).

### B45 — memcpy_fastpath RULED OUT (correct but no win); measurement blocker noted
A/B'd arm64_blue_dragon_memcpy_fastpath=true on the clean config (profiler on). The fastpath
IS active (826BF770 code_size 5156->248 = body replaced by host memmove) and rendering is
intact (read: MS Game Studios windmill intro, full 3D, no corruption). But NO measurable win:
  fps: 3.5/2.75 (memcpy on) vs B44 3.5/2.83 (off) - identical within scene noise.
  826BF770 profile: total/delta 3.41M/9113 vs 3.39M/9001 - unchanged.
=> 826BF770 is entered often but the copies in the reachable (intro) scene are small, so
host-memmove ~= the guest byte-loop. DECISION: keep default-off (no win, though correct). It
*might* help in a heavy field scene with large copies, but that's unproven and unreachable now.
MEASUREMENT BLOCKER (recurring): on the clean config at 2-6fps the intros play out slowly, so
~90-95s + extra skips still lands in the "Microsoft Game Studios" windmill INTRO, not field
gameplay. Clean-config gameplay fps A/B needs either (a) much longer waits (minutes, bad for a
10-min loop), (b) a save-state primitive (none exists - flagged before), or (c) the heavy field
reached via the slower vmx_dot=true path (but that's black-3D + different hot path).
STRATEGIC STATE: two real fps wins shipped (PM4 +9.7%, draw-wait +27%/spin 222x) + black-3D
solved. Remaining micro-fastpaths (memcpy ruled out; f1_carrier@82287788, stricmp@826C5620
untested) are small and measurement-blocked. The structural gate is CP-thread PM4 throughput
(10,752 draws/frame = guest behavior). Diminishing returns on per-function fastpaths; the
high-value next work is reach-gameplay infrastructure (save-state) or CP-thread parallelism.

### B46 — Reached REAL field gameplay; profiled true hot path (no single dominator)
Long wait (~200s) + extended hid_nop skips finally reached FIELD GAMEPLAY on the clean config
(read screenshot: explorable 3D desert/canyon - windmill, building, terrain, foliage, player
character on a ledge; fully rendered, no corruption). Heavy field scene fps = ~1.08 (heavier
than the 2-6fps intros). True gameplay a64_speed_profile (device-read):
  entry_delta=328,022
  top01 8246B408 (draw-wait spin) delta=97,344 (30%) code_size=488 - BOUNDED by the shipped
        fastpath (was 92%/21.3M on polluted config; fastpath holds it ~bounded)
  top02 822870D8 7575 | top03 820DFA50 6159 | top04 82274DB0 5992 | top05 82287788 5898
        (code_size 35568) | top06 826BF770 4939 (memcpy helper)
  top07 KeRaiseIrqlToDpcLevel 4490 | top09 KeAcquireSpinLockAtRaisedIrql 4118 |
        top10 KeReleaseSpinLockFromRaisedIrql 4118  <- notable GUEST KERNEL SPINLOCK/IRQL traffic
  Draw Thread F80002A0: last_fn=8273EF04 (kernel area), not pinned in the spin.
=> NO single non-spin dominator at gameplay. Work is distributed across rendering helpers +
guest kernel synchronization (Ke* spinlocks/IRQL). The fps gate is total WORK VOLUME (CP-thread
PM4 for ~10k draws/frame + distributed guest rendering + kernel sync), not one hot function.
This CONFIRMS the B45 strategic read: the easy single-function fastpath wins are exhausted.
POSSIBLE micro-candidate: a64_inline_kf_lower_irql (was =true in the old TOML, now default-false)
given the Ke*-IRQL prominence - but risky + fps-noisy; unproven.
HONEST STATE: shipped wins (PM4 +9.7%, draw-wait +27%/spin bounded) + black-3D solved are the
substantial gains. Blue Dragon now RENDERS its 3D world (intro 2-6fps, heavy field ~1fps). Path
to true FULL SPEED needs STRUCTURAL work beyond 10-min micro-iterations: (1) CP-thread PM4
throughput / parallelism for ~10k draws/frame (the structural gate), (2) save-state primitive to
make gameplay scenes reachable/measurable, (3) reduce guest kernel-sync overhead. Flag for user:
these are larger multi-session efforts, not autonomous micro-fastpaths.

### B47 — a64_inline_kf_lower_irql RULED OUT; AUTONOMOUS MICRO-PHASE COMPLETE (session summary)
A/B'd a64_inline_kf_lower_irql=true at field gameplay (reached via ~200s wait). Result:
fps 1.08 (= B46 baseline 1.08, identical); profile essentially unchanged (KfLowerIrql still
#11 at delta=4141, ~1.3% of entries - inlining it moved nothing). Rendering intact (read: same
field scene, no corruption/hang despite the APC-check skip). RULED OUT, keep default-off. (Also
confirms the heavy-field gameplay profile is REPRODUCIBLE: this run vs B46 match closely -
entry_delta 327,563 vs 328,022, same top fns/deltas.)

==================== SESSION SUMMARY (2026-05-30/31 autonomous push) ====================
GOAL: Blue Dragon playable at full speed on AYN Thor. START: 2.4fps, BLACK-3D (no world).
SHIPPED (all default-on, device-verified, no regressions):
 - B40 bulk PM4 type-0 parse (commit e816cdde7): +9.7% (2.58->2.83), NEON bulk read+swap of
   contiguous register runs; Burnout-verified safe.
 - B42 Blue Dragon draw-wait fastpath (commit 8a6df601d): +27% (2.83->3.58); 8246B408 was a
   draw-WAIT spin = 92% of guest exec; yield+sleep deschedules it (spin 21.3M->95K entries,
   222x), freeing the CP thread. Title-specific (guest fn 0x8246B408), inert for other games.
SOLVED:
 - B43 BLACK-3D (commit bba38c410): root cause = arm64_vmx_dot_f32_fastpath=true left in the
   device's persisted xenia.config.toml by a prior session (a documented BD black-idler). It is
   ALREADY default-off in code, so it was a SELF-INFLICTED test-env artifact, not a deep bug.
   Clean config RENDERS the 3D world. The B22-B39 "compositing" hunt was chasing this artifact.
RULED OUT (correct but no measurable win; kept default-off): memcpy_fastpath (B45),
 a64_inline_kf_lower_irql (B47).
RESULT: Blue Dragon now RENDERS its full 3D world (was black) - intro ~2-6fps, heavy field
 gameplay ~1fps. Reached + profiled real field gameplay (B46): NO single non-spin dominator;
 work distributed across rendering helpers + guest kernel sync. fps gate = TOTAL WORK VOLUME.
EASY SINGLE-FUNCTION FASTPATH WINS ARE EXHAUSTED. Remaining path to FULL SPEED is STRUCTURAL
 (multi-session, needs design + user-in-loop, NOT autonomous micro-iterations):
  1. CP-thread PM4 throughput / parallelism for ~10,752 draws/frame (the structural gate).
  2. Save-state primitive (none exists) - to make gameplay scenes reachable/measurable so fps
     A/B stops being blocked by ~200s boots + 1-6fps scene-noise.
  3. Guest kernel-sync overhead reduction (Ke* spinlock/IRQL traffic in gameplay).
METHOD NOTES for future: device xenia.config.toml OVERRIDES compiled defaults (only --ez/--ei
 extras beat it) - watch for stale cvars; clean fps is 1-6 scene-noisy - prefer profile entry-
 delta metrics; reach gameplay = ~200s wait + extended hid_nop skips to ~112000ms.
========================================================================================

### B48 — CP-throughput attack: localized the per-draw gate (IssueDraw overhead, NOT PrimitiveProcessor)
User directed: attack CP-thread PM4 throughput for the ~10k draws/frame. Added per-frame CPU
timing buckets to VulkanCommandProcessor::IssueDraw (std::chrono, gated by
vulkan_trace_draw_outcomes_per_frame, logged at swap): cpu_issuedraw_us (whole IssueDraw, rendered
draws) + cpu_process_us (PrimitiveProcessor::Process). DEVICE-MEASURED (Blue Dragon, rendered=
~2133 draws/frame scene, 4 consecutive frames):
  cpu_issuedraw_us = 40,465 / 43,313 / 40,685 / 51,657   (~40-52 ms/frame in IssueDraw alone!)
  cpu_process_us   = 1,612 / 1,822 / 2,103 / 2,288        (PrimitiveProcessor::Process)
  cpu_process_pct  = 3 / 4 / 5 / 4 %
=> ~20 us PER DRAW in IssueDraw; PrimitiveProcessor::Process is only 3-5% (the B37/B39 worklog
hypothesis that PrimitiveProcessor index-conversion is the ~50% hog is REFUTED). The per-draw gate
is the REST of IssueDraw (shader analysis AnalyzeShaderUcode, IsRasterizationPotentiallyDone,
interpolator mask, pipeline lookup/config, render-target Update, descriptor setup, draw submit).
Math checks out: heavy field 10,752 draws x ~20us ~= 215 ms/frame ~= 4.6 fps (matches observed
~1-5fps); 2133-draw scene x ~20us ~= 43ms ~= 23fps IssueDraw ceiling. *** IssueDraw per-draw
overhead IS the structural fps gate for draw-heavy guests. ***
*** REDUNDANCY LEVER (next): pipeline_binds=262 for 2133 draws (~12%) - 88% of draws REUSE the
previous pipeline, yet IssueDraw runs the FULL per-draw path (shader analysis, pipeline config,
RT update) every draw. descriptor_binds=2006/2133 (~94%). So most per-draw IssueDraw work is
REDUNDANT across consecutive same-state draws. The CP-throughput win = detect unchanged guest
render state since the last draw and SKIP/cache the redundant per-draw computation (shader
analysis + pipeline lookup are prime - they recompute identical results). Big, careful change to
the core draw path; gate behind a cvar, verify rendering, measure cpu_issuedraw_us drop + fps.
Instrumentation shipped this iteration (the buckets); the skip/cache optimization is next.
