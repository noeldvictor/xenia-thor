# Lost Odyssey Black ROOT CAUSE: Zombie Thread Never Signals Its Join (iter 30)

## Verdict (device-proven) — corrects the "guest-spinning" hypothesis

The joined worker thread did NOT spin forever. It **finished and became a ZOMBIE**,
but its thread object is never signaled as completed, so the joining thread's
`NtWaitForSingleObjectEx` on it never returns. The black frame is an HLE
thread-completion / zombie-reaping signaling gap, not a guest spin and not GPU.

## Evidence

Packet `scratch/thor-debug/game-pass-lo-snap2-20260530-003243` (Lost Odyssey
black, ~166 fps, A64 thread snapshot working via arm64_speed_profile_interval_ms).

- Join waiter (wait trace): `NtWaitForSingleObjectEx ... handle F8000240
  guest_object 0014A018 type thread status 00000102` from thid 00000014
  'XThread3DEB6CB0', LR 822C5358 — waiting for thread **handle F8000240** to finish.
  (116 such waits; object 0014A018 — same id as the iter-24 packet, so it recurs,
  not purely random.)
- Thread snapshot for **handle F8000240**: `state=zombie`,
  `last_fn=00000000 last_ret=00000000 r1=00000000` — i.e. the thread has EXITED
  (no live context), reaped to zombie. Across sweeps: 364 zombie vs 13 alive — it
  goes alive->zombie early and stays zombie.
- Global state census across all snapshots: 924 zombie, 318 alive, 13 exited —
  Lost Odyssey spawns ~94 guest threads and accumulates a large pool of ZOMBIES
  that never clear.

## Conclusion

The worker the game joins on RUNS, EXITS, and becomes a zombie — but the join
never completes. So the emulator is not signaling the thread/dispatcher object to
the satisfied state when the guest thread transitions to zombie/terminated (or the
zombie is not being reaped in a way that wakes waiters). The guest's main thread
then polls forever (827CACA8 KeDelayExecutionThread wrapper) and never reaches
color-render -> black.

This is a concrete, fixable HLE bug in the guest-thread termination/join path
(xboxkrnl thread exit -> object signal -> NtWaitForSingleObject wake), NOT a GPU,
file-IO, scheduler-starvation, or guest-spin issue. The big zombie backlog (924)
suggests thread-object termination signaling/reaping is broadly under-firing.

## Next experiment (characterize the signal path; do not guess-edit yet)

1. Read the xboxkrnl thread-exit / XThread termination path: where a guest thread
   transitions to terminated/zombie and where it should signal its thread
   dispatcher object so KeWaitForSingleObject/NtWaitForSingleObjectEx waiters wake.
   Files: src/xenia/kernel/xthread.cc (Exit/terminate), and the dispatch/wait in
   xboxkrnl (KeSetEvent/SignalObject on thread completion).
2. Confirm with a targeted trace: when handle F8000240 goes zombie, is there a
   corresponding object-signal / NtWaitForSingleObjectEx wake for waiters? If the
   signal is missing on the zombie transition, that's the fix site.
3. Only after the missing-signal site is identified from source + trace, design a
   minimal fix and device-verify Lost Odyssey reaches color draws.

## Corrections to prior notes

- iter-29/30 "guest-spinning in 82F8E090": WRONG — that was from an uncommitted
  draft; the snapshot shows the joined thread is zombie (exited), not spinning.
  (82F8E090 may be where a DIFFERENT live thread loops, but the JOIN TARGET is a
  zombie.)
- "join object id varies per launch": partially wrong — 0014A018 recurs (iter-24
  and this run). It is stable enough to track.

## Status

Lost Odyssey black root cause established: zombie worker thread whose join never
completes -> HLE thread-termination signaling/reaping gap. Device-proven via
thread snapshot. Next = read xthread exit/signal path. No code change.
