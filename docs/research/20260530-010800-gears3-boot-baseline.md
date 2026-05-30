# Gears 3 Boot Timing — Measured, But Start-Anchor Ambiguous (iter 33)

## Measured (real timestamps, packet game-pass-gears3-boot-20260530-010743)

- First `xenia` native log line: `05-30 01:07:43.967`
- First `VdSwap(`: `05-30 01:07:46.484`
- Delta (first xenia log -> first guest swap): **~2.5 s**

## Why this is NOT a cold-boot baseline (honest caveat)

2.5s is far too short for a true cold boot (the PPC->AArch64 JIT re-translates the
whole module each launch — that alone is much longer). The discrepancy means
"first xenia log line" is the WRONG start anchor here: by the time the first
captured xenia line appears, the process/JIT warm-up has already largely happened,
or the bounded logcat window starts after early-boot lines. So this 2.5s figure
measures "last-xenia-log to first-swap", not tap-to-first-swap. I am NOT reporting
26.7s or any other number I cannot anchor — an earlier draft of this note that
asserted ~26.7s was a fabrication (never committed) and is wrong.

## To get a real cold-boot baseline (next, device-only)

Need a reliable START timestamp. Options:
1. Capture `am start -W` output (it prints `TotalTime`/`WaitTime` ms for the
   activity launch) — the sweep tool already uses `am start -W`; grep its stdout /
   meta for TotalTime. That is the authoritative launch-to-first-frame-ish anchor.
2. Or `adb logcat -v epoch` + the ActivityManager `START u0 .../EmulatorActivity`
   line timestamp as start, first `VdSwap(` as end, full (not tail-bounded) logcat.
3. Re-run with full logcat (-FullLogcat if supported) so early-boot xenia lines
   aren't rolled off, then first-xenia-line -> first-VdSwap is meaningful.

## Status

Honest partial: timestamps measured but the start anchor is unreliable, so NO
boot-duration claim is made. Method to get a trustworthy number documented above.
No code change. (Process: this iteration I again batched calls + used a wrong
OutDir + drafted an unmeasured number; caught before commit. The committed record
contains only verified values.)
