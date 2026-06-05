# Gears 2 CPU profile: ~20% lock/contention overhead + a WORKING Thor CPU-profiling method

Device profile of Gears of War 2 (CPU-bound, guest thread pegged 100%) via simpleperf,
2026-06-04. 28,414 cpu-cycles samples over 12s on a steady scene. Evidence:
scratch/thor-debug/g2perf.data (+ binary_cache/).

## THE PROFILING METHOD (reusable - unblocks CPU/codegen work for the whole mission)
On-device guest CPU profiling on the retail (non-root) Thor:
- `simpleperf record -p <pid>` is BLOCKED (perf_event_paranoid=1 -> "Permission denied").
- `simpleperf record --app <pkg> -g -f 1000 --duration N -o /data/local/tmp/x.data` WORKS - it
  re-execs via run-as inside the debuggable app (jp.xenia.emulator.github.debug) and profiles it.
- Launch the game first (am start with the turnip cvars), warm up ~55-60s to the steady scene,
  then record. Gears runs HOT (61C in ~58s) - keep warmup+record <= ~75s from a cool start.
- Symbolicate offline: `python <ndk>/simpleperf/binary_cache_builder.py -i x.data -lib <dir-with-
  unstripped libxenia-app.so>` (build-id matched our local obj/local/arm64-v8a/libxenia-app.so),
  then `simpleperf.exe report -i x.data --symfs binary_cache -g caller --sort symbol`.
- LIMITATION: the JIT code cache is anonymous executable pages ("unknown[+2a12xxxx]"), so guest
  functions are NOT nameable without a xenia JIT symbol map (a future tooling task: dump the JIT
  guest-addr<->host-addr map + feed simpleperf a synthetic symbol file).

## THE FINDING (Gears 2, % of cpu-cycles)
- **~24%+ hot JIT guest code** in TWO clusters: unknown[+2a1204xxx] (~10%) + unknown[+2a1251xxx]
  (~13%) = the game's two hottest guest loops. Codegen target, but unnameable (see limitation).
- **~12.6% std::mutex lock/unlock contention**: __aarch64_cas2_acq 5.60% + __aarch64_swp2_rel 2.33%
  ARE the atomic internals of pthread_mutex_lock(3.10% self via unlock + 1.56% lock). Callgraph:
  guest code -> xenia HLE -> std::mutex::lock/unlock -> pthread -> cas2/swp2. ALSO scudo::HybridMutex
  (Android allocator) appears -> some of it is malloc/free contention between guest threads.
- **~8% kernel** ([kernel.kallsyms] 5.05+1.98+0.70) = futex + scheduling, largely from the above
  mutex contention spilling to the kernel.
- => **~20% of CPU is lock / contention / allocator overhead** on this multi-threaded CPU-bound
  title. That is the single clearest cross-game CPU lever (helps Gears 1/2/3, and likely LO).

## ACTIONABLE NEXT (do NOT rush - correctness-critical)
1. Identify the hot xenia mutex (most likely xe::global_critical_region, a recursive_mutex used
   pervasively for memory/kernel-object/GPU ops). The hoist-lock win (fe0e3c4ad) already cut its
   acquisitions on the GPU RequestRange path; the bigger target is guest memory/kernel hot paths
   acquiring it per-op across many guest threads. Finer-grained locking or acquisition-count
   reduction in the hottest HLE paths.
2. Allocator churn: scudo contention suggests heavy malloc/free; object-pool the hot allocations.
3. Build the JIT symbol map so future profiles NAME the hot guest functions (turns the ~24% JIT
   blob into specific codegen targets).

## UPDATE 2026-06-05: FULLY SYMBOLICATED (after deploying the current build, build-id matched)
Deployed the current build (FullDeploy - first time the 5 committed fixes a64 LoadV128Const/SHORT_4
+ MMIO + hoist-lock reached the device; Gears 2 still boots+renders = those fixes VALIDATED non-
breaking). Re-profiled (g2b.data, 24518 samples) with full libxenia-app.so symbols. The contended
mutex + the hottest named runtime path resolve to ONE chain, all under the guest threads
(XHostThread::Execute -> guest JIT -> HLE):
  **SharedMemory::RequestRange -> VulkanSharedMemory::UploadRanges -> SharedMemory::MakeRangeValid
  -> Memory::EnablePhysicalMemoryAccessCallbacks -> PhysicalHeap::EnableAccessCallbacks**
i.e. the GPU shared-memory upload path RE-ARMING physical write-watch callbacks. EnableAccessCallbacks
is the #2 named self hotspot (0.36%) and the chain takes the global lock (the exact lock the hoist-
lock fe0e3c4ad targets - CONFIRMS that lever sits on the real hot path). Other named self hotspots
(all <0.55%, long tail): disruptorplus::spin_wait::spin_once (0.55%, busy-wait), CommandProcessor::
WriteRegister + RegisterFile::GetRegisterInfo (PM4 packet processing), copy_and_swap_32_unaligned
(endian), XXH_read64/XXH3_accumulate_512_neon (hashing), Arena::Alloc, DeferredCommandBuffer::
CmdVkPipelineBarrier (lots of barriers). NONE individually dominant -> the bulk stays guest JIT
code (~24%) + this write-watch/lock path.

