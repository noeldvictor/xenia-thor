# Lost Odyssey Thread-Snapshot Attempt (iter 27) — my error + packet weak

## Honest correction

The prior draft of this note blamed a "shell glitch." That was WRONG. The real
cause: I parsed the wrong directory name (`game-pass-lo-threadsnap-20260530-000110`
from the stale prompt) when this run actually created
`game-pass-lo-threadsnap-20260529-235952`. The PowerShell errors were
"path does not exist", i.e. my path mistake, not tooling corruption.

## What the correct packet shows

`scratch/thor-debug/game-pass-lo-threadsnap-20260529-235952` (2.9 MB logcat,
Lost Odyssey black, near_black=1, ~172 fps, vdswap=11112):
- `arm64_speed_profile_thread_snapshot` produced NO recognizable snapshot rows
  (grep 'snapshot'/'profile'/'last_fn' = 0). The snapshot cvar did not emit in a
  greppable form here.
- `Xboxkrnl wait trace` rows = 0 this run, and `0014A018` not present. The wait
  trace (after_ms 30000) evidently did not fire in this capture window — likely
  the run/skip window timing differed from the iter-24 packet that DID capture it.

So this packet does NOT advance the 0014A018 identification. The authoritative
data remains the iter-24 packet (`game-pass-lo-waitprobe-20260529-233020`, 256
wait rows) and the iter-26 disasm (`game-pass-lo-disasm-20260529-233831`, 746
rows) already committed.

## Established LO root cause (unchanged, device-proven)

Lost Odyssey black = never-completing thread-join: worker XThread3F576CB0 waits
forever (NtWaitForSingleObjectEx status=00000102) on THREAD object guest_object
0014A018 to exit; main thread polls in a KeDelayExecutionThread delay-wrapper
(827CACA8, disasm-confirmed). Guest thread 0014A018 never exits. Not GPU, not
file-IO.

## Next (clean, specific)

To identify 0014A018, re-capture with the SAME timing as the iter-24 waitprobe
(that one reliably produced 256 wait rows): Get-ReachScene lost_odyssey +
"--ez xboxkrnl_thread_wait_trace true --ei xboxkrnl_thread_wait_trace_budget 256
--ei xboxkrnl_thread_wait_trace_after_ms 30000", then in the wait trace find any
row whose OWN thread id (not the waited object) = 0014A018 to see what 0014A018
itself is doing. If the snapshot cvar is needed, first verify its emit path/name
in src/xenia/cpu/backend/a64 (the cvar may print only on a different trigger).

## Process lesson (for me)

Use the ACTUAL OutDir printed by the sweep run, never a path copied from the
prompt. And read errors before labeling them — "path does not exist" != "glitch".
