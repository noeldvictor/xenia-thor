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
