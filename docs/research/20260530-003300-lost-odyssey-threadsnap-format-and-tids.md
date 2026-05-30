# Lost Odyssey Thread-Snapshot: Format + Thread Census (iter 30 parse)

## Reliable results this iteration

Packet `scratch/thor-debug/game-pass-lo-snap2-20260530-003243` (Lost Odyssey
black, ~166 fps, vdswap=10748). The corrected snapshot recipe
(`arm64_speed_profile_thread_snapshot` + `_on_idle` +
`arm64_speed_profile_interval_ms 5000`) WORKED: **1255 `A64 thread snapshot`
rows**.

### Actual snapshot row format (corrects earlier guesses)

```
A64 thread snapshot tid=00000001 handle=F8000004 native=000059BC state=alive
  last_fn=830D9D6C last_ret=827B6CD4 lr=00000000 ctr=827B4780 r1=70030000
  r3=4004E118 r4=00000000
```
Fields are `tid / handle / native / state / last_fn / last_ret / lr / ctr / r1 /
r3 / r4`. There is NO `pc=` field and state is `alive` (not "running"); progress
must be judged by whether `last_fn`/`last_ret`/`r3` CHANGE across the 16 snapshot
sweeps for a given tid.

### Thread census

The snapshot enumerates ALL guest threads each interval: **12 guest threads
(tid 00000001..0000000C), 16 sweeps each = ~1255 rows** (Lost Odyssey runs a
large thread pool). Every thread is `state=alive`.

## What's still needed (next iteration — parse only, no re-run)

The full census means the join target isn't obvious from counts alone (all tids
have equal row counts). To finish:
1. From the wait trace in this same packet, get the join-target THREAD object id
   and the waiter tid (the NtWaitForSingleObjectEx status=00000102 type-thread
   row), then map that object/handle to the owning tid.
2. For THAT worker tid, compare `last_fn`/`last_ret`/`r3` across the 16 sweeps:
   - values CHANGE => guest-spinning (running, loop never exits) — expected from
     iter-29 framing;
   - values CONSTANT => parked at one site.
3. Whichever guest function dominates the worker's `last_fn` is the spin function
   to disassemble next (function-start filter) to find the polled exit condition.

## Process note

A late grep in this iteration returned stale/echoed output (a real tooling
inconsistency), so the per-tid last_fn diff was deferred rather than reported from
unreliable output. The 1255-row snapshot + format above are solid and committed;
the worker-tid diff is a clean parse for next time.

## Status

Snapshot capture FIXED and format documented (durable result). LO root cause
unchanged (guest worker never-exiting join). Next: map waiter->worker tid and diff
its last_fn across sweeps. No code change.
