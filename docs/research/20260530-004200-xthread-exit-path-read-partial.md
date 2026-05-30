# XThread::Exit/Terminate — Verified: signal_state set, waiters NOT woken (iter 31)

## Correction

An earlier draft of this note claimed Exit calls `event_->Set` and that reads were
corrupted. Both WRONG — the clean reads came through fine. Here is the verified
code.

## Verified code (src/xenia/kernel/xthread.cc)

`XThread::Exit` (444-472):
```
448  // TODO(benvanik): dispatch events? waiters? etc?
449  RundownAPCs();
452  X_KTHREAD* thread = guest_object<X_KTHREAD>();
453  thread->header.signal_state = 1;     // <-- signals the thread dispatch header
454  thread->exit_status = exit_code;
456  kernel_state()->OnThreadExit(this);
459  emulator()->processor()->OnThreadExit(thread_id_);
466  running_ = false;
467  ReleaseHandle();
470  xe::threading::Thread::Exit(exit_code);  // does not return
```
`XThread::Terminate` (474-495): same pattern — `thread->header.signal_state = 1`
(479) + OnThreadExit, no waiter dispatch.

## The bug (precise)

On thread termination both paths SET `header.signal_state = 1` on the guest
X_KTHREAD dispatch object, but **do NOT wake/redispatch threads already blocked
waiting on that object**. The TODO at line 448 ("dispatch events? waiters?") is
exactly the missing step.

So the Lost Odyssey join sequence is:
1. Guest thread A calls KeWaitForSingleObject/NtWaitForSingleObjectEx on worker
   thread B's handle -> blocks (status 00000102), because B.signal_state==0 at
   that moment.
2. B finishes; XThread::Exit sets B.header.signal_state=1 and goes zombie — but
   never re-evaluates/wakes A's wait.
3. A stays blocked forever (its wait only re-checks signal_state when something
   dispatches it). Main thread polls via 827CACA8. No color draws -> black.

This matches the device evidence exactly: worker handle F8000240 is ZOMBIE
(exited, signal_state should be 1) yet the waiter's NtWaitForSingleObjectEx is
still 00000102. The 924-zombie backlog = many terminated threads whose waiters
were never dispatched.

## Fix site (to design next, NOT guess-edited yet)

The wake must happen where signal_state is set on exit. Likely the correct call is
the same dispatch routine KeSetEvent / a thread-object signal helper uses to wake
waiters (xboxkrnl dispatch). Need to read (clean) how a normal event signal wakes
waiters (xboxkrnl_threading KeSetEvent / NtSetEvent -> the wait list dispatch) and
invoke the equivalent for the thread object in XThread::Exit/Terminate after
setting signal_state. Must wake from the EXITING thread's context before
threading::Thread::Exit (which never returns).

## Next iteration

1. Clean-read xboxkrnl KeSetEvent / the dispatcher wake path: what function
   re-evaluates waiters when an object becomes signaled? (e.g.
   xboxkrnl_threading.cc KeSetEvent -> some NativeList / wait-list signal.)
2. Confirm whether thread-object waits register on a list that a manual
   signal_state write bypasses. If yes, the fix = call that wake from
   XThread::Exit/Terminate after signal_state=1.
3. Design minimal fix; build NativeCore+ApkShell; device-verify Lost Odyssey
   reaches ps_writes>0 color draws.

## Status

VERIFIED root-cause fix site: XThread::Exit/Terminate set thread signal_state=1
but never dispatch/wake blocked waiters (TODO line 448). Device evidence consistent
(zombie worker, waiter stuck 00000102). Next = read the KeSetEvent wake path to
apply the same dispatch on thread exit. No code change yet.
