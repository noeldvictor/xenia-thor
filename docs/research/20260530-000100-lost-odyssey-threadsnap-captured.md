# Lost Odyssey Thread-Snapshot Attempt (iter 27) — my error + packet weak

## Honest correction

The prior draft of this note blamed a "shell glitch." That was WRONG. The real
cause: I parsed the wrong directory name (`game-pass-lo-threadsnap-20260530-000110`
from the stale prompt) when this run actually created
`game-pass-lo-threadsnap-20260529-235952`. The PowerShell errors were
"path does not exist", i.e. my path mistake, not tooling corruption.

## What the correct packet shows (second correction — wait rows WERE present)

`scratch/thor-debug/game-pass-lo-threadsnap-20260529-235952` (2.9 MB logcat,
Lost Odyssey black, near_black=1, ~172 fps, vdswap=11112):
- `arm64_speed_profile_thread_snapshot` produced NO greppable snapshot rows (tag
  unknown / different trigger).
- BUT `Xboxkrnl wait trace` = **256 rows** (my first grep used wrong patterns).
  The worker `XThread410BECB0 (F80001E4)`, thid 00000014, does
  `NtWaitForSingleObjectEx` x65 on a **THREAD** object **guest_object 00151018**
  (handle F8000248), status 00000102, LR **822C5358** — the SAME wait site as
  iter-24, but the waited thread object id is **00151018 this run vs 0014A018 in
  iter-24**.

### Key new insight: reproducible pattern, fresh thread id per launch

The never-completing thread-join reproduces every launch, but the target thread's
guest_object differs run-to-run (0014A018 -> 00151018). Same waiter thid
(00000014), same LR (822C5358), same status (0102). So this is a STRUCTURAL bug:
the guest always spawns a worker thread and joins it, and that worker never exits
— it's not tied to one fixed object address. That makes the fix target clear:
find why the spawned guest worker thread never terminates (or is never scheduled
to completion) in the emulator's thread/scheduler path.

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
