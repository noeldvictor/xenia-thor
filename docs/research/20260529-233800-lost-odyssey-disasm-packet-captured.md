# Lost Odyssey Disasm/File-IO Packet Captured — Parse Pending (iter 25)

## Status

Device capture succeeded; PARSING DEFERRED because the analysis tooling stopped
rendering output mid-iteration (per guardrail, did not fire blind follow-ups).

## Captured packet (the durable artifact)

`scratch/thor-debug/game-pass-lo-disasm-20260529-233810` — Lost Odyssey
(deterministic launch) with:
- `disassemble_functions true`, `disassemble_function_filter '827CACFC,822C5358'`
  (the main-thread spin LR and the worker NtWaitForSingleObjectEx LR from iter 24),
- `xboxkrnl_thread_wait_trace true` (budget 128, after_ms 30000),
- `xboxkrnl_file_io_trace true` (status log budget 64).

Run result: still black (near_black=1, ~169 fps, vdswap=9134, no crash) — same
guest-spin stall state as iter 24, as intended for the probe.

## Next iteration: parse this packet (no device re-run needed)

In `<packet>/*.logcat.txt`, with fresh tooling:
1. Filtered PPC disassembly around guest `827CACFC` (main spin) and `822C5358`
   (worker wait): what function/loop is each in? (grep for the disasm emit — likely
   tagged with the guest address or "disasm"/"PPC"; broaden if a guessed pattern
   returns 0.)
2. File-IO trace during the stall: any `NtCreateFile`/`NtReadFile`/
   `NtQueryFullAttributesFile` returning a C0.. status or `missing=1`? That would
   link Lost Odyssey's never-completing worker to the same file-IO class as Banjo
   (a guest waiting on a file op that fails/never completes). If instead there is
   no failing IO and the worker waits on an event/semaphore, it's a kernel-signal
   gap.
3. Conclude: file-IO-completion stall vs kernel-event-never-signaled, and commit
   the finding.

## Process note

Tooling glitch this iteration was output-rendering only; the device run + packet
write succeeded. Cvar names confirmed forwarded: disassemble_functions (line 136),
disassemble_function_filter (137), xboxkrnl_file_io_trace (138).
