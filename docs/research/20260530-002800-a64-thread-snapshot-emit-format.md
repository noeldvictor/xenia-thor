# A64 Thread-Snapshot: Emit Format + Required Cvars (iter 29)

## Why prior snapshot attempts emitted nothing

`arm64_speed_profile_thread_snapshot true` alone does NOT produce output. The
guest thread snapshot is logged only when the A64 speed profiler is actually
running, which requires `arm64_speed_profile_interval_ms > 0`
(a64_backend.cc:1492 `StartSpeedProfiler` guards on it). My iter-27/28 snapshot
captures set only the bool, not the interval -> no snapshot rows. That was the
gap, not a tooling glitch.

## Emit format (a64_backend.cc)

- Snapshot gate (line 2633): `should_log_thread_snapshot =
  arm64_speed_profile_thread_snapshot && (!..._on_idle || idle_detected)` ->
  `LogGuestThreadSnapshot()`.
- Per-thread line (line 2660):
  `A64 guest thread snapshot thid {:08X} '{name}' pc {:08X} fn {:08X}
   last_fn {:08X} wait {...}`
  i.e. for EVERY guest thread it prints the live guest PC + containing fn +
  last_fn + wait state. This is exactly what distinguishes the LO joined thread:
  - if the join-target thread appears with an ADVANCING pc across snapshots ->
    guest-spinning (running but never exits),
  - if it appears parked at one pc (e.g. its entry/thunk) -> created but stuck/
    not progressing,
  - if it never appears -> never scheduled/created.

## Correct capture recipe (next iteration)

Lost Odyssey (Get-ReachScene lost_odyssey), -ExtraIntent with ALL of:
`--ez arm64_speed_profile_thread_snapshot true`
`--ei arm64_speed_profile_interval_ms 5000`   <-- REQUIRED, was missing
(optionally `--ez arm64_speed_profile_thread_snapshot_on_idle true` to fire when a
profile interval goes idle, which matches the stalled-black state)
plus `--ez xboxkrnl_thread_wait_trace true --ei xboxkrnl_thread_wait_trace_budget
256 --ei xboxkrnl_thread_wait_trace_after_ms 30000`.
Then grep the logcat for `A64 guest thread snapshot` and read each thread's pc/fn.
FIRST verify arm64_speed_profile_interval_ms + arm64_speed_profile_thread_snapshot
are in the EmulatorActivity allow-list / thor_xenia_debug.ps1 params (the snapshot
bool is at EmulatorActivity ~line 459; confirm the interval param too).

## Status

Durable result: identified the snapshot emit format + the missing
`arm64_speed_profile_interval_ms>0` requirement that made prior snapshot captures
empty. This unblocks the unscheduled-vs-guest-spinning determination for the
Lost Odyssey never-completing thread-join. No code change.
