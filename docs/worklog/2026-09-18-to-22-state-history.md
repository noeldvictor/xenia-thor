# State history, 2026-09-18 to 2026-09-22

Moved here from AGENTS.md section 8 on 2026-09-22 (user: "cut to a core"). The text is
unchanged. It is the day-by-day record of the Banjo work (the puzzle-transition freeze, the
LLVM backend, the dark lower half), the Blue Dragon GPU regression, and the harness changes.
The short current state is in AGENTS.md section 8.

- Banjo-Kazooie (4D5307ED) reaches the game world. Two a64 inline fast paths (spin lock,
  IRQL) used their own encodings and stalled it; both use the HLE's state now. The first
  visit to a scene runs at 2 fps while Turnip compiles pipelines on the command processor
  thread (170 ms each); the VkPipelineCache blob now persists during play, and a cached visit
  costs milliseconds. The lower half of the world frame is black with speckles (Xenos axis,
  open). The `XamShowDirtyDiscErrorUI` dialog on the device is closed (2026-09-21): it was the
  profile cvar `xam_redirect_xui_font_cache`, removed from the Banjo profile. The PC dialog
  was the x64 NaN helper (`943c14d15f`). START then stopped at the sign-in prompt: the login
  persists through `logged_profile_slot_0_xuid` in the config file, which Android never reads,
  so `ProfileManager` now signs the existing profile into slot 0 at start. START reaches the
  main menu and three A presses reach the opening story in the world (2026-09-21).
  The black and gray grass on the Spiral Mountain title screen (PC Vulkan, and the device's
  "black glitches around trees") is closed (2026-09-21, commit of this line): the tree pushed
  BOTH texture sets as push descriptors, and a pipeline layout may hold one push descriptor set
  (VUID-VkPipelineLayoutCreateInfo-pSetLayouts-00293). NVIDIA and Turnip dropped the vertex set,
  every vertex texture fetch read zero, and the grass vertex shader (color = vertex color *
  terrain texture) output black. Found with RenderDoc: identical shader, bindings, constants
  and textures in the good and bad captures; the post-VS color output was 0. Only the pixel set
  is pushed now. The PC title screen scores GOOD (lower_black 0.000) in every run since.
  Open on the device (2026-09-21 night): the shadow atlas (1024x1024 k_24_8 at 156AD000,
  resolved every frame) ACCUMULATES casters frame after frame (`not_far24` in the resolve trace
  climbs 599 -> 86033 -> 600274, then resets), where the PC stays under 7000. The lower half
  of the title frame is dark and shadows flicker over objects. Not the launcher toggles
  (gate_rt_update, ownership memo, whole-draw cull, draw merging, fp16, UMA direct, the xendroid
  parity set, TU_DEBUG=nolrz each A/B'd with `tools/thor/atlas_ab.py`): it is in the device
  render-target path or the driver. Next: RenderDoc on the device (the layer from the
  renderdoccmd APK, the in-app trigger cvar), or the device .xtr replayed on the PC with
  `--vulkan_trace_resolve_checksum`. The puzzle-transition stall hit 5 of 14 launches during
  the A/B; it blocks measurement and needs the r3 trap next.
  The stall's fault site is named (2026-09-21 night): `821E3080` is `RtlpDeCommitFreeBlock` in
  the game's own statically linked heap (reNut's function table), the heap whose critical
  section pointer is null under the emulator. The recomp replaces that heap and the memcpy/memset
  family with native code; xenia now does the same at the same guest addresses
  (`src/xenia/cpu/guest_crt_hooks.cc`, cvar `cpu_guest_crt_hooks`, launcher toggle "Run the
  game's C runtime as host code"): a thread-safe small-block heap inside guest memory, host
  memmove/memset. Two lessons from the first device build (2026-09-22): (a) the hooks planted
  nowhere because the lazy code hash was computed after the game patches changed code bytes -
  the kernel's pristine hash (UserModule::CalculateHash) now feeds XexModule::code_hash, and the
  plant happens in XexModule::DeclareFunction; (b) with the hooks planted, only a64-compiled
  callers reached the host heap: the LLVM guest-call helper and GuestFunction::Call entered the
  translated body of a kExtern function. Both now dispatch to the handler. With every call
  routed the device booted to the puzzle on the host heap (48 traced calls, no fault), but the
  PC deadlocked once at boot with the hooks on (every thread waiting on events), so
  `cpu_guest_crt_hooks` defaults OFF with the A/B levers `cpu_guest_crt_hooks_heap`,
  `cpu_guest_crt_hooks_mem`, `cpu_guest_crt_heap_zero_all`, `cpu_guest_crt_heap_no_recycle`;
  a PC series (four rounds of on / on-without-mem / off) and a device series are the next data.
  The offline tools that made this a 40-minute diagnosis instead of a night: the PC code dump
  (`--dump_guest_mem_*` on the Windows build, 15 s) + `guest_disasm_offline.py` with reNut's
  names (meInternalAlloc, the trailer 0x9876+index at block end, RtlSizeHeap as the heap-of-
  pointer probe), and the heap's own boot trace.

  Performance, corrected (2026-09-22 night, `emit_ab.py` and `title_probe.py` at the title
  after the puzzle, device, cool): 6.7 fps. The GPU takes 1 to 13 ms per frame and IssueDraw
  14 to 23 ms, so the emit phase is NOT the bound; the earlier "CPU-bound in emit" reading came
  from six no-op levers (both merge cvars were already off; the attract camera moved between
  the windows). The hottest host threads are two of the game's own worker XThreads (62% and
  59% of a core) and a third at 29%; the main thread and the command processor sit below them.
  simpleperf over 15 s: 48% in JIT'd guest code spread over many functions (the top one,
  `sub_82942B90`, 3.2%), 22% in libxenia (4% `xe_llvm_resolve_cached` and
  `xe_llvm_resolve_function`, the indirect-call resolution; 3% bionic mutex slow paths,
  `NonPI::MutexLockWithTimeout` and the 16-bit CAS), 21% in the kernel in one unnamed symbol
  (the mutex and futex signature), 6% libc. Next: the callgraph run (`title_probe.py
  --callgraph`) names the mutex and the syscall callers; the levers are the lock it names, an
  inline cache for the indirect calls, and `__emutls_get_address` (0.5%, emulated TLS).
  The host-heap stall study ended at 3 of 4 clean boots on the PC with freed blocks kept intact;
  the residual stall is the XCTD read queue (the wait chains name it); default off.

  The puzzle-transition stall, named (2026-09-22 night): a guest call through a garbage
  function pointer. Instance 1: the main thread in `sub_82CE6A38+B0` (a per-tick virtual-call
  loop under `appMainTickPreDraw`) with r3 = 0x361580, an unmapped address; the vtable read
  returns 0 from the readable zero page, and the call to guest 0 loads the indirection table's
  own base (host 0x200000000, code 2). Instance 2: worker thread B in `sub_82951B78+2EC` with
  ctr = 0x3F92C642, a float. The fault handler parks the thread, every other guest thread waits
  forever, the swaps stop. `/trap` now holds the parked thread's full record (the fault hook,
  129ff78e14). The "Null critical section" is `RtlFreeHeap(heap = 0)`: the game's
  `sub_82240178` matches a block's trailer tag against its three "me" heaps (40100000, 40300000,
  40500000; all serialized, locks intact) and returned -1. It did not precede instance 2, so it
  is a second symptom, not the cause. The discriminator ran (`stall_study.py --runs 4 ""
  "cpu_backend_llvm=false"`, 02:26 to 02:45): with the LLVM backend, 3 of 4 launches froze at
  swap 836 to 838 (the same frame every time; the fourth stayed alive); with a64 only, 0 of 4
  froze and all four ran past swap 1045 (the title-colour check missed on the slower backend, so
  the study now counts only frozen swaps as a stall and reports "alive" separately). The stall
  is an LLVM-backend code-generation defect until shown otherwise. Arms so far: the residency
  options off froze 1 of 2 valid runs; every run without the object cache showed swaps 0 (LLVM
  compiles the title at boot for longer than the harness window), so the "fresh LLVM freezes
  3 of 3" and the first barrier arm were VOID, not stalls (the study now says NO_BOOT for
  swaps 0). `llvm_bisect.py` kept [82000000,82400000) (one freeze) and then found neither
  quarter freezing in 2 runs each, which points at a pair (an LLVM caller and an a64 callee)
  rather than one function; stopped. The two candidate mechanisms are in the tree's own
  comments (llvm_assembler.cc ~805-870): an a64 guest entry reached raw by a musttail jump
  reads its backend context through x19, which LLVM did not reserve (fix: `+reserve-x19`,
  object cache version 4), and LLVM callers keeping values in x22-x28/q8-q15 across a call
  into a64 code (`cpu_llvm_guest_call_clobber_barrier`, a cache-key letter, so a warm-up
  launch rebuilds the cache first). Results (04:00 to 04:15): the barrier arm 0 of 4 classic
  freezes (one run froze at swap 307 instead); the x19 arm (`+reserve-x19`, cache v4) 0 of 4
  classic freezes (one run froze at swap 308). Baseline the same night: 3 of 4. Each arm ran on
  a FRESHLY BUILT object cache, the baseline on the old one, and the cache stamp hashes only
  `llvm_assembler.cc/.h` (`tools/build/gen_version_h.py`), not `ppc_context.h`, the helpers'
  signatures or the a64 thunks - so a stale cache was the third candidate. The control (no
  x19, cache v5, 04:25) froze 2 of 2 at the classic point (swaps 843 and 858, one with
  ctr = 0): a fresh cache is not the fix. RETRACTED at 05:00: the shipped x19 build (rebuilt,
  cache v4 rebuilt) froze 2 of 2 at swaps 838 and 839 with the instance-1 signature
  (`sub_82CE6A38+B0`, ctr = 0). So neither the x19 reservation nor (by the same doubt) the
  barrier is shown to fix it; the freeze rate moves between 0 and 4 of 4 across batches of
  four, and four launches cannot separate a fix from a lucky batch. The x19 reservation stays
  (it costs one register and the a64 contract does need it) but is not a verdict. Rule 7
  applied to myself: a 0-of-4 is a claim about the batch first. The skip arm
  (`cpu_backend_llvm_skip_addrs=82CE6A38 82951B78`, 05:02) froze 1 of 4, at swap 858 with NO
  fault record (a freeze without a parked thread: a third shape, or a missed /trap read).
  The asm dump launch then showed `LLVMfallback fn=0x82CE6A38 opcode=is_false -> a64`:
  the instance-1 crash site is an a64 function in every build, so the garbage object pointer
  in its array is DATA corrupted by other code, not that function's code generation. State
  at 05:15: the freeze needs the LLVM backend (a64-only 4 of 4 alive, one batch), it is a
  call through a garbage pointer read from game data, and no single lever tested tonight
  (residency off, x19, barrier, the two callers on a64) is shown to remove it. Next, in this
  order: an 8-launch a64-only arm and an 8-launch LLVM arm on the same evening (the rate
  swings 0 to 4 of 4 between batches of four); then `llvm_bisect.py --runs 4` (the window
  bisection, one frozen run keeps a half); then a write trap on the container's array
  (`sub_82CE6A38`'s r28+0x18) to name the writer. Tools for all three exist.
  The a64-only arm ran (05:25 to 05:55): 0 of 8 froze, all eight past the freeze point
  (12 of 12 tonight), against ten-plus freezes across the LLVM batches. The freeze needs the
  LLVM backend. Interim for the player: `cpu_backend_llvm=false` (the launcher toggle
  "opt_llvm_backend" off) removes it; the title-scene cost of a64-only is the next A/B
  (`emit_ab.py --launch "cpu_backend_llvm=false"`), and in the stall records the a64 runs
  advanced the swap counter about twice as fast during the watch window.
  THE NUMBER (06:05, `emit_ab.py --launch`, the title after the puzzle, case 40.8 C): LLVM on
  6.3 fps (2,303 draws, IssueDraw 23.0 ms, GPU 3.8 ms; the game's workers at 64% and 59%);
  **a64 only 26.7 fps** (2,233 draws, IssueDraw 14.6 ms, GPU 9.4 ms; the command processor
  thread at 63%, the main thread 55%). Same scene, same draw count, four times the frame
  rate, and no freeze in 12 of 12 launches. The LLVM backend is the title's bottleneck AND the
  freeze's precondition. Ship `cpu_backend_llvm=false` for Banjo; measure Blue Dragon both
  ways before touching its default (the backend was built for it).
  SHIPPED (06:20): `XeniaOptimizations.SHIPPED_TITLE_OVERRIDES` gives 4D5307ED the
  "opt_llvm_backend" toggle OFF unless the user chose otherwise on the per-game screen (the
  menu shows it, directive 17). Verified through the play path: the launcher resolved
  4D5307ED, no "LLVMBackend" line in the log (LLVM never initialized), 26.5 fps at the title
  with the a64 thread signature (command processor 65%, main thread 57%). The title is
  30fps-native and the profile caps it at 30, so the title scene now runs near its ceiling.
  Two harness notes: `/cvar?name=` returns the config value, not a launch-Bundle override
  (it said `cpu_backend_llvm=true` while LLVM was off; read the "LLVMBackend" line instead
  until the getter is fixed); and one `emit_ab.py` baseline printed 30.3 fps with 0 draws
  (no trace lines: not a scene measurement, ignore rows with draws=0).
  Confirmed (06:30 to 07:00): eight play-path launches with the shipped default, 0 frozen,
  all eight past the freeze point (20 of 20 tonight on the a64 path). The cvar getter now
  returns the live value (6d07a9db4f). Still open: the null-heap free (`meInternalFree` ->
  `RtlFreeHeap(0)`, about 1 in 4 once the classic freeze is gone - none seen in these
  eight), Blue Dragon's own LLVM-on versus a64 measurement on `bd_gameplay_route.sh`, and
  gameplay-scene fps for Banjo beyond the title (the title now sits near its 30 cap).
  Blue Dragon, a64 only (07:10, `bd_gameplay_route.sh`, EXTRA='--ez cpu_backend_llvm
  false'): the gameplay scene at 11.7 fps stable for 90 s (gate OK, 1,190 draws, 263,199
  vertices per frame, 0 faults) against the documented ~17.5 fps with LLVM on the 2026-08-09
  build. The opposite of Banjo: the LLVM backend earns its place on Blue Dragon, so the
  global default stays ON and the per-title override is the right shape. The same-build LLVM
  arm (07:40, cache warmed, gpu 44 C at start): 11.3 fps in the same scene (gate OK, 1,208
  draws, 263,257 vertices, 0 faults). CORRECTION: on today's build the LLVM backend is
  NEUTRAL for Blue Dragon (11.3 against 11.7) and four times worse for Banjo. And Blue
  Dragon's gameplay scene itself fell from the documented ~17.5 fps (2026-08-09 build) to
  ~11.5 on both backends: a regression of about a third somewhere in the six weeks of
  commits, on the same route and the same scene gate. That is now the largest open
  performance item; it needs a build bisection with `bd_gameplay_route.sh` as the metric
  (one run per cooldown, about 6 minutes each). The global LLVM default stays ON until one
  more title is measured both ways; Banjo's per-title OFF ships.
  The premise held (08:05): commit 4614144aeb rebuilt today (premake regenerated for the old
  tree first - the generated makefiles list the sources of the tree that generated them,
  so the first attempt built nothing) runs the same scene at 14.6 rising to 19.9 fps over
  130 s (gate OK, 263,325 vertices) against today's 11.3 flat. `bd_bisect.py` (git bisect,
  build, warm-up, cool, route, good >= 15.5 / bad <= 13) runs from 08:10; the log is
  `scratch/banjo/rd/bd_bisect.log`.
  The bisection (07:27 to 10:50): 2026-08-17 good (18.5), 2026-09-18 good (18.4), 2026-09-20
  `69b6f4f42c` good (15.6), 2026-09-21 `a062a5aecc` bad (11.15), `333c124907` bad (11.2); the
  commits of 2026-09-21 02:48 to 12:50 could not be measured (the route's button timings fire
  into the loading screen on those builds; two skips, then the driver was stopped). Direct
  tests on master, each one route with the scene gate: full LTO instead of ThinLTO 11.3 (not
  it); `threading_per_object_condvar=true` 11.7 (not it, and no hang, so the flag is safe);
  the cached page count 11.4 (not it; kept, it removes a page-table walk per
  MmQueryStatistics). The gameplay callgraph (`perf-27136-20260922-112125.data`): the guest
  worker's kernel time is 94% syscall - 69% `pthread_cond_broadcast` from `KeSetEvent` ->
  `XEvent::Set` and from `XmaDecoder::WriteRegister`, 17% mutex wakes, `pthread_cond_timedwait`
  under `KeWaitForSingleObject`, `EnterGlobalLock` on the recursive mutex - the game's event
  traffic paid in futex syscalls. The a64 emitter revert 11.4, the leftover "Field dynamic-res
  cap 640" patch disabled 11.5, the present cap off (IMMEDIATE mode) 11.4, the residency cache
  11.4: seven levers, one number. THE NUMBER IS THE GPU (12:40, `vulkan_trace_pass_timestamps`):
  `gpu_frame_us` 78 to 79 ms per frame, 72.5 ms inside passes, 25 render-target transfer calls
  and 45 transfers per frame. The command processor's `vfres_us` 78 ms was the CPU waiting on
  the GPU inside the UMA-direct upload's serialize-before-write; with UMA direct off the same
  wait moved to `setup_us`. Blue Dragon's field is GPU-bound on today's build, so no CPU or
  present lever could move it, and the 09-18 sync is where the GPU work grew: 20 GPU commits,
  one with a switch, `gamma_render_target_as_unorm16` (default true; the Vulkan cache never
  used UNORM16 before `32f998e1ba`; the cvar's own text warns of render-target copies when a
  title switches gamma views). Harness trap of the hour: the route's EXTRA is an intent extra,
  and `EmulatorActivity` copies only whitelisted names - an unlisted cvar is ignored without a
  word (the first UNORM16 arm was void). Any cvar goes through `xenia_launch_cvars` (the
  launch-cvar file, read at startup on every path); the whitelist is for the app's own levers.
  UNORM16 off (through the launch-cvar file) 79 ms; `aliased_depth_read_only=false` 79 ms.
  THE COMPARISON (13:10, the 09-18 commit `9154c42ad9` rebuilt and routed with pass
  timestamps): `gpu_frame_us` 64 ms (58 ms in passes, 6 between) against today's 79 (72.5 in
  passes), with the SAME 25 transfer calls, 45 transfers and 263k vertices per frame, and the
  route at 20 fps. The transfers did not grow; the work inside the passes grew a quarter. That
  points at the 09-18 sync's shader and sampling changes (trilinear `min_linear`/`mip_linear`
  sampler filters `75b13003fb`, extended-range float16 pack and unpack `2ece4a1456`, 21-bit
  approximation rounding `2233a13b05`), each testable as a reverse-applied commit on master
  with a NativeCore build (fast) and one route. The sampler revert: 78.5 to 79 ms (not it).
  A route right after an install with no warm-up rendered nothing for 150 s (VOID): warm the
  cache first, every time, even when the LLVM stamp did not change.
  The extended-range float16 revert: 79 ms (not it). The shading-rate recording change is
  out on inspection: VRS is emitted only for foliage-classified draws and Blue Dragon's field
  has none (vrs_base=0 in both builds). Windows Defender holds a freshly written .so open;
  `thor_build.ps1` now deletes the staged copy before writing it (3cd7195530). Remaining
  candidates in the 09-18 sync: the 21-bit rounding (running), the GPU register reset
  defaults `195b6bf721`, the MIN/MAX blend-factor emulation `12e754437d`, the vertex-fetch
  clamp `dec32502ed`; each reverse-applies cleanly.
  FOUND (13:55): the 21-bit rounding of scalar approximations (`2233a13b05`, `ReduceFloat
  Precision` after every exp, log, rcp, rsq and sqrt: about ten integer ops each) reverted
  gives `gpu_frame_us` 64.5 to 65 ms (58.4 to 59 in passes) - the 09-18 build's number to the
  millisecond - and 15.0 fps on the route against 11.4. The Xenos precision emulation is
  correct for Ace Combat 6's ground and costs a quarter of Blue Dragon's GPU frame on Adreno.
  Shipped as `gpu_round_scalar_approximations` (off on Android, on elsewhere; the launcher
  toggle "Xenos 21-bit scalar approximations" turns it on per title), gated inside the one
  helper; the CPU shader interpreter always rounds. About the old builds' "20 fps": the route
  counts draw-outcome lines per 10 s, and a frame with a mid-frame submission logs twice,
  so the honest comparator across builds is `gpu_frame_us`, not that column.
  Shipped form confirmed (the cvar at its Android default, the getter reads false): 64.5 to
  65 ms of GPU, the route at 14.6 to 15.0 fps. At 64 ms the field cannot pass about 15.6
  presented frames, so the next Blue Dragon lever is GPU work (the pass timestamps name
  the passes), not CPU.
  In presented frames (the badge's swap count, the number the player sees): about 10 fps
  before (badge windows 9.9 to 11.9) and 12.7 after. The route's column (15.0) overcounts;
  `bd_gameplay_route.sh` now prints the badge rate and the median `gpu_frame_us` itself.
  Where the 64 ms go (`gpu_trace_resolve_timing`): 97 passes; geometry 52 ms in 18 passes, of
  which the top two take 25 and 22 ms; EDRAM transfers 3.7 ms in 35; composite 2.7; resolve
  copies 1.6; gaps 4.2. The next Blue Dragon lever is inside those two scene passes (paused:
  the user put Banjo first, 2026-09-22 afternoon).
  Banjo's dark lower half, measured (2026-09-22 afternoon, `banjo_split_ab.py`, 30 attract
  frames per launch, lower band 56-95% of the height): dark in 22 to 25 of 30 frames at
  baseline. Not MSAA (the No-MSAA patch 22/30), not mid-frame submission (30/30 with it off),
  not the Android GPU defaults (all eleven back to desktop values 30/30), not Turnip's tile
  binning (`gpu_vulkan_driver_debug=sysmem` 19/30), not `gpu_fp16_shaders`. The frame is
  two predicated EDRAM tiles (`gpu_trace_bin_select`): tile A (select 80000003, ~396
  packets, rows 0-384) and tile B (select 0C, ~2,558 packets, window offset y=-384, scissor
  from row 384, 1280-pitch 2xMSAA); row 384 of 720 is exactly the 53.3% cut. The PC renders
  the frame whole, and the PC replay of a DEVICE trace (`4D5307ED_3060.xtr`, captured on a
  dark frame) renders the lower half too: the command stream is right, the device loses
  tile B when it executes it. The old "PC GOOD (lower_black 0.000)" did not prove this: the
  dark layer is luma ~30, above the near-black threshold. Tool fixes of the hour: the driver
  selection was written with `apply()` and lost on the harness's force-stop (`commit()`
  now; the Qualcomm driver then loads, and the app dies silently on it); the device trace
  viewer aborted on a `filesystem_error` from the profile directory (no storage root) and
  now starts, but a 79 MB trace did not finish in 300 s. Next: finish the device replay
  (the fast loop), then the tile-B path on the device: the window-offset handling and the
  EDRAM ownership of tile B's range.
  Later the same afternoon (device over wireless adb, 192.168.1.5; the MCP now finds it):
  tile B's resolved block is near black on the device (byte-lane means about 0/8/8/4) with
  ~1,000 distinct values, and with `gpu_debug_offset_tile_no_depth` (no depth for offset
  draws inside a tile pass) its geometry shows up faintly: depth rejects tile B's draws on
  the device. Both tiles use reversed depth (`zfunc` 6, greater-or-equal); the frame order
  is tile A -> clear depth rows 0-384 to 0.0 (far) -> tile B -> clear rows 0-336 to 0.5
  (guest 1.0) -> final pass, IDENTICAL on the device and in the PC replay of the device
  trace (clear values, viewports, NDC transforms). Not the cause, each measured or read:
  the render-target path (both "fbo"), `VK_EXT_depth_range_unrestricted` (no code uses it),
  `VK_EXT_shader_stencil_export` (hidden with `vulkan_disable_shader_stencil_export`: 24/30
  still dark), the depth-store-NONE and retro variants (off by default), the BD depth handoff
  (in-pass transfers off). The PC trace tool builds again (premake vs2022, then MSBuild on
  `build/xenia-gpu-vulkan-trace-dump.vcxproj` with Configuration "Release Windows",
  Platform x64). The device trace viewer's dump mode never played a one-frame trace
  (`SeekFrame` returns when the frame is current; fixed with a `SeekCommand` as trace_dump
  does); its first real replays died - once to a system-wide low-memory kill, once
  unexplained - while the user was using the device for other work, so the replay result
  is still open. Next: the device replay of `4D5307ED_3060.xtr` on an idle device (dark ->
  the device's GPU execution; clean -> a live-only race), then the depth EDRAM contents
  entering tile B (the 640-pitch 4xMSAA pass also writes depth base 0 inside both tiles).
  Device etiquette (user, 2026-09-22): ask before driving the device; the user works on it.
  NOT A RACE (16:32): with `vulkan_debug_wait_each_submission` (the CPU waits for every GPU
  submission) the lower half is dark in 20 of 20 frames, and the Single Player main menu
  shows it in every frame - a static, deterministic repro. The device executes the same
  commands wrong; the replay viewer is not needed to prove it. Turnip debug flags noubwc,
  nolrz, syncdraw, flushall: 13 to 15 of 20 dark each (no change). Same-frame diff (trace
  4D5307ED_3146, device log tagged by swap counter vs the PC replay): tile B's draw-state
  sequence is IDENTICAL (depth, viewport, NDC, scissor). A forced depth clear to far before
  tile B (`gpu_debug_offset_tile_clear_depth`, 1,906 clears): still 14/20 dark - depth is
  not the root cause. The "No shadows" patch: 14/20 dark, but the first three frames were
  bright - something builds up over the first seconds. THE MEASUREMENT: both of tile B's
  color outputs are about ten times darker on the device than in the PC replay of the same
  frame (resolve checksum lane means: color 1 at 1E8E4000 PC 2/79/90/50 vs device 0/7/8/4;
  color 2 at 1E1B4000 PC 80/121/124/32 vs device 12/17/13/0). Tile B's pixel shaders
  produce dark output on the device. Next: per-draw visibility on the device - the
  first draw of tile B whose output is dark (a render-target readback after each draw of
  one tile pass, or a device RenderDoc capture), then that draw's inputs (textures sampled
  by screen position through the window offset are the first suspect: they would read the
  wrong rows only in the offset tile).
  Then (17:00-17:20): `tile_draw_bisect.py` (live `gpu_debug_offset_tile_draw_limit` on the
  Single Player menu, 137 offset draws per tile pass): the lower band is 7 with all of them
  and 9 with none - on the device tile B's draws have NO visible effect, while the PC
  replay of the same frame renders them. Stencil off for tile B
  (`gpu_debug_offset_tile_no_stencil`): 16/20 dark. Depth off: faint geometry appears.
  Depth forced to far: no change. Device and PC match on depth clamp, float24 handling
  (no pixel-shader conversion on either), render-target path, state sequence. Ruled out so
  far: MSAA, mid-frame submission, the Android GPU defaults, tile binning (sysmem), UBWC,
  LRZ, driver sync flags, stencil export, fp16, the 21-bit rounding, shadows, depth
  clears, stencil, CPU/GPU races. What is left is what the logs do not show: the vertex
  and pixel shader outputs of tile B's draws on the Adreno. The tool that answers it is a
  device-side GPU capture (RenderDoc's Android layer in the app, triggered by the existing
  `renderdoc_trigger_capture` cvar) or a per-draw render-target readback.
  The Banjo menu freeze (17:10, device only): A on "Start New Game / Resume Saved Game"
  stopped every guest thread with no fault record. The in-app backtrace gave the cause in
  one call: `NtReadFile` looks up its event handle, the per-thread handle cache
  (`kernel_object_handle_cache`, on for Android only) evicts an old entry and releases the
  last reference to a closed file. `~XFile` then runs `HostPathFile::Destroy`, which deletes
  a delete-on-close vfs entry whose content package the game already closed. The fault is
  in host code with the global lock held, so the thread parks and all other threads wait
  on the lock. The a64 crash diagnostic then faulted too: it read the guest context
  through x20, which is not the context in host code, and this hid the first fault. Fixes:
  the cache holds only dispatcher objects (event, mutant, semaphore, thread, timer) and
  releases an evicted object after the unlock; the diagnostic reads the thread's own
  context and records host-code faults in the trap record (`host_code=1`). Harness:
  `xenia_stall` now detects a locked kernel table or a zero-fps stall with no fault
  record and returns the symbolized frames of each thread in a fault handler
  (`host_fault_threads`, inlined frames expanded) with a verdict line. Axis: OS and
  memory manager - a desktop build (the cache off) never keeps a closed file alive.
  THE DARK LOWER HALF IS SOLVED (18:50): it was our own CPU draw cull. The settings
  snapshot (`xenia_cvars`, new) showed four app toggles on in the device's saved settings
  that the PC never runs: `gpu_cull_compaction`, `gpu_whole_draw_only`,
  `vulkan_merge_draws_indirect`, `vulkan_merge_draws_rewrite`. With the two cull toggles
  off: 0 of 20 frames dark (baseline 8/20, draw merge off 20/20). THE CAUSE, found on the
  PC (19:15, no device): the PC replay of the device trace `4D5307ED_3060.xtr` goes dark
  with the device's toggles (lower-half luma 28 vs 90), and with
  `vulkan_dynamic_state_topology=true` alone - `gpu_cull_compaction` turns dynamic
  topology on. Only triangle list/strip pipelines without a geometry shader have a
  dynamic topology; binding any other guest pipeline (a rectangle-list GS pipeline)
  writes its static topology into the command buffer, and the tracker still held the old
  value, so the next strip drew with the wrong topology. Fix: a guest pipeline change
  marks topology and restart for re-emission. PC replay with every device toggle: 90.
  A first theory (the cull tests the guest clip box, not the host one) was wrong as the
  cause; the host-clip-box cull test stays as a correctness fix. Ruled out on the way:
  hiding the tile-GPU extensions (`vulkan_hide_extensions=tiler`, new) made it worse
  (18/20). Lessons: the earlier "not the Android GPU defaults" check reset only the
  `XE_ANDROID_DEFAULT` cvars, never the app toggles; the order is now snapshot
  (`xenia_cvars`; every Emulator::Setup logs "Non-default cvars") -> PC replay of the
  device trace with those settings (`tools/pc/trace_ab.py --from-snapshot`) -> the
  device only to confirm. The user's Thor use: no emulator run without approval for the
  rest of 2026-09-22.
  PC parity, Gears of War 1 (19:30-20:05, PC only, `tools/pc/pc_run.py` with the device
  snapshot): boots, menus, loads, reaches gameplay ("Exit the cell area.") in 290 s with
  the device's settings; no stall on the PC. `trace_gpu_request_file` (new cvar) plus
  `pc_run.py --trace-at` trace the frame on screen of a parked window; `trace_ab.py` on
  that frame: every device setting is neutral (cull, merges, VRS 0.0-0.3) EXCEPT
  `gpu_fp16_shaders`: image diff 2.2, the lit floor darkens (lower-half luma 12.8 ->
  9.4), a lamp bands. Shipped per-title override: Gears (4D5307D5) runs with FP16 off.
  THE DEVICE'S GLOBAL TOGGLES ARE EXPERIMENT LEFTOVERS: cull, both draw merges, fp16 and
  VRS are on in the saved global settings although each defaults off - every title on the
  device runs with them. The cull and merges are correct now (topology fix) and faster;
  fp16 is lossy per title. Next device step (with the user's approval): reset the global
  experimental toggles, then enable per title only what the PC trace A/B shows neutral.
  MagnaCarta 2 (4E4D080B) on the PC with the device's settings reaches the in-engine
  castle scene in 200 s; trace A/B: every device setting neutral (diff <= 0.1).
  `cpu_global_lock_mutex=false` (04:45, one run): the transition passed, then no frames with
  the main thread and one worker at 100% each - the livelock the original mtmsr comment
  predicted. The per-thread depth alone is not a substitute for the mutex; the lever stays
  off and the kernel-time attack needs another shape (a cheaper lock, fewer enter/leave
  pairs, or the per-section analysis of what the guest does under mtmsr). Harness lesson of
  the same hour: `xenia_install` refuses while the app runs and the chain read its answer as
  a dict without `exit` - force-stop first, and check `match` before trusting a device run.
  The swap-307/308 freeze is the SECOND bug, now with its chain: the main thread faults in
  `RtlpInsertFreeBlock+8` <- `RtlFreeHeap+1CC` <- `meInternalFree+BC` <- `sub_82364BB8+54`
  with ctr = `DoWork_CStreamingWaveBank_XACT`: `sub_82240178` matched no heap's trailer tag,
  returned -1, and the free ran on heap 0 (the "Null critical section" lines). About 1 launch
  in 4 once the classic freeze is gone. Next for it: the trap dump of the block at the
  null-CS hit (`title_probe.py` arms it) and `RtlSizeHeap`'s answer for that block.

  Retro 2026-09-21 (the stop ritual): slow = one device launch per cvar, 50 launches for four
  facts, 40% of them lost to the stall and the rest to heat; the tool that would have made it
  fast = live `cvar_set` A/B inside one launch with a per-frame metric, RenderDoc headless on the
  PC, the offline code dump with the recomp's names; exists now = yes for all three
  (`atlas_ab.py`, `pc_goto.py --renderdoc` + `tools/renderdoc/*`, `guest_disasm_offline.py`),
  and the missing one, a device-side RenderDoc capture, is the next tool to build.

  Retro 2026-09-22 (the stop ritual): slow = a per-frame metric read across live cvar windows
  without the lever's current value (six no-ops read as six wins), and a fault whose one diag
  line the log ring cut and the harness cleared; the tool that would have made it fast = the
  harness refusing a no-op lever, and the fault filling the trap record; exists now = yes for
  both (`emit_ab.py`, `KernelTrapRecordFault`), plus `title_probe.py` (profile and trap in one
  launch) and the offline disassembler decoding past VMX128 words.
  Open (2026-09-21 evening): the puzzle transition crashes a guest thread in two runs of three,
  and the game then stalls with a half-lit puzzle on the panel (every thread waits on the crashed
  thread's semaphore). Two fault shapes so far: a halfword load at guest 0xFFFFFFF8 (null minus 8)
  inside the allocator 821E2F60 on the bundle thread, and a jump to host 0x200000000 (a guest
  target of 0x100000000, a 33-bit value) with the host lr in JIT code. The extended
  `A64 CRASH DIAG` (every fault code, the lr function, r1, the guest back chain) now names it:
  the main thread faults in guest 82716418 with r3 = 0 right after "Null critical section in
  RtlEnterCriticalSection", after the title enumerated the two save games the fork wrote at
  13:57 (XamContentCreateEnumeratorInternal lists them twice) and opened one. With the saves
  moved aside the transition did not crash but stalled early instead: the bundle thread and a
  second thread wait on the async read event F8000058 (NtCreateEvent) while the main thread
  runs inside the allocator 821E2F60 - the async-completion race class of June
  (`xboxkrnl_ntreadfile_force_complete`). Next: a run with `xboxkrnl_file_io_trace=true` through
  `xenia_launch_cvars`, and a look at the save the fork writes (1,411 bytes + a 328-byte
  header under `content/B13EBABEBABEBABE/4D5307ED/00000001`). Banjo's water draws fail in the Vulkan backend on the PC and the
  device alike (tessellation is unimplemented upstream; `PM4_DRAW_INDX(99, 17, 0): Failed in
  backend` = kTrianglePatch). The title screen shows speckle noise on textures (open).
  Details: `docs/research/20260920-banjo-spinlock-protocol.md`,
  `docs/research/20260921-banjo-dirty-disc-font-cache.md`.
