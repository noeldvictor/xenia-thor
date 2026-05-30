# A64 Stackpoint Overflow Audit — Infinite Undiscovery (autonomous iter 16)

## Mechanism (from a64_emitter.cc)

The A64 backend mirrors the guest stack into a `stackpoints[]` array (host SP +
guest r1 + guest LR per guest call frame) so host/guest stacks can be resynced
after non-local control flow. Gated by `a64_enable_host_guest_stack_synchronization`.

- `PushStackpoint()` (prolog): writes the entry, `current_stackpoint_depth++`,
  then `cmp depth, a64_max_stackpoints (65536)` and on `>=` calls
  `HandleStackpointOverflowError` -> `xe::FatalError("Overflowed stackpoints!")`
  (a64_emitter.cc:6109) -> SIGABRT. This is what Infinite Undiscovery hits.
- `PopStackpoint()` (epilog): `current_stackpoint_depth--`.
- `EnsureSynchronizedGuestAndHostStack()`: compares live depth to the prolog-saved
  depth; on mismatch ("a longjmp occurred and some frames' PopStackpoint never
  ran") branches to `synchronize_guest_and_host_stack_helper()` to fix up.

So the design ALREADY handles the normal leak source (longjmp/setjmp skipping
pops). An overflow of 65536 therefore means one of:
1. The sync fixup is not catching Infinite Undiscovery's particular non-local
   control flow (so leaked stackpoints accumulate unbounded), or
2. Genuine runaway: a guest recursion / call pattern the mirror can't bound, or a
   guest-call path that pushes without ever reaching an epilog pop or a sync check.

## Why no code change this iteration

This is a JIT-backend stack-synchronization correctness issue. A wrong edit here
(raising the cap, force-popping, or disabling sync) risks silent stack corruption
across ALL titles, not just IU. Per the no-guess-edit guardrail, this iteration
produces the audit, not a patch.

- Raising `a64_max_stackpoints` only delays the overflow if it's a true leak.
- Disabling `a64_enable_host_guest_stack_synchronization` removes the mechanism
  and likely breaks longjmp-using titles' correctness.

## Concrete next experiments (for a future dedicated iteration)

1. Instrument (default-off, budgeted) push/pop/sync-fixup counts + max depth, like
   the other audit cvars, and run IU to a fresh overflow. If pops << pushes and
   sync-fixups are rare, it's a leak the sync path is missing -> find IU's
   non-local-flow site (it crashed in guest context via the JIT code cache). If
   depth climbs monotonically with balanced-but-deep frames, it's runaway
   recursion -> different class.
2. Check whether IU uses an exception/SEH or guest longjmp pattern that returns
   via a path lacking `EnsureSynchronizedGuestAndHostStack` (e.g. an indirect
   branch or kernel callback boundary that doesn't emit the sync check).
3. Only after the instrument capture identifies leak-vs-runaway, design the fix
   (add a sync check at the missing boundary, or bound/representation change).

## Status

IU remains: boots, then `Overflowed stackpoints!` SIGABRT. Audit documented;
deferred to an instrument-first iteration. Higher-ROI items (JIT boot cache,
UMA wall-clock) can go first.
