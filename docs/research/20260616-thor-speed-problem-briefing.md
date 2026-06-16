# Thor speed problem — briefing for external-model "hard rethink" consultation

Payload for the `consult-hard` skill (Codex / Gemini). Goal: get a from-first-principles
rethink of **where the next big speed win is** for Xbox-360 emulation on the AYN Thor, given
that the obvious GPU and CPU levers are mostly mapped (and several are proven dead). Everything
below is device-measured fact unless marked hypothesis. Do NOT re-propose anything in
"Walls / proven dead" without a concrete reason it was mis-measured.

## Target
- xenia (Xbox 360 emulator) fork, Android, on **AYN Thor**: Snapdragon 8 Gen 2 (QCS8550),
  **Adreno 740** (tile-based deferred renderer / TBDR, Vulkan 1.3), **~16 GB UMA**, **8 ARMv9 cores**
  (1× X3 @3.19, 2× A715 + 2× A710 @2.8, 3× A510 @2.0). NEON only (no SVE). i8mm/bf16/dotprod
  present but only safe for internal heuristics, never guest FP32 geometry.
- Guest CPU: PowerPC **VMX128**. Emulation = PPC→HIR→ARM64 JIT (lazy, on the executing thread).
- Priority titles: **Blue Dragon** (GPU-bound), **Burnout Revenge** (CPU-bound), **Lost Odyssey**
  (boots but renders black — guest-logic stall), **Banjo** (false dirty-disc verify).

## Current perf (device-measured, post the big fence fix)
- **THE shipped win:** Turnip/KGSL blocking-fence-poll fix (`vulkan_lazy_completion_polls`,
  default-on). `vkGetFenceStatus` on Turnip/KGSL was BLOCKING until GPU completion (Mesa kgsl
  WAITTIMESTAMP ioctl with timeout=0, which the kernel treats as "wait forever"). Fixing it:
  **Burnout +46% (12.2→15.2 full-stack), BTTF +78%, Blue Dragon +30% (5.9→7.9), Gears +64%.**
  Four titles, pixel-correct. This was by far the biggest single win — i.e. the bottleneck was
  **CPU↔GPU sync serialization**, not raw GPU or raw CPU.
- After that fix: **Blue Dragon ~7.9 fps** (at its GPU floor), **Burnout ~15 fps** (now CPU-bound
  on the guest Main XThread, 99% on one core).

## Where the time goes now (per-title, measured)
- **Blue Dragon (GPU-bound):** ~1100–2180 tiny draws/frame, ~263k verts/frame. The cost is the
  Adreno **binning pass running the position vertex-shader per-vertex-per-draw** before cull.
  Elimination chain proved the ~12.7ms pre-main-pass drain is **irreducible per-vertex binning
  COMPUTE** (379k × ~32ns), NOT transfers/load-store/barriers/fetch-bandwidth. Levers that worked:
  fence fix (above). Levers bounded/flat: per-draw merge (shape-level format churn blocks it),
  binning de-interleave (flat: it's compute not bandwidth), EDRAM transfer restructuring (fps-neutral
  on heavy scenes).
- **Burnout (CPU-bound):** the guest **Main XThread is pegged 99%** on one core executing JIT'd
  game logic (an entity-traversal hot loop that calls a real non-leaf function per element doing
  real work). There is also a hypothesized ~18ms per-frame "pipeline bubble" (unlocalized).
- **Lost Odyssey:** boots, runs uncapped (~170fps) but renders BLACK — a deep guest-code spin at a
  multicore idle barrier waiting on a GPU event xenia isn't producing. Not a render-layer bug.

## Walls / proven dead (do not re-propose without disproving the measurement)
1. **Cross-barrier guest-state elision (register/CR/state across guest CALLs)** — fundamentally
   unsafe; guest functions share one PPCContext. Two independent attempts collapsed (silent stack
   corruption; BD crash). The only safe call-elimination is INLINING the leaf — built, but fps-FLAT
   and misses Burnout's hot non-leaf call.
2. **CPU codegen micro-ops** (rlwinm/CR/vsplt/vand fast-paths) — shipped, correct, but ~−2.6%
   guest-CPU aggregate and **fps-flat** on GPU/JIT-bound scenes. The CPU codegen track is considered
   EXHAUSTED for the priority CPU-bound title: Burnout is bottlenecked on *real* guest function-call
   work, not codegen quality.
3. **Concurrent-with-guest parallel JIT** — DEADLOCKS on xenia's recursive global lock + lazy-compile
   cycle (device-proven). Only safe as a load-window pre-warm (shipped, default-off, not a proven win).
4. **GPU post-bin cull / vertex reduction / MDI / instancing / mesh / pre-transform** — all proven
   ~−0.1% (Adreno bins per-vertex-per-draw *before* cull, so reducing post-bin work is zero win).
5. **LLVM JIT backend** — rejected (RPCS3 data: ~40% slower than a hand JIT).
6. **UMA zero-copy / sparse shared-memory direct** — present-hangs; `VK_EXT_external_memory_host`
   absent on this driver. Held off.

## The hard questions for the external model
1. Given BD is floored on **irreducible per-vertex binning compute** and the Adreno bins the full
   submitted vertex set before any cull: is there ANY way to reduce the vertices that reach the
   binner, short of a per-game LOD/cull game-patch? (e.g. a cheaper binning-position shader variant;
   exploiting the TBDR/GMEM differently; a fundamentally different way to emulate the 360's EDRAM/
   predicated tiling that submits less geometry?)
2. Burnout is single-guest-thread CPU-bound on real game logic. The 360 game itself is mostly
   single-threaded hot. Is there a *legitimate* speedup — better hot-loop codegen we haven't tried,
   speculative/parallel execution of independent guest work, value/trace specialization, or a 2nd
   optimizing JIT tier — that survives the shared-PPCContext + recursive-global-lock constraints?
3. Are we wrong that "fps lives in GPU/driver/sync, not CPU"? The single biggest win was a *sync*
   fix. Is there another whole class of **CPU↔GPU serialization / driver / scheduling** bug like the
   fence one still hiding (present path, command submission, descriptor/pipeline, thread placement on
   the heterogeneous cores)?
4. What would a *ground-up reimagination* of this emulator for this exact SoC (Adreno 740 TBDR + UMA
   + 8 heterogeneous ARMv9 cores) do differently that the current xenia architecture cannot?

The user's standing directive: "rearch xenia gpu cpu stuff to take advantage of the Thor hardware
and get all games working full speed; large reimagination okay."
