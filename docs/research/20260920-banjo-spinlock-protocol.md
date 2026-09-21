# Banjo-Kazooie stalls after its first world frame: two lock protocols on one word

Date: 2026-09-20. Title: Banjo-Kazooie: Nuts & Bolts (4D5307ED). Axis on the paradigm matrix:
CPU ISA and memory model. The a64 backend and the kernel HLE wrote different encodings of
"held" into the same `KSPINLOCK` word.

## Symptom

- The intro renders at 27.7 fps. The first game-world frame renders with the lower half black.
- No new frames after that. The FPS badge reads 0.0. GPU busy is 1 %.
- One thread runs at 69 to 95 % of one core inside `sched_yield` (simpleperf).

## Diagnostic

A stall report in `xeKeKfAcquireSpinLock` (`xboxkrnl_threading.cc`). After two seconds of
spinning, once per call, it logs the lock address, the owner word, the owner's thread id, the
spinner, and the guest link register. Output on the device:

```
SPINLOCK STALL: lock=405143C0 owner_pcr=FF0BD000 owner_tid=00000000
  spinner_tid=00000015 spinner_lr=8266C30C our_pcr=000CD000 r13=00000000000CD000
  words=00000000 00000000 00000000 405142AC (held over 2 s)
```

The game's own critical section around the lock says unheld (count 0, owner 0). The kernel
lock word holds `0xFF0BD000`. No PCR has that value. `owner_tid` is 0 because no thread owns
it.

## Cause

The HLE protocol (`xeKeKfAcquireSpinLock`, `KeTryToAcquireSpinLockAtRaisedIrql_entry`,
`xeKeKfReleaseSpinLock`): held = the owner's PCR (r13) stored big-endian; free = 0.

The a64 inline fast paths (`a64_emitter.cc`, commit `1a34dd7b41` of 2026-05-20, cvar
`a64_inline_kernel_spinlock_exports`, default true):

| export | old inline behavior | HLE behavior |
|---|---|---|
| `KeAcquireSpinLockAtRaisedIrql` | CAS 0 to host value 1 | CAS 0 to big-endian PCR |
| `KeTryToAcquireSpinLockAtRaisedIrql` | CAS 0 to host value 1 | CAS 0 to big-endian PCR |
| `KeReleaseSpinLockFromRaisedIrql` | atomic decrement (`ldaddal -1`) | store 0 with release |

The two protocols agree only when the same side does both halves. Banjo mixes them: the
spinner's own PCR is `0x000CD000`. Stored big-endian the bytes are `00 0C D0 00`, which the host
reads as `0x00D00C00`. The inline release subtracts one: `0x00D00BFF`, bytes `FF 0B D0 00`,
read big-endian as `0xFF0BD000`. That is the value in the log, exactly. The sequence:

1. Thread 0x15 acquires the lock through the HLE (`KfAcquireSpinLock`, the IRQL-changing
   export, which is not inlined). The word holds `BE(0x000CD000)`.
2. It releases through the inlined `KeReleaseSpinLockFromRaisedIrql`. The word holds
   `0xFF0BD000`, which is not zero.
3. It acquires again through the HLE. The CAS from 0 never succeeds. The thread yields forever.

The old inline acquire also stored 1, which the HLE spin path then read as a PCR of
`0x01000000` and dereferenced (`TranslateVirtual<X_KPCR*>`). That is a second latent fault.

## Fix

The inline paths now use the HLE protocol. Acquire and try-acquire load r13 from the context,
byte-swap it, and CAS it into the word (slow path when r13 is 0). Release stores zero with
release semantics (`stlr wzr`). The decrement is gone. The LSE and the exclusive-monitor
variants both changed.

## Why the decrement existed

The 2026-05-20 note (`20260520-a64-context-cache-and-spinlock-fastpaths.md`) does not say.
A decrement is the release of a count, not of an owner word. The x64 backend has no inline
spin-lock path; this was new code for a64 and it was tested on titles where the a64 fast path
did both halves.

## Result of the first fix (spin lock)

Probe after the build (`xenia_probe`, warm cache, 38,104 functions in 11.1 s): intro at
29.6 fps; START at +41 s; the puzzle transition and the game world behind it at +91 s; no
`SPINLOCK STALL` line. The stall is gone. The FPS badge showed 0.0 in the 30 s windows, and
the badge lines with a value show 1 to 4 swaps per 500 ms with gaps of 3 to 6 s, and one burst
of 16 swaps. So the game ran at 2 to 6 fps with pauses, not 0. The probe aborted at a GPU
temperature of 70.5 C.

The log for that run held 1,381,223 lines. 1,330,825 of them were two lines in pairs:
`KeRaiseIrqlToDpcLevel - old_irql > 2` and `KfLowerIrql : new_irql > kpcr->current_irql!`,
13,000 per second. 50,267 were `Texture fetch constant ... has "invalid" type`.

## The second mismatch: the IRQL state

Same class, same file. The inline `KeRaiseIrqlToDpcLevel` and `KfLowerIrql` in
`a64_emitter.cc` swapped a private word, `Processor::irql_` (through
`A64BackendContext::processor_irql`). Nothing else reads that word. The HLE
(`KeRaiseIrqlToDpcLevel_entry`, `xeKfLowerIrql`, `xeKfRaiseIrql`), the kernel's DPC
impersonation (`kernel_state.cc`), the APC delivery gate (`current_irql >= 1` masks APCs in
`xboxkrnl_threading.cc`), `XObject::Wait` (`wait_irql`), and the guest's own code use
`X_KPCR::current_irql`, the byte at `0x18(r13)`.

Consequences:

- An inline raise leaves the KPCR byte unchanged. APCs deliver at what the guest believes is
  DPC level.
- An HLE raise followed by an inline lower leaves the KPCR byte at 2. APCs are masked until
  some HLE lower runs. Threads that wait for an APC-driven completion wait longer or forever.
  This is the shape of the Gears stall (five threads wait on an event the HLE never signals);
  not tested there yet.
- Every HLE raise or lower on a thread whose byte is stale logs an error line. Two lines per
  pair, 13,000 per second, each a write to logcat from the guest thread.

Fix: the inline raise and lower read and write the KPCR byte with `ldrb` and `strb` through
r13. The KPCR is per guest thread, so the plain byte access is the HLE's own sequence. The
APC guard around the inline lower is unchanged. The two HLE error lines print 16 times, then
once in 65,536, with the values and the occurrence count. The texture fetch warning prints
16 times, then once in 4,096.

## Rule

An inline fast path in the backend must read and write the same state, in the same encoding,
as the HLE it replaces. The check: for each inlined export, name the memory the HLE touches
and the value it stores, then read the emitter. Two exports failed that check today. The
remaining inlined exports (`RtlEnterCriticalSection`, `RtlLeaveCriticalSection`,
`RtlTryEnterCriticalSection`, `KeEnterCriticalRegion`, `KeLeaveCriticalRegion`) use the guest
structure fields with the guest byte order; they pass.
