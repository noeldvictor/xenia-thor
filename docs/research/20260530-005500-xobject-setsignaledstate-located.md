# CORRECTION: "XObject::SetSignaledState" Does NOT Exist (iter 32)

## Retraction

The first draft of this note claimed `XObject::SetSignaledState` is at
`xobject.cc:129`. **That was FABRICATED.** `grep -c SetSignaledState
src/xenia/kernel/xobject.cc` returned 0 (exit 1 = no match); a whole-kernel
`grep -rn SetSignaledState src/xenia/kernel/` returns NOTHING. The symbol does not
exist. I invented a line number/symbol from empty grep output — exactly the
verify-before-asserting failure flagged earlier this session. Retracting it.

## Verified facts (Bash grep, confirmed)

- `src/xenia/kernel/xobject.h` (373 lines): `signal_state` is a field
  (xobject.h:59, in the dispatch header struct); the only Signal* method is
  `static X_STATUS XObject::SignalAndWait(XObject* signal_object, XObject*
  wait_object, ...)` at xobject.h:185.
- There is NO `SetSignaledState` method. So the iter-31 hypothesis ("call
  SetSignaledState in Exit") is moot as written — the actual wake mechanism has a
  different name and must be found by reading the real code, not guessed.

## What is still TRUE and device-proven (unchanged)

Lost Odyssey black = a guest worker thread that the game joins runs, EXITS, becomes
ZOMBIE (handle F8000240 state=zombie; 924 zombies), yet the join's
NtWaitForSingleObjectEx stays status 00000102 forever. And (iter-31, verified
read) XThread::Exit/Terminate set `guest_object<X_KTHREAD>()->header.signal_state
= 1` (xthread.cc:453/479) with a TODO at line 448 about dispatching waiters. The
ROOT CAUSE (terminated thread doesn't wake its join waiters) stands on device
evidence; only my guess about the FIX SYMBOL was wrong.

## Honest blocker

I have not reliably read how a signaled object wakes waiters in this codebase
(the actual dispatch/wake function), and this session I have repeatedly asserted
unverified specifics. The kernel-thread-exit fix must NOT be attempted by me
autonomously in this state.

## Recommendation to the user (reviewer gate)

Pause autonomous work on the kernel fix. The Lost Odyssey black-frame is
root-caused and documented (commits fa6bbc34c, 66b441395). Implementing the wake-
on-thread-exit fix requires reading the real xboxkrnl wait/dispatch path
(KeSetEvent / NtSetEvent wake, NativeList of waiters, KeWaitForSingleObject
re-check) and is an all-titles behavioral change — worth a human eye, especially
given the tool-output and self-verification issues this session. The next safe
autonomous step is the DEVICE-ONLY boot-breakdown baseline, not a kernel edit.

## Status

Retracted a fabricated symbol. Verified: no SetSignaledState; signal_state is a
field; SignalAndWait at xobject.h:185. LO root cause unchanged. No code change.
