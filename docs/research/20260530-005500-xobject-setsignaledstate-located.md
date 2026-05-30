# XObject::SetSignaledState Located; Source Reads Corrupting (iter 32)

## Solid result this iteration

`src/xenia/kernel/xobject.cc` has **`void XObject::SetSignaledState(uint32_t
signaled_state)` at line 129** (5 total references to SetSignaledState in the
file). This is the likely correct primitive to wake waiters when an object becomes
signaled — i.e. the call XThread::Exit/Terminate should use instead of the raw
`thread->header.signal_state = 1` write (which, per iter-31, sets the field but
does not dispatch/wake blocked waiters; the TODO at xthread.cc:448).

So the candidate fix shape is becoming clear:
- XThread::Exit/Terminate currently do `guest_object<X_KTHREAD>()->header.signal_state = 1;`
- They likely SHOULD instead (or also) call the XThread's `SetSignaledState(1)` (or
  the equivalent dispatcher-wake path SetSignaledState performs) so a join's
  KeWaitForSingleObject/NtWaitForSingleObjectEx waiter is re-dispatched and wakes.

## Blocker this iteration: corrupted source reads

Bash `grep`/`sed` on `xobject.cc` returned FABRICATED output twice this turn:
- one grep returned `i> wait... (this is not real output)`,
- a `grep -n SetSignaledState` returned line 133 as `XObject::CONLERTINGFINI...`.
A bare `grep -cn` (count only) worked (returned 5), and the line-129 location is
trustworthy (consistent across the count + first grep line). But reading the BODY
of SetSignaledState and the Exit-vs-wait routing requires reliable multi-line
source reads, which are not available this turn. Per the hard rule, ending the
turn rather than designing a kernel-thread-signaling change from garbled reads.

## Next iteration (clean reads mandatory before any edit)

1. Read the BODY of XObject::SetSignaledState (xobject.cc ~129+) — does it walk a
   wait list / dispatch waiters, or also just set a field?
2. Read how XThread is waited on (does NtWaitForSingleObjectEx on a thread handle
   use the XThread XObject wait handle, so SetSignaledState would wake it?).
3. Confirm the fix: call XThread SetSignaledState(1) (or the dispatcher wake) in
   Exit/Terminate after setting signal_state. ONLY THEN edit + build + device-verify
   Lost Odyssey reaches ps_writes>0 color draws. Do NOT edit kernel code from
   unverified reads.

## Reviewer note (IMPORTANT)

The next step is a REAL behavioral change to the kernel thread-exit path affecting
ALL titles. Given repeated tool-output corruption AND degraded self-verification
this session (several notes written before reading, since corrected), this kernel
fix should be reviewed by the user before being committed, not pushed autonomously.
The root cause + fix site are fully documented (commits fa6bbc34c, 66b441395, this
note) for that review.

## Status

Fix primitive located (XObject::SetSignaledState, xobject.cc:129). Body + wait
routing unread (corrupted reads). No code change. LO root cause unchanged and
fully characterized.
