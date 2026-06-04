# xenia-thor Hyper-Optimization Roadmap (2026-06-03)

Produced by the `thor-hyperopt-rethink` super-workflow (38 agents: map -> ideate ->
adversarial critique -> synthesize) analyzing xenia x Turnip x AYN Thor (CPU/NEON/GPU/UMA).
Grounded in device-measured profiling: **Blue Dragon on Turnip is CPU-throughput-bound**
(gpu_frame_us ~1ms vs a ~256ms / ~4fps frame; the ~197ms = guest a64-JIT + xenia per-draw
CPU submit across ~1100-2180 draws/frame). Turnip is 2.4x faster than Qualcomm at identical
gpu_frame_us, proving the lever is CPU/submit throughput, not GPU fill.

## Framing: reconcile the two profiling realities
Both true, different cost terms:
- CPU-side per-draw/submit + guest-JIT (~197ms) = the headline.
- "GPU busy 77-79%, CPU 84% idle" = the GPU is busy doing *structurally bad work* (1100-2180
  tiny ~30-vert draws, per-draw position-VS binning, render-pass breaks) - the binning
  FRONT-END floor, not fill. CPU is "idle" in aggregate because the work is pinned to ONE
  critical-path thread (GPU-Commands + ring-feeder) while 7 cores sit idle.

**Unifying enemy: per-draw cost - paid simultaneously on the single hot CPU thread AND in the
GPU binning front-end - multiplied by a draw count that is far too high.** Every high-value
rethink cuts (a) per-draw CPU work, (b) draw/triangle count, or (c) moves the hot thread onto
the X3 (3.19GHz) instead of an A510 (2.0GHz).

## Top rethinks (ranked by impact x feasibility)

### R1 - Heterogeneous-aware affinity/priority router (pin hot thread to X3) [Rank 1, M, Risk None]
Replace the hardware-blind abstract-CPU mapping (`XThread::SetActiveCpu`,
`ignore_thread_affinities/priorities` default-true) with a Thor topology router: probe
cpufreq to find X3=cpu7 / mids=cpu3-6 / A510=cpu0-2; hard-pin the GPU-command worker +
ring-feeder guest thread to cpu7 via the gated `sched_setaffinity` (threading_posix.cc:626-645);
secondary hot threads -> A715/A710; spin/audio/helper -> A510; raise uclamp.min/nice to hold
max DVFS. Single-thread critical path => guaranteeing 3.19GHz is a direct multiplier; also
UNLOCKS every other CPU lever. Impact 5-15% wall-clock (1.5x is an upper bound). Hints only,
zero-risk fallback. **Cheapest exp:** cvar `thor_thread_router` pinning the GPU-Commands tid to
cpu7; A/B fps + /proc/<tid>/stat voluntary_ctxt_switches at matched guest_ms; ship if >=5%.

