# Lost Odyssey: Joined Worker Thread Is Silent (iter 28 parse)

## Result

Packet `scratch/thor-debug/game-pass-lo-joinchase-20260530-000724` (256 wait rows,
black, ~174 fps). The join target this run is THREAD object `00153018`.

Three threads appear as wait-trace SUBJECTS this run (thids):
- thid `00000014` "XThread41594CB0 (F80001E0)" — 158 rows, the never-returning
  join (NtWaitForSingleObjectEx on a type-thread object, status 00000102, LR
  822C5358).
- thid `00000006` "Main XThread" — 96 rows, all `KeDelayExecutionThread` (the
  827CACA8 poll wrapper).
- thid `00000017` — 2 rows.

(Per-launch ids vary: the join-waiter was thid 00000014 here and in iter-27, but
the Main poller is 00000006 this run; the joined THREAD object id also varies per
launch — confirming the structural, not fixed-address, nature.)

**The joined thread object NEVER appears as a subject** — it issues zero kernel
wait calls.

## Interpretation (with caveat)

Two possibilities, and the wait trace alone cannot fully separate them:
1. The joined thread was created but is **never scheduled / never runs** in the
   emulator (a thread-startup/scheduling gap) — it would issue no kernel calls
   because it never executes.
2. The joined thread IS running but **spins purely in guest code** with no kernel
   wait calls — so it never shows in a wait trace (which only logs kernel wait
   APIs).

To distinguish: an A64 speed-profile / thread snapshot (which samples ALL guest
threads' PCs regardless of kernel calls) would show whether 00153018 has a live
advancing PC (case 2) or never appears at all / is parked at its entry (case 1).
The snapshot cvar did not emit greppably in prior attempts; verifying its emit
path/name in src/xenia/cpu/backend/a64 is the prerequisite.

## Net root-cause status (Lost Odyssey black)

A guest worker thread is created and joined by thid 00000014, but never completes;
it does no kernel waits, so it is either unscheduled or guest-spinning. Main thread
polls meanwhile. Result: guest never reaches color-render code -> black. Structural
(target id varies per launch), reproducible, device-proven. Not GPU, not file-IO.

## Next experiment

1. Verify arm64_speed_profile_thread_snapshot emit format/path; if it samples all
   guest thread PCs, capture and check whether 00153018-equivalent (the per-run
   join target) has a live/advancing PC (guest-spin) or is parked (unscheduled).
2. That single distinction determines the fix lane: thread-scheduler/startup gap
   vs a guest spin waiting on a missing signal. Characterize; do not guess-edit.
