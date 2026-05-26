# A64 Fast-Entry Emitter Planning Audit

Date: 2026-05-26 11:15 -04:00
Base commit: `02390345c`
Status: source-only emitter planning audit, behavior blocked

## Context

The previous fast-entry source slices added the data model, dirty-flush
contract, stub skeleton, direct-call guard model, payload/flush planner, and
codegen protocol gate. The latest codegen protocol audit still blocked behavior
because generated code did not use the protocol and the normal A64 call paths
were unchanged.

This slice adds a deterministic planning audit only. It locates the emitter
attachment points a future guarded fast-entry codegen patch would need, and it
checks that no generated behavior has been added.

## Changes

- Added `tools/thor/thor_a64_fast_entry_emitter_planning_audit.ps1`.
- The audit checks current source attachment points for:
  - the direct guest-call `fn->machine_code()` branch;
  - the `GUEST_CALL_RET_ADDR` return-address load;
  - the late-bound indirection / `ResolveFunction` fallback path;
  - stackpoint resume and host/guest stack synchronization;
  - debug break, exception callback, and source-map visibility.
- The audit also checks that generated code still does not call the guard,
  payload/flush planner, or codegen protocol gate.

## Audit

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_fast_entry_emitter_planning_audit.ps1
```

Report:

```text
scratch/thor-debug/20260526-111500-a64-fast-entry-emitter-planning-audit.txt
```

Result:

```text
decision=emitter_planning_source_ready_behavior_blocked
safe_next_patch=lane_switch_or_compile_time_route_ranking_for_non_fast_entry_structural_lane
do_not_patch=direct_callsites;machine_code_pointer;global_indirection;payload_materialization;dirty_flush_codegen;fast_entry_stub_codegen;speed_ab
```

Key planning points:

```text
direct_call_guard_site=src\xenia\cpu\backend\a64\a64_emitter.cc:5862
return_addr_site=src\xenia\cpu\backend\a64\a64_emitter.cc:5863
direct_branch_site=src\xenia\cpu\backend\a64\a64_emitter.cc:5864
late_bound_site=src\xenia\cpu\backend\a64\a64_emitter.cc:5912
resolve_call_site=src\xenia\cpu\backend\a64\a64_emitter.cc:5922
stackpoint_site=src\xenia\cpu\backend\a64\a64_emitter.cc:6114
stack_sync_site=src\xenia\cpu\backend\a64\a64_emitter.cc:6169
debug_site=src\xenia\cpu\backend\a64\a64_emitter.cc:3957
exception_site=src\xenia\cpu\backend\a64\a64_backend.cc:3128
lookup_site=src\xenia\cpu\function.cc:92
```

The audit confirms:

- normal `machine_code()` entry is unchanged;
- global indirection and `ResolveFunction` still use normal entry;
- direct callsites do not test `guest_call_fast_entry_code()` or fast-entry
  contracts;
- payload population, dirty flush codegen, fast-entry stub codegen, and
  alternate callsite behavior are absent;
- no Thor route capture or speed claim exists for this source-only state.

## Decision

This closes the current fast-entry source-only planning chain. The remaining
fast-entry behavior work is a real generated-code design problem, not another
micro-audit. Do not run Thor speed A/B or patch callsites from this state.

Next work should switch to a non-fast-entry structural A64 lane, or first run a
compile-time route-ranking audit that identifies a different body-dominant
backend class before any risky Thor capture.