### R2 - Persistent constant-buffer arena + per-draw CPU work elision [Rank 2, L, Low-Med]
(Consolidates #5 persistent UBO arena, #6 run-coalescing, #18 merge-eligible elision, descriptor
half of #8/#20.) Today every constants-changed draw re-Requests a fresh per-type sub-alloc,
scatter-memcpys, and forces a new transient descriptor set alloc+write+bind
(vulkan_command_processor.cc:6613-6740, 6920-6958) because the buffer offset moved. Replace with
ONE persistent per-type UMA ring arena written in place + a single descriptor set using
`UNIFORM_BUFFER_DYNAMIC` so a per-draw constant change = a 4-byte `pDynamicOffsets` bind (no set
alloc, no vkUpdateDescriptorSets). Then short-circuit the whole UpdateBindings/UpdateDynamicState
block for same-pipeline runs where the existing `current_constant_buffers_up_to_date_` /
`..descriptor_set_values_up_to_date_` bitmasks + `merge_consts_same_` (counted 6551-6556) say
unchanged - emit only index-bind + draw. This is the missing CPU half that makes draw-merge
(R5) finally pay (MDI alone was a wash, docs/research/20260602-mdi-device-ab-result.md).
**MEASURED 2026-06-03: consts_same=367 of 775 same-pipeline draws (~47%) => elision half is
viable.** Guardrail: constants byte-copied verbatim; gate on the guest's own invalidation
bitmasks; assert-mode re-upload+memcmp; respect minUniformBufferOffsetAlignment + ring wrap;
pixel-identical A/B. **Cheapest exp:** cvar that skips the per-draw constant Request+memcpy when
the bitmask says unchanged (no arena yet); log cpu_bind_us + pixel-identical.

### R3 - Single-instruction guest-memory addressing via flat 4GB UMA membase [Rank 3, L, Medium]
(#3, highest single score 4/4.) `ComputeMemoryAddress` (a64_seq_util.h:271-296) does mov + a
branchy large-page fixup (>0x1000) then `AddGuestMemoryOffset` repeats W-reg arith on EVERY
guest load/store. Replace with zero-extend-only: reserve the full 4GB guest space contiguously
in the 16GB UMA at fixed base x21 so an access = `ldr xt,[x21, w0, UXTW]`; handle the large-page
+0x1000 fixup ONCE at map time. Stretch: LDP/STP coalescing, PRFM, ARMv9 MOPS (CPYP/SETP) for
memcpy/memset. Touches nearly every basic block. Impact 3-5% on geometry-heavy games. Guardrail:
exact 32-bit wraparound (UXTW), MMIO stays on the checked path, validate large-page aliases
on-device. Stage behind `arm64_flat_membase_addressing`. **Cheapest exp:** gate ONLY the UXTW
indexed-addressing emission for non-MMIO addrs (<0xE0000000), defer the re-layout; BD on, check
corruption, measure.

### R4 - Frame-persistent precomputed cull/transform cache for BD static geometry [Rank 4, XL, Medium]
(#17.) BD re-submits ~2000 draws/frame, much static world geometry byte-identical frame-to-frame
(only view/proj changes). Cache keyed by (vertex_base, index_addr, vs_hash, vertex-const-hash);
on a hit where only the view changed, reuse the cached CULLED index buffer + re-run cheap
whole-draw frustum rejection via NEON 4xDP4 on cached object-space positions (the
`FastAffineReplay` path, draw_extent_estimator.cc:890-922). Amortizes per-triangle cull (Lever 2,
~51% tris, validated) across frames. Lowers the per-triangle binning floor that draw-merge can't
touch. Guardrail: hook `shared_memory_` write-watch to evict on any guest write; NEON FP32 =
VMX128 1:1 (no divergence); use DEVICE_LOCAL + lazy-copy (NOT HOST_VISIBLE|DEVICE_LOCAL = the
Adreno-TDR memory type). **Cheapest exp (go/no-go): MEASURE REUSE RATE FIRST** - log
(vertex_base,index_addr,vs_hash)+counts over 30s; reuse>40% build, <20% kill.

### R5 - Draw concatenation completion (Lever 2b MDI), unblocked by R2 [Rank 5, M, Medium]
The MDI merge (vulkan_command_processor.cc:4400-4469) device-A/B'd as a WASH because per-guest-draw
CPU still ran and state-command emission broke runs (~4336). R2 removes that blocker, so
concatenation extends. Near-zero alone, MULTIPLICATIVE with R2 - sequence R2 first. Guardrail:
run-break on every pipeline-layout/render-pass break; fingerprint incl vertex_base/endian.
**Cheapest exp:** re-run the MDI A/B with R2's elision cvar ON.

### R6 - Direct-guest-call fast path: PC-relative BL + narrowed stack-sync [Rank 6, L, Medium]
(#1.) `CallDirect` (a64_emitter.cc:5847-5947) does 4x movz/movk + blr per call;
`a64_enable_host_guest_stack_synchronization` defaults TRUE (a64_backend.cc:65) so every call pays
PushStackpoint (~12 ld/st + umull + cmp) + post-return sync. Replace with PC-relative `bl`
(relocation-patched, +-128MB pools, fallback mov+blr); gate the stack-sync to call edges that can
longjmp (HIR-provable r1 mutation); fold first 16 stackpoint levels into a fixed slab. ~1500
calls/frame. Impact marginal by insn-count (~0.17%) + unmeasured BTB relief. Guardrail: default
the sync-narrowing OFF (corrupts setjmp/longjmp on false-negative); only narrow HIR-proven-safe
edges. **Cheapest exp:** instrument CallDirect per-frame counters; build only if calls >50% of
unaccounted CPU.

### R7 - Background/parallel JIT compilation on the idle A715/A710 cluster [Rank 7, XL, Med-High]
(#2.) Translation is single-threaded; a compile spike steals critical-path time -> hitch.
Dispatch cold/predicted-next function translation onto a worker pool pinned to cpu3-6, keep guest
on X3; the indirection table (a64_emitter.cc:5913-5917) is the atomic publish point (CAS).
Frame-PACING benefit, not mean fps. Two hard problems: (1) ARM64 IC IVAU is per-core - must IPI
the X3 to ISB before it runs new code; (2) entry-table STATUS_COMPILING unused in ResolveFunction
-> double-compile race, need interlock. **Cheapest exp:** stopwatch DefineFunction/Translate,
histogram over 1000 frames incl a scene transition; build only if stalls >5ms.

### R8 - Compute-shader EDRAM tile transfers (BIG BET, conditional) [Rank 8, L, Medium]
(#15.) Ownership transfers are graphics quad-draws w/ per-transfer descriptor binds
(PerformTransfersAndResolveClears, vulkan_render_target_cache.cc:4604+), each can force a
render-pass break. Batch a region's transfers into ONE compute dispatch reading source RTs /
`edram_buffer_` UMA mirror before the graphics pass. **HONESTY: B35 measured shipping coalesce
cut render_pass_begins 34% but fps did NOT move - render-pass breaks are real GPU-driver cost but
NOT the BD fps wall.** Hygiene that composes with R2/R5; helps Burnout/BD-boot transfer-heavy
paths; do AFTER R1-R5, do not expect a BD fps move. **Cheapest exp:** micro-bench compute vs
quad-draw copy of a 512x512 region on Burnout menu; proceed if >40% faster.

## Sequencing / dependencies
- R1 unlocks every CPU lever (pointless to optimize per-draw CPU if it runs on an A510).
- R2 unblocks R5 (removes the per-draw state-emit that breaks MDI runs); R2+R5 = the per-draw-cost knockout (each alone ~= wash).
- R3 independent (JIT memory codegen) - parallelizable with R2.
- R4 depends on the reuse-rate measurement; benefits from R3; lowers the binning floor R5 can't touch.
- R6/R7 gate on their measurements; R7 benefits from R1's cluster pinning.
- R8 after R2/R5; not a BD fps lever.

## Critical unknowns (cheap measurements that decide the XL bets)
1. `merge_consts_same_` ratio (gates R2) - **ANSWERED ~47%, R2 viable.**
2. Per-frame static-geometry reuse rate (gates R4 entirely) - reuse>40% build, <20% kill.
3. Call-dispatch fraction of unaccounted CPU (gates R6) - >50% build.
4. Compile-stall histogram (gates R7) - stalls>5ms build.

## Week-1 plan (quick wins, low/zero risk, high information)
- R1 affinity router (minimal cvar: pin GPU-Commands to X3).
- R2-precheck cvar (skip per-draw constant Request+memcpy when bitmask unchanged) - may itself yield single-digit ms.
- R3 UXTW-only flat membase (no re-layout), cvar-gated.
- The 4 go/no-go measurements (instrumentation only).

## Where UMA helps / does NOT
HELPS (eliminates copies): R2 persistent arena, R3 flat reservation, R4 culled-index/cached-position
buffers (DEVICE_LOCAL read-only), R7 worker RWX code.
DOES NOT (it solves copies, NOT CPU<->GPU sync or per-draw iteration): the ~197ms is not a
copy/bandwidth problem - shared-mem staging is already ~8 dirty-tracked copies/frame (<0.1%). So
#10 batch-upload, #11 CRC32 dedup, #12 AHB zero-copy are rejected. **AHB zero-copy (#12) is a
trap** - the HOST_VISIBLE|DEVICE_LOCAL TDR is a driver/MMU fault under sustained TBDR reads, not
an algorithm bug; importing the same RAM via AHB doesn't prove it goes away. Keep UMA-direct OFF,
keep the staging path. Only small read-only validated UMA uses (R2/R4) are safe.

## Rejected (wrong bottleneck / closed lever)
#4 indirect-call inline cache (<0.1ms); #9/#10/#11/#12 shared-mem upload (already dirty-tracked
<0.5%); #7 batched index conversion (Process is 3-5% of IssueDraw, already memoized); #13/#14/#16
RT-ownership map (<0.5%, real cost is the ShaderInterpreter extent replay -> R4); #19 cull
micro-JIT (cull lever already closed - per-triangle cull gave ZERO GPU win on BD, commit
6253c14a7, because Adreno TBDR hardware-rejects off-screen tris; R4 is the only surviving cull
framing); #21/#22/#23 lock sharding/spinlock/doorbell (no measured lock contention on BD - gate
on a "measure contention >5%" experiment, likely only a Lost-Odyssey-class CPU title).

## Bottom line
Week-1 = the 4 cheap measurements + R1 + R2-precheck + R3-UXTW (days, zero/low risk) - they decide
the XL bets. Highest-leverage unknowns: consts-same (DONE ~47%) + static-geometry reuse rate. If
BD re-submits the same per-mesh constants + world geometry each frame, R2+R5 (per-draw CPU
knockout) and R4 (amortized cull) are the path from 4fps toward full speed. If reuse comes back
low, weight to R3 (flat membase, every block) + R6/R7 (JIT throughput). Do NOT spend the loop on
UMA-copy elimination, RT-ownership structures, cull micro-JITs, or lock sharding - all measured
non-levers on BD.
