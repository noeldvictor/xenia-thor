# Lost Odyssey Join Waits on HOST Thread Handle — Reframes the Fix (iter 36)

## Verified facts (clean reads this turn)

1. `XThread::GetWaitHandle()` returns `thread_.get()` — the HOST thread
   (src/xenia/kernel/xthread.h:260). So waiting on a guest thread object waits on
   the underlying host `xe::threading::Thread`.
2. `XObject::Wait` (src/xenia/kernel/xobject.cc:188-) waits on
   `GetWaitHandle()` via `xe::threading::Wait(...)` and returns kSuccess only when
   the HOST wait handle signals (host thread terminates). It does NOT consult the
   guest `signal_state`.
3. `kthread->terminated` is NOT read anywhere meaningful in our fork (only a
   comment in kernel_state.cc:496). So upstream canary's `terminated=1` (its
   xthread.cc:482) is not the join mechanism here.

## Correction to prior notes

Earlier notes (committed 66b441395, fa6bbc34c) said the bug is "XThread::Exit sets
signal_state=1 but never wakes join waiters." That mechanism is WRONG: a thread
join in this fork waits on the HOST thread handle, which the OS signals when the
host thread exits (via xe::threading::Thread::Exit at the end of XThread::Exit).
So if the worker reaches XThread::Exit, the join SHOULD wake. The device evidence
(worker handle in state=zombie, join stuck status 00000102) is still real, but the
cause is NOT a missing kernel signal on the wait path.

## New, sharper question (not yet answered — do NOT guess)

Why does the join not wake even though the worker is a zombie? Possibilities to
test:
- (a) The worker host thread has NOT actually exited (zombie = guest-side XThread
  state, but the host xe::threading::Thread may still be alive/blocked), so the
  host wait handle never signals.
- (b) The join is waiting via a DIFFERENT path than XObject::Wait (e.g. a guest
  KeWaitForSingleObject HLE that checks signal_state on the guest dispatch header,
  not the host handle) — in which case signal_state timing/dispatch matters after
  all.
- (c) Handle resolution: the waiter's handle maps to a stale/!signaling object.

## Next step (one verification, no edit)

Read how guest NtWaitForSingleObjectEx / KeWaitForSingleObject (xboxkrnl_threading.cc)
resolves a thread handle and whether it calls XObject::Wait (host handle) or a
guest-dispatch wait that reads signal_state. That determines which of (a)/(b)/(c)
is in play. Only then is a fix designed.

## Status

Corrected the fix mechanism (join waits on host thread handle, not signal_state).
No code change. Kernel fix still gated; the real fix target is now an open,
specific question rather than the previously-stated (wrong) one.
