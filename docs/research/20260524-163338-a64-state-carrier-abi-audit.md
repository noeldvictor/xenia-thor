# A64 State-Carrier ABI Audit

Date: 2026-05-24 16:33 EDT

Branch: `master`

Starting commit: `414f3edc0`

## Goal

Close the design gap left by
`docs/research/20260524-145241-state-carrier-followup-plan.md`: the runtime
counter volume is real, but a broader `82282490 -> 82287788` carrier cannot be
chosen safely until the A64 direct-call ABI and existing carrier scope are
source-reviewed.

This is an offline source audit. No Thor runtime capture was needed because no
generated behavior changed.

## New Tool

Added:

```text
tools/thor/thor_a64_state_carrier_abi_audit.ps1
```

The tool checks the A64 emitter and sequence sources for the current direct-call
contract, the existing `f[1]` carrier scope, and state-carrier counter coverage.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_state_carrier_abi_audit.ps1
```

## Result

All source checks passed:

```text
check name=callee_prolog_stores_x0_guest_return status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:2483
check name=direct_call_loads_x0_guest_call_return_then_blr status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:4034
check name=tail_call_loads_x0_guest_return_then_br status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:2873
check name=existing_f1_fastpath_is_callee_local status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:2439
check name=callee_call_audit_counts_child_edges status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:4279
check name=fpscr_writeback_audit_exists status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:4303
check name=state_load_audit_exists status=ok source=src/xenia/cpu/backend/a64/a64_sequences.cc:369
check name=state_store_audit_exists status=ok source=src/xenia/cpu/backend/a64/a64_sequences.cc:411
check name=backend_state_carrier_counters_exist status=ok source=src/xenia/cpu/backend/a64/a64_backend.h:258
```

Prior route-clean counter context from the follow-up planner:

```text
f1_reads=3477646
f1_fallbacks=0
fpscr_reads=3640919
fpscr_required_writebacks=1182090
fpscr_call_kills=2924556
```

Derived contract:

```text
current_direct_call_abi=guest_return_only_in_x0
existing_f1_carrier_scope=callee_local_stack_slot
existing_counter_coverage=enough_for_design_audit
parent_to_callee_payload_status=blocked_without_call_abi_extension_or_edge_specific_entry
normal_entry_requirement=must_keep_context_load_fallback
safe_next_behavior_patch=none
safe_next_tooling_patch=edge-specific ABI/variant design audit with compile-time and runtime counters
```

## Interpretation

The current compiled-call ABI uses `x0` for guest return metadata. A caller
cannot simply preload a host register and expect normal `82287788` entry to skip
context loads. The existing `arm64_blue_dragon_f1_carrier_fastpath` is a
callee-local stack-slot carrier only; it is not a parent/callee payload ABI.

That blocks another immediate behavior patch from the current evidence. A real
broader carrier must first pick one of two explicit designs:

- a deliberate A64 call-ABI extension with normal-entry fallback rules;
- an edge-specific compiled entry or variant for the hot `82282490 -> 82287788`
  call path.

`f[1]` stays the first plausible read-only payload candidate because it has
`3477646` route-clean reads and zero fallbacks. `fpscr` stays audit-only until
the exact CFG writeback and external flush points are proven.

## Decision

Do not run another quiet speed A/B from the current state-carrier evidence.

The next useful slice should build an edge-specific ABI/variant design audit
with compile-time and runtime counters, or prove exact `fpscr` writeback rules
before any generated behavior change.
