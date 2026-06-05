# Design scope: JIT spin-loop-yield - the #1 remaining cross-game CPU lever

The biggest remaining CPU cost on CPU-bound Thor titles is a guest user-mode SPIN-WAIT, not
compute or our codegen. This doc scopes a safe fix. Evidence: docs/research/20260604-gears2-
cpu-profile-lock-contention.md + the LO profile.

## The finding (measured)
- Gears 2 top CPU: guest_82977688 (~15%) + guest_8298C2A0 (~10%). Lost Odyssey top CPU:
  guest_827B6278 (~12%). DISASSEMBLED: all three are the SAME Xbox 360 XDK synchronization
  primitive (spin-then-block lock/critical-section) - identical NOP-spin (8x `or r31,r31,r31` x4),
  identical 0x1388 (5000) tick timeout, identical struct offsets. Two different engines (UE3 Gears,
  Mistwalker LO) share it => XDK code. ~12-25% of CPU per title.
- It is a USER-MODE spin: the routine spins polling a flag/lock for up to 5000 ticks, only calling
  the kernel block path AFTER the timeout. We run that user-mode spin at full JIT speed.

## What is ALREADY efficient (ruled out - do NOT chase these)
- Guest BLOCKING waits already block the host thread: XObject::Wait / WaitAny / WaitAll
  (xobject.cc:207-308) call xe::threading::Wait/WaitAny/WaitAll which block on host condition
  variables (threading_posix.cc PosixCondition). The MaybeYield() calls there are single courtesy
  yields on the kTimeout return path, NOT spin-poll loops. So there is NO "make the wait block" win.
- NtYieldExecution -> MaybeYield(sched_yield) is wired (threading_posix.cc:141).
- The `or r31,r31,r31` NOPs are already elided to zero HIR ops; they are delay-padding, and
  r31/r27 are NOT the standard PPC priority-nop registers (those are or 1/2/3/6,...), so honoring
  priority-nops would NOT catch this specific spin (but could help other spin code - low value).

## So the only lever is JIT idle/spin-loop DETECTION + a host yield during the user-mode spin.

### Detection options (ranked by safety/overhead)
1. **Clock-read-frequency heuristic (RECOMMENDED).** These XDK spins are TIMING spins: each spin
   iteration reads the timebase (mftb -> LOAD_CLOCK, or the kernel tick) to check the 5000-tick
   timeout. Add a per-guest-thread counter incremented in LOAD_CLOCK (the a64 sequence + the x64
   one); when a thread does >= N (e.g. 64-256) LOAD_CLOCKs within a short wall-clock window without
   an intervening guest blocking-wait/IO, call xe::threading::MaybeYield() (sched_yield) on, say,
   every Kth read. Targeted to exactly the timing-spin pattern; overhead is one counter + compare
   in LOAD_CLOCK (already a CallNative, so cheap relative). Reset the counter on any guest
   blocking-wait (XObject::Wait) and periodically. Gate cvar (default off) + measure.
2. **Backward-branch idle detection.** Detect a loop that branches backward to a recently-executed
   PC with NO memory stores except a single timestamp slot, and reads the clock -> spin -> yield.
   More general but needs per-backward-branch JIT instrumentation = overhead on ALL loops; higher
   risk of false positives slowing real loops. Defer unless (1) is insufficient.
3. **Exact XDK-signature detection.** Recognize the specific spin routine shape at translation and
   emit a yield. Fragile (game/SDK-version-specific addresses + constants). Not recommended.

### Correctness / safety
- Yielding inside a spin is SEMANTICALLY SAFE: the spin has no side effects beyond a timestamp; a
  host sched_yield only changes TIMING (lets the lock-holder/producer run), then the spin re-checks
  its condition. No deadlock risk (yield never blocks indefinitely; the guest's own timeout still
  fires). The guest's perceived elapsed time is unaffected (the cntvct/mftb clock keeps advancing).
- The ONLY risk is FALSE POSITIVES: yielding in a hot NON-spin loop that happens to read the clock
  a lot -> could add latency. Mitigate: require a HIGH read-count threshold + no recent IO/store-
  to-non-timestamp + cvar gate + measure that fps does not regress.
- Interaction with the prime-core thread router (XeniaOptimizations): yielding helps most when the
  lock-holder is ready-to-run on a contended core; with good affinity it may help less. Measure
  with the router both on and off.

## Validation plan (once implemented, cvar-gated)
Re-profile Gears 2 / LO with simpleperf --app + the JIT perf-map (memory thor-cpu-profiling-
simpleperf): the spin functions' self % should DROP and [kernel] futex/sched may shift; confirm
fps does NOT regress (the spin yielding should let the awaited work complete sooner, ideally
RAISING fps on the multi-threaded titles). Thermal-safe short records (9s warm + 12s record).

## RESOLVED 2026-06-05: the spins read a KERNEL TICK from MEMORY, not mftb -> option 1 is OUT.
Re-reading the disasm: the per-iteration spin CONDITION reads a kernel tick value from guest MEMORY
(guest_827B6278: r30 = [[KPCR r13 +0x100]+0x58]; Gears guest_8298C2A0's loop: r9/r10 = [r31+0x2A90]/
[r31+0x2A9C]) - a plain lwz. The `mftb` reads in these functions are ONE-TIME baselines BEFORE the
loop, NOT per-iteration. So instrumenting LOAD_CLOCK (mftb) would NOT see the spin's hot reads ->
**option 1 (clock-read-frequency yield) is INVALIDATED; do not implement it (it would not catch the
spin).** The correct approach is option 2 (backward-branch idle detection, clock-source-agnostic) -
a real JIT change: instrument backward-taken branches whose loop body did no store to non-stack
memory (a pure poll loop) + yield after N. Substantial; needs a dedicated effort + device A/B. (Also
possible: the polled kernel-tick's update granularity in our emulation - if coarse, the spin lasts
longer; worth checking KeTickCount/interrupt-time update rate, but the spin still busy-waits its
timeout regardless.)

## (superseded) original open question: which clock does the spin read?
Option 1 only catches spins that read mftb (-> LOAD_CLOCK). But the disasm shows the spins read
TWO different sources: Gears guest_8298C2A0 (the OUTER loop) uses `mftb` directly (-> LOAD_CLOCK,
catchable by option 1), while the INNER poll guest_82977688/827B6278 compares a timestamp to a
KERNEL TICK COUNT read from guest memory ([[KPCR r13 +0x100]+0x58]) - a plain lwz, NOT mftb, so
option 1 would NOT see it. Net: option 1 catches the spin ONLY IF the outer driving loop uses mftb
(true for Gears 2; UNVERIFIED for LO - disasm LO's caller of guest_827B6278). To resolve: disasm
the OUTER callers and confirm the loop's clock source. If they use the kernel tick (not mftb),
either (a) ALSO instrument the kernel-tick-update / the KeQueryPerformanceCounter-style read path,
or (b) favor option 2 (backward-branch idle detection), which is clock-source-agnostic.

## Recommendation
First disasm the outer spin-driver loops (Gears guest_8298C2A0 caller, LO guest_827B6278 caller) to
confirm the clock source, THEN implement the matching detection cvar-gated: option 1 (clock-read-
frequency yield, lowest overhead) if mftb-driven, else option 2 (backward-branch idle detection,
clock-source-agnostic). Yielding is semantically safe; the risk is false positives (gate + measure
fps does not regress). This is the next big CPU lever for the spin-wait-bound titles (Gears 1/2/3,
LO) after the shipped cntvct clock win - but it needs a dedicated, careful implementation + device
A/B, not a rushed change.
