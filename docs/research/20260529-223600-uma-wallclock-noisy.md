# UMA Wall-Clock A/B — Noisy / Inconclusive (autonomous iter 17)

## Attempt

Tried to convert the proven UMA copy-bytes win into a frame-time number on an
uncapped scene (Lost Odyssey, ~170 fps, deterministic launch), comparing
per-frame perf-counter timings off vs on.

| metric (us/frame) | OFF | ON |
|-------------------|-----|----|
| queue_submit_us | 25.9 | 23.1 |
| present_submit_us | 51.7 | 67.3 |
| present_us | 44.7 | 2201.8 (!) |
| staging_copies | 11117 | 0 |
| direct_writes | 0 | 20471 |

UMA confirmed engaged (memory type 6, host-coherent), staging copies -> 0 as
expected.

## Why it's inconclusive

The two arms' snapshots were taken at different cumulative frame counts (OFF at
issue_swaps~9330, ON at ~4500), and `present_us/frame` differs ~50x (2202 vs 45),
which is physically implausible at 170 fps. That means the timing accumulators
caught different states — the ON capture likely included an init/stall spike, and
the black-screen present path on this title is erratic. So these per-frame timing
deltas are NOT a reliable wall-clock measurement.

The only directionally-clean signal is `queue_submit_us/frame` 25.9 -> 23.1
(~11% less submit time), consistent with the UMA path removing transfer-queue copy
work — but on this noise level it is suggestive, not proof.

## Honest status

- SOLID (committed, ce90899b9): UMA removes the staging copies entirely — 11117
  copies -> 0, ~49 MB staging traffic -> 0 on this Lost Odyssey capture (and
  16780 -> 0, ~693 MB on the earlier Gears menu capture). That copy-elimination is
  the real, cap-independent UMA win.
- NOT ESTABLISHED: a clean frame-time/FPS speedup number. The instantaneous
  perf-counter timing snapshots are too noisy across arms, and the available
  rendering scenes are either vsync-capped (Gears) or black/erratic (Lost Odyssey).

## Next for a real wall-clock number (future)

- Need a title that renders real content AND is GPU-bound or uncapped and stable.
  None currently qualifies (Gears renders but is vsync-capped; the black titles
  are erratic). Revisit once a title reaches stable in-game rendering.
- Or add a steady-state-only timing window to the perf counters (reset counters
  after N warmup frames, sample a fixed window) so both arms compare the same
  steady region rather than cumulative-since-start.
