# ThorPack kill-test — the executable protocol (the "Ghidra super-hot AOT" bet, gated)

Date 2026-06-18. The user asked: "why can't we use ghidra to examine code and create super hot faster
code type stuff." That is exactly **ThorPack** — a per-title, profile/Ghidra-guided direct PPC→A64
trace-specializer that bypasses HIR for the hottest guest blocks. This doc turns the one-line kill-test
from the reconciled-rearch doc into an executable protocol. **Do NOT build the backend before this gate
passes** — blind static-RE already produced a red-team-caught false positive this project (the bogus
"foliage draw builder"), and three independent analyses rate ThorPack a BOUNDED bet (~1.3-1.6× best case
on CPU-bound titles; useless on GPU-bound BD). The kill-test exists to find out if even that is reachable
before we spend months.

## The honest hypothesis being tested
Our HIR JIT pays per-guest-block overhead (PPCContext register round-trips to memory, conservative
flag/CR materialization, no cross-block value forwarding) that a hand-tuned direct A64 translation of a
specific hot function could remove. **If a hand-written direct translation of Burnout's #1 hot function's
20-50 hot blocks is <15-25% faster than what our JIT already emits for them, ThorPack is DEAD** (no
headroom — the JIT is already near-optimal for branchy integer code, which matches our codegen-quality
priors). ≥25% on a real hot fn = the bet is live and we build the tiered specializer.

## Step 0 (DEVICE-GATED) — pin Burnout's actual hot function. DO NOT GUESS.
Memory says Burnout is CPU-bound on the guest Main XThread and its #1 hot fn is branchy integer/CR
game-logic (cmplw/beq/bne/subf, zero vector ops), but the exact guest PC is NOT recorded. Get it:
1. Build with `cpu_emit_jit_perf_map=true` (guest-PC → host-addr map for symbolization).
2. On a thermal-gated device fire, reach steady Burnout gameplay (movie-free, per RULE 0), run
   `simpleperf record --app jp.xenia.emulator.github.debug -g --duration 20` (non-root `--app` works,
   per [[thor-cpu-profiling-simpleperf]]).
3. Offline-symbolize with the unstripped .so + the perf-map → the top guest PC(s). Record the guest
   function entry address. This is the ONLY device step; everything below is device-free.

## Step 1 (device-free) — Ghidra-disassemble the hot fn from the extracted XEX
We already have `scratch/ghidra/guest/burnout/default.xex`. Load as `PowerPC:BE:32` @ `0x82000000`
(skill `xenia-thor-ghidra-game-patch`). Disassemble the hot fn from Step 0's entry. Capture: block
count, the 20-50 hottest blocks (the inner loop), the live guest registers/CR fields across them, every
guard/exit (calls, indirect branches, memory ops that must keep guest semantics).

## Step 2 (device-free) — measure what our JIT ALREADY emits (the baseline = the denominator)
Two ways, do both:
- **Headroom-by-inspection:** dump the a64 the JIT emits for the hot fn (a64 backend disasm). Count, per
  hot block: context loads/stores (PPCContext round-trips), redundant flag/CR computation, missed fusion.
  This localizes the *kind* of waste a direct backend would remove. If there is little (tight code),
  stop — ThorPack is dead by inspection.
- **Microbenchmark baseline:** in the host cpu-tests harness (`src/xenia/cpu/testing/`, x64 AND qemu-a64),
  reconstruct the hot fn's inner loop as a `TestFunction` (or replay the real XEX bytes via the
  test_module guest-code path), call it N×10⁴ in a tight loop, and time it. This is the JIT denominator.

## Step 3 (device-free) — hand-write the direct A64 translation of the hot blocks
Hand-emit optimal A64 for the 20-50 hot blocks: keep hot guest regs in host regs across the whole trace
(no per-block context spill), materialize only the CR fields actually consumed, fuse compare+branch,
**HIR-JIT fallback at EVERY guard** (call/indirect/uncertain memory) so it is always correctness-safe.
Time the same N×10⁴ loop.

## Step 4 — the verdict + differential safety
- **Speed:** `direct_time / jit_time`. <0.75-0.85× of JIT (i.e. ≥15-25% faster) on a REAL hot fn = LIVE.
  Otherwise DEAD — record it and stop (do not re-chase; this is the same "near-ceiling" wall).
- **Correctness (mandatory regardless):** diff full guest state (all GPR/CR/XER/memory touched) between
  the JIT and the direct version over ≥10⁴ randomized-input calls. ANY divergence = the direct path is
  unsound → fix or abandon. This is the same differential discipline that validated the known-bits fold.

## If LIVE → the build shape (NON-blocking, threads the deadlock wall by construction)
Per Codex #1-2: background cores consume IMMUTABLE trace records → candidate direct superblocks in
PRIVATE arenas → a single publisher atomically swaps block entries at safe points. NO worker ever takes
the recursive global lock (that is what DEADLOCKED the parallel-JIT attempt, [[parallel-jit-precompiler-wall]]).
The [[burnout-frame-serialization]] lock-free `EntryTable::Get` lookup shipped this session is the
prerequisite read-side for that publication. Ship as a default-off `XeniaOptimizations` toggle, per-title.

## Why this is the gate, not a punt
"Gate before build" is the project's hard-won discipline (it killed the FSI compute-ROP, the vectorized-
3-PPE, the per-draw-merge-on-BD, and the static-RE foliage false positive — all plausible, all dead on
device data). ThorPack is the single most expensive idea on the board; the kill-test is one device fire
+ a few device-free days, and it either unlocks the bold CPU bet or saves months. Sources:
docs/research/20260618-deep-reimagination-reconciled.md, scratch/consult/rearch/codex.md.
