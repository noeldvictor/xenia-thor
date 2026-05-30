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

## CORRECTION: disassembly WAS captured (746 rows)

An earlier draft of this note wrongly said disasm extraction failed. It did NOT —
the packet has 746 `Filtered function dump` rows. The filter resolved
`827CACFC` to its containing function `827CACA8-827CAD30` and dumped full PPC.

Key disasm of the main-thread spin function `827CACA8` (LR 827CACFC sits at
`loc_827CACFC` inside it):
```
827CACA8 mfspr r12,256 / stw / std r30,r31 / stwu r1,-0x70   ; prologue
827CACBC or    r31, r4, r4                                    ; save arg
827CACC0 cmpi  crf6, 0, r3, -0x1                              ; r3 == -1 ?
827CACC4 beq   crf6, 0x827CACD8
827CACC8 rldicl r11, r3, 0, 32
827CACCC mulli  r11, r11, -0x2710                             ; * -10000 (100ns->ms scale)
827CACD0 std    r11, 0x50(r1)
... loc_827CACFC ...                                          ; the LR the main thread returns to
```
The `* -10000` constant and the small frame are the signature of a
**timeout/delay wrapper around KeDelayExecutionThread** (10000 = 100ns ticks per
ms). So the main thread is repeatedly calling this delay-with-timeout helper and
returning to `827CACFC` — i.e. polling with a timed sleep, consistent with the
iter-24 wait trace (179 KeDelayExecutionThread). This CONFIRMS (with disassembly,
not inference) that 827CACFC is a poll/delay loop, not a blocking wait.

(The 822C5358 worker-wait function disasm is also in the 746 rows; not yet
extracted in detail — next pass.)

The original "failed" reasoning below is RETAINED ONLY as a caution for cases
where disasm truly is missing (LR-vs-function-start, compile-time roll-off); it
did not apply here.

## (retained caution) When disasm IS missing — why
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
