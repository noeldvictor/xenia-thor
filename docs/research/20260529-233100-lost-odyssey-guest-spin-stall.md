# Lost Odyssey Black Stall = Guest Delay-Loop Spin, Not Deadlock (iter 24)

## Probe

Lost Odyssey (deterministic launch) with `xboxkrnl_thread_wait_trace` (budget 256,
after_ms 30000) + `arm64_speed_profile_thread_snapshot`. Packet
`game-pass-lo-waitprobe-20260529-233020` (black, near_black=1, ~172 fps,
vdswap=11379, no crash). Wait-trace emit format is `Xboxkrnl wait trace <API> ...`
(256 rows captured).

## Findings

Wait-API frequency in the black-stall window:
- `KeDelayExecutionThread` x179 (short timed sleeps)
- `NtWaitForSingleObjectEx` x75
- `KeWaitForSingleObject` x2

Per guest thread:
- `XThread3F576CB0 (F80001E0)` x149 - almost all `NtWaitForSingleObjectEx`,
  `status=00000102` (STILL_WAITING / timeout) on a thread handle
  (handle F8000240, guest_object 0014A018, type thread) - waiting on a worker
  thread that never completes.
- `Main XThread (F8000028)` x103 - all `KeDelayExecutionThread` (`wait_type 1`,
  `timeout 0`), and ALL 103 return to the **same LR `827CACFC`** -> a tight guest
  spin/poll loop at one site.

## Interpretation

NOT a kernel deadlock on an unsignaled object: the main thread is actively
running and yielding via `KeDelayExecutionThread` in a loop at LR `827CACFC`
(poll/spin), while worker `XThread3F576CB0` blocks on `NtWaitForSingleObjectEx`
for a thread that never finishes (`status=00000102`). Classic "guest is waiting
for an async operation (asset load / stream / worker completion) that never
signals completion." That is exactly why it issues only depth/setup draws and
never reaches its scene-render code (the no-color-draws finding from iter 21-22).

So: Lost Odyssey black = a guest-progress stall where a background worker never
completes, and the main thread spins at guest PC near `827CACFC` waiting for it.

## Next experiment (characterize, do not guess-edit)

1. Disassemble around guest `827CACFC` (the main-thread spin LR) and the worker's
   wait site `822C5358` (LR of the NtWaitForSingleObjectEx rows) with the
   disassemble-function-filter cvar, to see WHAT the worker is supposed to do and
   why it never completes (an I/O completion? a sub-thread join? an event the host
   side never signals?).
2. Check the file-I/O trace (xboxkrnl_file_io_trace) during the stall: if the
   worker is blocked on a file read that returns an unhandled status, that links
   to the same file-IO class as Banjo. If it's an event/semaphore never signaled
   by an unimplemented kernel path, that's a kernel-impl gap.
3. Only after the worker's blocking dependency is identified, decide a fix. This
   is a guest-execution/kernel lane, not GPU.

## Status

Black-frame for Lost Odyssey traced to a guest delay-loop spin (main thread LR
827CACFC) + a worker stuck on NtWaitForSingleObjectEx (status 0102) - a worker
that never completes. No deadlock, no GPU bug. Next: disassemble the spin/wait
sites to find the unmet dependency. No code change.
