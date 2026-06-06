# Spin-loop-yield CPU lever — generalization scoping (2026-06-06)

**Goal:** the #1 CPU lever for CPU-bound titles (Lost Odyssey, Gears of War 2). Deep
profiling ([[thor-cpu-profiling-simpleperf]], [[thor-game-state]]) found both titles spend
~12-25% of CPU in a SHARED Xbox 360 XDK spin-wait/critical-section primitive: LO
`guest_827B6278` (~12.32%) and Gears2 `guest_82977688` (~14.79%) are STRUCTURALLY IDENTICAL
(8x `or r31,r31,r31` NOP-delay spacer x4, a `0x2ABD` flag check, a `0x1388`/5000-tick mftb
timeout, a backward branch). The thread busy-spins this loop instead of yielding, so the
lock-HOLDER thread is starved. Making the spinner yield its host core lets the holder run →
the condition resolves sooner → fps up. Scoping doc only (no code).

## The concept is ALREADY PROVEN on this fork (+27% fps), but TITLE-SPECIFIC
There is a working, title-specific precedent: the **Blue Dragon draw-wait fastpath**
(`a64_emitter.cc` ~3060, cvar `arm64_blue_dragon_draw_wait_fastpath`, default-on). It
recognizes BD's draw-wait spin by HARDCODED guest address (`0x8246B408`, caller `0x8246E618`;
also `0x827294CC`), replaces the body with a fastpath that polls the predicate and calls
`YieldBlueDragonDrawWaitFastpath()` every N=16 wait-return hits (cvar
`..._native_yield_stride`); that helper `xe::threading::Sleep(~100us)` or `MaybeYield()`
(cvars `..._native_sleep_us`, ...). **Comment records "+27% fps" measured on BD.** So the
lever WORKS; it is just hardcoded to BD's addresses + BD's predicate shape, so LO/Gears2
(different addresses + predicate details) get nothing.

## Key mechanism facts (confirmed this session)
- mftb (`OPCODE_LOAD_CLOCK`) on a64 is STILL a `CallNative(Clock::QueryGuestTickCount)`
  (`a64_seq_memory.cc:720`) - the cntvct fix (45f19b2b1) is INSIDE QueryGuestTickCount, not
  inlined at the JIT site. So there IS a single hook point hit by every mftb poll.
- The yield helper infra exists: `xe::threading::MaybeYield()` + `Sleep(us)` (used by the BD
  fastpath, audio, NtYieldExecution). The guest `NtYieldExecution` (xboxkrnl 0x101) is also
  implemented - but the XDK spin primitive does NOT call it; it busy-polls mftb.
- Detection precedent iterates blocks/instrs at emit time (`a64_emitter.cc` ~3081 audit loop).

## Approaches (ranked)

### B. mftb-poll-rate yield in the LOAD_CLOCK native call (SIMPLEST, RECOMMENDED FIRST)
Every XDK spin polls mftb in a tight loop. Add per-guest-thread poll-rate tracking to the
`QueryGuestTickCount` path (or a thin LOAD_CLOCK wrapper): if a thread issues K consecutive
mftb reads with no intervening "real work" / from a tight PC window within a small wall-clock
budget, treat it as a clock-spin and `MaybeYield()`/`Sleep(small)`. ONE hook, title-agnostic,
catches ALL mftb busy-spins (LO, Gears2, others) with no per-title addresses and no body
rewrite (guest logic unchanged - yield is a scheduling hint).
- Win: generalizes the proven +27% BD result to all clock-spin titles.
- Risk: false-positives on legit timing-heavy code that polls mftb fast but isn't spinning ->
  over-yield/perf loss. Mitigate with a HIGH consecutive-read threshold + same-PC gating +
  cvar-tunable stride/sleep (mirror the BD fastpath's stride=16/sleep=100us defaults).
- Validate: cvar default-OFF; device A/B on LO + Gears2 (CPU-bound) at a matched scene -
  per-thread `top` (spinner core no longer pegged) + fps delta + correctness (READ png, no
  hang/regression). These titles are thermally hot - SHORT capture, the now-fixed hot-capture
  (a7523a82f) survives the 64C watchdog.

### A. Structural pattern-detect the XDK spin (precise, FOLLOW-ON)
At emit time, recognize a function/block matching the XDK signature (the 8x `or rN,rN,rN`
NOP idiom + flag-poll + mftb-timeout backward-branch, no stores/calls besides the poll) and
inject the yield-helper call (do NOT rewrite the body). More precise (no mftb-rate false
positives) but needs pattern-matching in the analyzer; do only if B over-yields.

### (Rejected) just add LO/Gears2 addresses to the BD fastpath list
The fastpath REPLACES the body with BD's predicate-specific impl; LO/Gears2 predicates differ,
so address-listing alone won't apply. Not viable without per-title fastpath bodies.

## Recommended staged plan
1. Approach B (mftb-poll-rate yield), cvar `a64_clock_spin_yield` default-off, stride+sleep
   cvars mirroring the BD fastpath. Build + host-verify it doesn't break cpu-tests.
2. Device A/B on LO + Gears2 (SHORT, hot-capture): confirm spinner-core unpegged + fps up +
   no correctness regression at matched scene. If win, default-on + a stacking
   XeniaOptimizations toggle (CLAUDE.md mission).
3. If B over-yields legit code, add Approach A structural gating.
4. The existing BD draw-wait fastpath stays (already default-on, +27%); B is the cross-title
   generalization, they compose.

## Notes
- Guest-visible-timing-adjacent (changes scheduling, not results) - cvar-gate + device-validate
  at matched scene; never ship blind. NOT a single-cycle task: a dedicated build + 2 device A/B
  fires (LO, Gears2). This doc is step 0.
