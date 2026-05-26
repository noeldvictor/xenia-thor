# A64 Fast-Entry Codegen Protocol Gate

Date: 2026-05-26 11:00 -04:00
Base commit: `038ee9934`
Status: source-only protocol gate, behavior blocked

## Context

The previous source-only fast-entry slices added separate fast-entry storage,
the `r3-r10/lr` payload and dirty-flush contract, a default-off stub skeleton,
direct-call guard modeling, and payload/dirty-flush planning. The latest audit
still blocked runtime behavior because generated code did not use those models.

This slice adds the next no-op source gate only. It does not emit or route to
alternate guest code.

## Changes

- Added `A64GuestCallFastEntryCodegenProtocolBlocker`.
- Added `A64GuestCallFastEntryCodegenProtocolInputs` and
  `A64GuestCallFastEntryCodegenProtocolDecision`.
- Added `EvaluateA64GuestCallFastEntryCodegenProtocol`.
- Added `tools/thor/thor_a64_fast_entry_codegen_protocol_audit.ps1`.

The evaluator composes:

- `EvaluateA64GuestCallFastEntryGuard`;
- `BuildA64GuestCallFastEntryPayloadFlushPlan`;
- explicit generated-code capability gates for guard emission, payload
  population, dirty flush emission, late-bound fallback, stackpoint resume, and
  debug/exception visibility.

## Audit

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_fast_entry_codegen_protocol_audit.ps1
```

Report:

```text
scratch/thor-debug/20260526-110000-a64-fast-entry-codegen-protocol-audit.txt
```

Result:

```text
codegen_protocol blockers=9/9 inputs=8/8 behavior_changed=0
decision=codegen_protocol_source_ready_behavior_blocked
safe_next_patch=lane_switch_or_behavior_neutral_emitter_planning_audit
do_not_patch=direct_callsites;machine_code_pointer;global_indirection;payload_materialization;dirty_flush_codegen;fast_entry_stub_codegen;speed_ab
```

The audit also confirms:

- direct calls still branch to `fn->machine_code()`;
- normal entry still receives only the guest return address in `x0`;
- late-bound paths still use normal entry;
- generated code does not call the guard, payload/flush planner, or new
  codegen protocol gate;
- payload population, dirty flush, fast-entry stub codegen, and fast-entry
  callsite behavior are absent.

## Decision

This closes the "no-op codegen protocol helper" slice. It is not route proof,
speed proof, or a behavior patch. Do not run a Thor speed A/B from this state.

Next work should either switch to another structural A64 lane, or add only a
behavior-neutral emitter planning audit that proves where guard/payload/flush
codegen would attach while generated behavior remains unchanged.
