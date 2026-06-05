# ARM64 cntvct_el0 timebase: a validated cross-game CPU win (clock_gettime 14.76% -> 0.77%)

Commit 45f19b2b1 (src/xenia/base/clock_posix.cc). Found + validated 2026-06-05 via the
simpleperf --app + JIT-perf-map pipeline (memory thor-cpu-profiling-simpleperf).

## The find
A Lost Odyssey CPU profile (in-engine, 56% JIT guest code) showed **14.76% of CPU in
[vdso] clock_gettime**. Root: the guest `mftb` (move-from-timebase) instruction lowers to
HIR `load_clock` -> a64 LOAD_CLOCK (CallNative LoadClock) -> Clock::QueryGuestTickCount ->
QueryHostTickCount -> `Clock::host_tick_count_platform()` which called
`clock_gettime(CLOCK_MONOTONIC_RAW)`. Games read the timebase constantly (frame pacing,
timers, spin-waits - e.g. the Gears 2 spin-wait), so that vdso call is a hot cross-game cost.

## The fix
On ARM64 the same monotonic source is the architected generic timer, readable from EL0
directly: `cntvct_el0` (count) + `cntfrq_el0` (frequency), one `mrs` instruction each, no
vdso/syscall. clock_posix.cc host_tick_count_platform()/host_tick_frequency_platform() now
read those registers under `#if XE_ARCH_ARM64` (count + frequency switch together to stay on
one consistent source; cntfrq_el0 is mandated on ARMv8). x86-64/other POSIX keep clock_gettime.
Compile-time hard-switch (always-on, no cvar): a runtime toggle is unsafe here because the
frequency is captured at static init before cvar parse, and it's a strict-improvement infra
change (same time source, cheaper read), not an accuracy/speed tradeoff.

## Validation (device, deployed build)
- CORRECTNESS: LO boots + renders + presents (6874 draw/VdSwap log lines), no crash/abort/SIGSEGV
  -> the register-read clock did not break guest timing.
- PERF (MEASURED): new build [vdso] = **0.77%** (lo3.data) vs **14.76%** baseline (lo.data) =
  the clock_gettime vdso call is eliminated. ~14% of CPU reclaimed on timing-heavy LO. The
  residual 0.77% is non-mftb clock_gettime.
- Note: the CallNative(LoadClock) + QueryGuestTickCount (tick_mutex_ + scaling) overhead REMAINS
  (now in libxenia-app.so, not vdso); a future win could inline the cntvct read into the JIT for
  mftb and/or drop the tick_mutex_ on the hot path. But the expensive syscall is gone.

## Reusable lesson
The simpleperf-->perf-map pipeline finds RUNTIME hot paths (not just codegen targets): a dso-level
split ([vdso]/[kernel]/libxenia/JIT) immediately flags syscall-heavy costs. Thermal-safe measure
for CPU-hot titles = 9s warm + 12s record (peaked 45.9C; the old 100s warm hit 72.9C).
