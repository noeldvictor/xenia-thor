# Priority-title FPS ceiling: empirical lever exhaustion + the genuine multi-month roadmap (2026-06-13)

Synthesis of an exhaustive session that DEVICE-TESTED every quick/medium fps lever for the priority
titles and found them dead or already shipped. Purpose: give a clear, honest basis for deciding the
multi-month investment, and stop future sessions re-burning effort on closed levers.

## Per-title bottleneck (measured, this session)

- **Burnout Revenge (race) = 6.7 fps, CPU-bound on per-draw IssueDraw.** Heavy race: gpu_frame_us=45ms
  (22fps GPU ceiling) but cpu_issuedraw_us=93ms (~47ms real work + ~46ms GPU-fence wait) for **2175
  draws ~= 21us/draw** on the single GPU-command (cp-worker) thread. The draws are IRREDUCIBLE by
  batching (per-mesh strips, different vertex FORMATS per draw -> draw-concat AND multiDrawIndirect both
  can't merge them). So the race is bound on a per-draw IssueDraw floor + serial command processing.
  (NOTE: post-fence-fix the guest entity loop is only ~20ms of a separate thread; IssueDraw dominates.)
- **Blue Dragon (heavy vista) = ~6-8 fps, GPU-bound on the Adreno binning HARDWARE floor.** gpu_frame
  ~128ms is irreducible per-vertex binning compute (de-interleave lever measured FLAT; per-triangle cull
  net-neutral). CPU mostly idle (waits on the GPU fence). Not software-movable.
- **BTTF = ~22-30fps** (fence fix + prime-core router hit its 30fps target on the DeLorean scene).
- The biggest win the project ever landed was a **GPU DRIVER/SYNC fix** (Turnip/KGSL blocking fence,
  +46-78% across 4 titles), NOT codegen or shaders.

## Empirically-closed lever ledger (DO NOT re-test)

| Lever | Result | Why |
|---|---|---|
| JIT leaf-inline | FLAT on Burnout race | optimizes guest game-logic; race is IssueDraw-bound (wrong thread). Crash-fixed + BD/Burnout pixel-correct, but indiscriminate (cold-call code growth). |
| Cross-call register/CR preservation | UNSAFE | the context_barrier at every guest CALL exists because guest fns share the PPCContext; eliding state across it corrupts on longjmp/import (red-team caught silent stack corruption). |
| Whole-draw offscreen cull | -38% NET LOSS (device) | Burnout already CPU-culls -> 0 draws dropped; per-vertex extent computation added ~235ms to the bottlenecked thread. |
| draw-concat / MDI batching | DEAD on Burnout | per-mesh strips, different vertex formats -> nothing to merge. |
| gpu_edram_passes_dont_care_safe | INERT | trigger never fires (titles clear via resolve-clears, not pass-opening rect draws). |
| frames-in-flight raise | not the constraint | cap=3, scene uses 2; lazy-polls fix already captured the pipelining win. |
| BD de-interleave binning / per-tri cull | FLAT / net-neutral | binning is irreducible per-vertex HW compute. |
| Parallel IssueDraw (this session) | BLOCKED (architecture) | the DeferredCommandBuffer records the guest stream SERIALLY; each draw's state depends on cumulative register writes, so draws can't be recorded in parallel without a major two-pass rearch. |

Already SHIPPED + helping (these took Burnout ~12->15fps): Turnip fence fix, gate_rt_update (+34%),
dynamic constants arena, push descriptors, prime-core router, hoist-residency-lock, persistent pipeline
cache. Those are the easy/medium wins, banked.

## The honest goal assessment

The goal ("rearch ... exploit NEON/ARMv9/GPU ... all games full speed ... large reimagination") has a
**structural mismatch**: the hardware it wants to exploit (NEON/GPU) is NOT where the priority titles are
bottlenecked. Burnout is bound on CPU draw-issue overhead + a serial command stream; BD is at the Adreno
binning HARDWARE floor (irreducible); the per-draw state derivation is serial. A NEON or GPU-shader
rearchitecture, however large, does not move these specific bottlenecks. This is why every codegen/SIMD/
GPU lever came back flat or dead.

## The genuine remaining paths (all multi-day to multi-month; ranked by EV)

1. **Burnout traffic-density game-patch** (highest EV, targets the REAL bottleneck = draw count at the
   source). Cuts entities -> fewer loop iterations AND fewer of the 2175 draws -> less IssueDraw + less
   GPU. BLOCKED on tooling: needs Ghidra (NOT installed) to RE Burnout's XEX + find the density param.
   First unit = install Ghidra, extract+decompress the XEX (gdfx_extract + the game-patch skill), load
   PowerPC:BE:32 @ 0x82000000, find the traffic/entity-count logic. Caveat: alters gameplay (fewer cars).
2. **DeferredCommandBuffer two-pass parallel rearch** (large reimagination, exploits the 8 cores). Pass 1
   serially derives per-draw state into a list (cheap register tracking); pass 2 records draws in parallel
   across cores via Vulkan secondary command buffers. Targets the 47ms IssueDraw real work. HIGH risk
   (correctness of parallel rendering) + uncertain (pass-1 state derivation may dominate); needs a
   per-thread profile first to confirm IssueDraw_real is on the critical path (vs the GPU-fence wait).
3. **NEON-vectorize guest math for guest-logic-bound titles** (LO, non-vista RPG scenes) - the hook's
   NEON ask, but only helps titles bound on guest math (NOT Burnout's race). Needs RE of the hot guest
   computation + a JIT SIMD fast-path; guest-precision-exact (per the approx-math rule).
4. **Static recomp** - rejected (CPU-only win, we're not CPU-codegen-bound where it'd help; months/title).

## Recommendation

There is no per-session fps win left for these titles. The most bounded next investment is **#1 (the
Burnout game-patch)** - it targets the one bottleneck localized this session and is data, not a risky
rearch - but it requires standing up Ghidra and a multi-session RE grind. #2 is the true "large
reimagination" but is high-risk and should be gated by a per-thread profile first. Do NOT spend more on
codegen micro-opts or GPU culling for these titles - they are empirically exhausted.

Durable value delivered this session (not fps, but real): the inline-leaf crash fix, a prevented
memory-watch corruption (red-team), the cross-barrier-elision architectural verdict, a reusable JIT
host->guest perf-map + inline analyzer, and this fully device-tested lever map.
