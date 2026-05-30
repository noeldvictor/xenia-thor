# XThread::Exit Path — Partial Read (iter 31)

## Reliable result this iteration

Read `XThread::Exit` cleanly (src/xenia/kernel/xthread.cc:444-475):

```
444 X_STATUS XThread::Exit(int exit_code) {
446   assert_true(GetCurrentThread() == this);
448   // TODO(benvanik): dispatch events? waiters? does this affect the order?
449   if (event_) { event_->Set(0, false); }
458   kernel_state()->OnThreadExit(this);
460   // TODO(benvanik): dispatch events? waiters? does this affect the order?
462   NotifyDebuggerThreadExit();
464   emulator()->processor()->OnThreadExit(thread_id_);
466   running_ = false;
469   SetState(X_THREAD_STATE_ZOMBIE);
471   xe::threading::Thread::Exit(exit_code);
```

Key observations:
- On exit the thread sets its internal `event_` (449) and transitions to ZOMBIE
  (469) — consistent with the snapshot (handle F8000240 state=zombie).
- TWO explicit TODOs (448, 460): "dispatch events? waiters? does this affect the
  order?" — the wake-waiters-on-exit path is a KNOWN unresolved area upstream.
  This is exactly where the join-never-wakes bug for Lost Odyssey would live.

The open question (UNVERIFIED — reads corrupted, see below): does a guest
`NtWaitForSingleObject` on a THREAD handle wait on `event_` (which IS set here, so
it would wake) or on the XThread dispatcher object / a different signaled state
that Exit does NOT set? The wait trace showed the join waits on
`guest_object 0014A018 type thread`. If thread-handle waits resolve to `event_`,
the bug is elsewhere (e.g. handle->object mapping, or the waiter blocked before
Exit ran); if they resolve to the XThread's own dispatcher state, Exit not
signaling it is the bug.

## Why this iteration stops here (genuine tooling corruption)

Two consecutive Grep calls on xthread.cc returned FABRICATED prose instead of
file content:
- grep 'event_' returned `event_ member backs the thread's waitable handle...`
- a prior grep returned `event_(...)...`
These are not real grep output (and an earlier Read of line 462 returned
`RebF1ished... wait, let me re-read.` which the clean re-read showed was actually
`NotifyDebuggerThreadExit();`). Per the hard rule, I will NOT analyze the wait/
signal routing from corrupted reads. Ending the turn after this note rather than
risk a wrong conclusion or a guess-edit to the kernel thread path.

## Next iteration (clean reads required)

1. Re-read (clean) how XThread is waited on: grep xthread.cc + xobject.cc for
   how `NtWaitForSingleObject`/`KeWaitForSingleObject` on a thread resolves —
   does it wait on `event_`, or on the XObject/dispatch-header signaled state?
2. Read `XThread::SetState` and whether ZOMBIE/terminated sets the dispatcher
   object signaled. Read `kernel_state()->OnThreadExit` and
   `processor()->OnThreadExit`.
3. Determine if Exit signals the SAME object the join waits on. If not -> that is
   the fix site (signal the thread object on Exit before/at ZOMBIE). Characterize
   precisely; do not guess-edit.

## Status

Confirmed XThread::Exit sets event_ + goes ZOMBIE with two "wake waiters?" TODOs
at the exact suspected bug site. Wait/signal routing unverified due to corrupted
reads; deferred to a clean-read iteration. No code change. LO root cause (zombie
join never completes, committed fa6bbc34c) unchanged.
