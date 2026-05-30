# Lost Odyssey Stall = Thread-Join That Never Completes (iter 26)

## Refined conclusion (no disassembly needed)

The Lost Odyssey worker `XThread3F576CB0 (F80001E0)` blocks on
`NtWaitForSingleObjectEx` against a **THREAD-type** object (handle F8000240,
guest_object 0014A018), `status=00000102` (still waiting). A wait on a thread
handle is a **thread-join**: "wait until thread 0014A018 terminates." It never
returns -> thread `0014A018` **never exits**. Meanwhile the Main XThread spins at
LR 827CACFC polling via KeDelayExecutionThread, presumably waiting for that same
worker chain to finish before it proceeds to scene render. That is the unmet
dependency behind the black screen (no color draws): a guest thread that was
created but never completes.

## Disassembly extraction failed — why (tooling limit, not glitch)

Tried `disassemble_functions` + `disassemble_function_filter '827CACFC,822C5358'`
in packet game-pass-lo-disasm-20260529-233831: ZERO disasm rows in logcat. Two
real reasons:
1. **Wrong address kind**: 827CACFC and 822C5358 are LR (return-address) values
   from the wait trace, NOT function START addresses. The filter matches function
   entry points, so LR values match nothing. Need the containing function's start
   address (e.g. from a scanner/function-range dump) to filter correctly.
2. **Compile-time emission**: disassembly is emitted when the function is JIT-
   compiled (early boot); the bounded `logcat -t` tail rolls those rows off long
   before the 30s+ capture point. A disasm capture must grab early-boot logcat or
   dump to a file.

## Next experiment

Identify what thread `0014A018` is and why it never exits — this is more direct
than disassembling the waiter:
1. Re-run Lost Odyssey with `arm64_speed_profile_thread_snapshot true` (+ on_idle)
   and parse the snapshot for ALL guest threads, find the one whose guest_object/
   tid corresponds to 0014A018, and see its last_fn / wait state. If 0014A018 is
   itself blocked on another wait (a wait chain) or never scheduled, that's the
   root.
2. If 0014A018 is itself waiting on an event/IO, follow that chain. If it's
   runnable but never advancing, it's a scheduler/JIT gap.
3. For disassembly later: get function-start addresses via a scanner/function-
   range dump first, then filter on those (not LR values), and capture early-boot
   logcat.

## Status

LO black narrowed to a **never-completing thread-join** (worker waits forever for
guest thread 0014A018 to exit). Disasm extraction blocked by LR-vs-function-start
+ compile-time-logcat-roll-off (documented for the next attempt). No code change.
Pivoting next iteration to the boot-breakdown easy win + the thread-0014A018
snapshot. Guest-execution/kernel lane, not GPU/file-IO.
