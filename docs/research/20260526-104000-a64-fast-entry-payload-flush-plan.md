# A64 Fast-Entry Payload/Flush Plan

Date: 2026-05-26 10:40 local

## Context

The previous source-only fast-entry slices added:

- separate `A64Function` fast-entry storage and payload/dirty-flush contract;
- a default-off stub skeleton;
- a direct-call guard decision model.

Generated behavior is still intentionally unchanged. Normal `machine_code()`,
direct calls, global indirection, payload population, dirty flush codegen, stub
codegen, callsite guards, and stackpoint resume are all absent/unchanged.

No Thor route capture or speed A/B was run for this slice.

## Change

Added a behavior-neutral source planner in `A64Function`:

- `A64GuestCallFastEntryPayloadFlushPlanBlocker`
- `A64GuestCallFastEntryPayloadFlushPlanInputs`
- `A64GuestCallFastEntryPayloadFlushPlan`
- `BuildA64GuestCallFastEntryPayloadFlushPlan`

The planner remains conservative. It blocks if any required source payload,
writable payload slot, dirty-flush coverage, context writeback, stackpoint
resume flush, or debug/exception flush protocol is missing.

Added deterministic audit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_fast_entry_payload_flush_plan_audit.ps1 |
  Tee-Object -FilePath scratch\thor-debug\20260526-104000-a64-fast-entry-payload-flush-plan-audit.txt
```

## Audit Result

The audit reports:

- `payload_flush_plan_blockers_cover_required_classes=true`
- `payload_flush_plan_inputs_cover_protocols=true`
- `payload_flush_plan_model_present=true`
- `required_payload_mask_still_r3_r10_lr=true`
- `required_dirty_flush_mask_still_all_boundaries=true`
- `guard_protocol_still_present=true`
- `normal_entry_abi_still_return_x0_only=true`
- `direct_calls_still_use_normal_entry=true`
- `payload_flush_plan_unused_by_generated_code=true`
- `generated_payload_behavior_absent=true`
- `generated_dirty_flush_behavior_absent=true`
- `fast_entry_callsite_behavior_absent=true`
- `decision=payload_flush_plan_source_ready_behavior_blocked`

## Decision

This is source/protocol progress only. It is not route proof and not speed
proof.

Do not patch direct callsites, replace `A64Function::machine_code`, rewrite
global indirection, materialize payload state, emit dirty flush codegen, or run
a Thor speed A/B from this evidence.

Safe next work, if this lane continues:

- no-op codegen protocol helpers with generated behavior unchanged; or
- switch lanes if the helper work does not produce a reusable backend rule.

Keep using `-Arm64OffsetMemoryAddressFastpath true` explicitly in future Blue
Dragon speed captures because the offset helper has route-clean local positive
evidence, while keeping that cvar default-off globally.

## Validation

- PowerShell parser checks passed for the new audit, related fast-entry audits,
  and `tools/thor/thor_codex_goal_loop.ps1`.
- `tools/thor/thor_a64_fast_entry_payload_flush_plan_audit.ps1` passed.
- `tools/thor/thor_a64_fast_entry_guard_protocol_audit.ps1` passed.
- `tools/thor/thor_a64_fast_entry_dirty_flush_protocol_audit.ps1` passed.
- `tools/thor/thor_a64_guest_call_fast_entry_feasibility.ps1` passed.
- `git diff --check` passed with CRLF conversion warnings only.
- `tools/thor/thor_build.ps1 -Mode NativeCore` passed.
