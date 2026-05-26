# A64 Fast-Entry Data Model Rerank

Date: 2026-05-26 09:35 EDT

## Trigger

After `docs/research/20260526-092500-a64-nonclosed-gpr-cache-capture.md`,
the current guest-state/cache lane had a route-clean counter result but no
behavior threshold: `clean_hits_possible=0`, `dirty_hits_possible=0`,
`normal_fallback=256`, `peak_live_slots=5`, and
`estimated_spill_pressure=3`.

The continual harness window was rebuilt:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_continual_harness_review.ps1 -Mode Window
```

Window output:
`scratch/thor-debug/continual-harness-window.md`.

## Rerank

Do not start another Thor capture from the closed nonclosed-cache lane. The
remaining speed-adjacent structural lane with a reusable path is guest-call
fast-entry, but only as source/data-model work with generated behavior still
unchanged.

The patch adds behavior-neutral storage to `A64Function`:

- separate `guest_call_fast_entry_code` pointer and length;
- explicit `A64GuestCallFastEntryContract`;
- payload mask bits for `r3-r10` and `lr`;
- dirty-flush mask bits for context barrier, helper call, host call,
  debug/trap, tail call, return, exception, and unresolved target;
- `SetupGuestCallFastEntry` plus a contract getter.

No emitter callsite uses the new entry. Normal `machine_code()`, normal
host-to-guest entry, direct-call `fn->machine_code()` behavior, and global
indirection remain unchanged.

## Audit Output

Feasibility report:
`scratch/thor-debug/20260526-093500-a64-fast-entry-feasibility.txt`.

Key result:

- `fast_entry_data_model_present=true`
- `existing_fast_entry_behavior_absent=true`
- `verdict=separate_fast_entry_data_model_present_behavior_absent`
- `behavior_status=normal_entry_unchanged;global_indirection_unchanged;direct_calls_still_use_normal_entry`

Guarded-stub report:
`scratch/thor-debug/20260526-093500-a64-guarded-stub-data-model-audit.txt`.

Key result:

- `normal_entry_storage_unchanged=true`
- `global_indirection_singleton=true`
- `direct_calls_use_normal_machine_code=true`
- `late_bound_unresolved_stays_normal=true`
- `alternate_entry_storage_present=true`
- `payload_abi_storage_present=true`
- `behavior_fast_entry_present=false`
- `decision=data_model_present_behavior_unchanged`

Prior runtime blockers still apply:

- `unresolved_direct_targets=52`
- `normal_entry_fallback=67`
- `stackpoint_sensitive=67`
- `dirty_flush_points=268`
- `callee_first_use_known=0`

## Decision

This is a harness and source-contract slice, not speed proof and not Thor route
proof. Do not run a quiet speed A/B from it.

The next fast-entry slice, if chosen, must remain behavior-neutral and define
the missing dirty-flush/payload protocol or a default-off stub skeleton. Do not
patch direct callsite behavior until the following are explicit:

- direct callsite guard and normal fallback;
- generated fast-entry stub or offset shape;
- payload population for `r3-r10/lr`;
- dirty flush before context barrier, helper call, host call, debug/trap, tail
  call, return, exception, and unresolved target;
- late-bound target resolution behavior;
- stackpoint resume protocol.

Keep `-Arm64OffsetMemoryAddressFastpath true` in future Blue Dragon speed
captures because it remains route-clean and locally positive, but keep the cvar
default-off globally.