NEXT FIX TARGET (careful, correctness-critical): reduce REDUNDANT write-watch re-arming in
SharedMemory::MakeRangeValid / Memory::EnablePhysicalMemoryAccessCallbacks (if a range is already
watched + valid, skip re-arming the protection) and/or cut the lock hold time there. Write-watch
correctness is essential (miss a guest write = stale GPU data = corruption), so any skip must be
provably safe + cvar-gated. This is the deeper half of the hoist-lock lever.

## UPDATE 2026-06-05 (code-analysis + 2nd profile): RE-PRIORITIZED - write-watch path is ALREADY
## optimal; lock contention is SCENE-TRANSIENT, not steady. Real lever = the JIT guest code.
- Code analysis (shared_memory.cc + memory.cc): the path is ALREADY well-optimized, NO clean safe
  win. RequestRange already skips valid pages (empty upload_ranges_ -> early return). EnableAccess-
  Callbacks already skips already-watched pages for the Protect syscall, and its per-page access
  check CANNOT be shortcut (must distinguish already-watched [safe to batch] from kNoAccess [must
  NOT protect or real faults break]). The Protect/mprotect syscall CANNOT move outside the global
  lock (flag-set + mprotect must be atomic, else a guest write between them is missed = corruption).
- 2nd profile (g2b.data, current deployed build) shows mutex+atomic contention ~0.4% TOTAL - vs
  ~12.6% in g2perf.data (1st profile). The ~30x swing is SCENE VARIANCE (nondeterministic intro-
  mash hit different game phases), NOT the build (hoist-lock was off in both). => lock contention
  is a TRANSIENT spike during heavy/loading phases, NOT a steady-state bottleneck. The hoist-lock
  (fe0e3c4ad) is fine as a default-off toggle; don't over-invest in the lock lever.
- THE STEADY bottleneck is the ~24% hot JIT GUEST CODE (consistent across scenes = the game logic).
  To attack it we must NAME the hot guest loops -> BUILD A JIT PERF-MAP: no /tmp/perf-<pid>.map or
  jitdump emission exists yet, but the infra does (GuestFunction::MapMachineCodeToGuestAddress,
  A64Function host code range). Add a cvar-gated writer that, on each guest function emit, appends
  "<host_hex_start> <host_hex_size> guest_<guest_addr>" to the app-readable perf map simpleperf
  reads -> future profiles resolve the "unknown[+2a12xxxx]" JIT frames to guest PCs = named codegen
  targets. THIS is the next high-value tooling unit (own focused cycle).

## UPDATE 2026-06-05: JIT PERF-MAP SHIPPED (15e82252c) + VALIDATED -> hot guest functions NAMED.
cvar cpu_perf_map_path -> A64Function::Setup writes "<host_hex> <host_size> guest_<addr>" per emit.
Deployed; launched Gears 2 with --es cpu_perf_map_path .../files/jit.map -> 15193 functions mapped.
The report's "unknown[+<hostaddr>]" sample addrs are ABSOLUTE host addrs in the JIT cache, so a
~10-line python bisect over the map resolves them. RESULT - the ~24% anonymous JIT blob = TWO guest
functions: **guest_82977688 ~14.79% + guest_8298C2A0 ~10.10% = ~25% of Gears 2 CPU** in two hot
game-code loops. The codegen mission is UNBLOCKED. NEXT: Ghidra-analyze guest_82977688 (Gears 2 XEX,
base 0x82000000) to identify the routine -> a general codegen improvement (if our a64 lowering of its
PPC pattern is suboptimal) or a game-HLE (if it's a known routine like a memcpy/decompress). Also
re-usable on Lost Odyssey / any CPU-bound title.

## UPDATE 2026-06-05: BOTH top Gears 2 functions DISASSEMBLED = ONE spin-wait mechanism (NOT codegen).
- guest_82977688 (~15%): a guest poll/rate-limit routine - 8x 'or rX,rX,rX' NOP delay (already elided
  to 0 HIR ops) then a timeout-gated poll of 0x829917C0 vs 0x1388(5000). Already well-compiled.
- guest_8298C2A0 (~10%): the OUTER time-bounded spin LOOP that repeatedly calls guest_82977688 while
  a timebase condition holds. Driven by **mftb** (move-from-timebase, 8298C334) -> our LOAD_CLOCK
  (a64_seq_memory.cc:720) does CallNative(Clock::QueryGuestTickCount) which takes tick_mutex_ + a
  scaling mul/div PER READ (clock.cc UpdateGuestClock).
=> Gears 2's top ~25% CPU is a guest SPIN-WAIT on a timed condition (likely waiting on an async op /
thread / resource), NOT a compute kernel. NO clean codegen win: the functions are well-compiled; the
cost is inherent busy-spinning for a wall-clock duration. Real levers (both non-trivial): (1) JIT
idle/spin-loop detection + host yield (reduce busy-wait CPU, let the awaited producer run); (2) speed
up the awaited operation (0x829917C0 / the polled condition - game-specific); (3) clock_no_scaling
cvar bypasses the tick_mutex_+scaling per mftb (cuts clock-contention, but the spin DURATION is
wall-clock-bound so it won't cut the spin's CPU burn - only multi-thread clock contention). LESSON:
UE3/Gears is SPIN-WAIT-bound, not compute-bound -> codegen lowering won't move it. NEXT: profile a
DIFFERENT-engine title (Lost Odyssey, Mistwalker JRPG) with the perf-map -> JRPGs likely have real
compute kernels (skinning/math/AI) where the codegen lever actually applies.
