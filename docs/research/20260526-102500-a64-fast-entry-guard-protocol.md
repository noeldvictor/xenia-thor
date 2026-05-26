# A64 Fast-Entry Guard Protocol

Date: 2026-05-26 10:25 local

## Context

This follows `docs/research/20260526-101000-a64-fast-entry-stub-skeleton.md`.
The fast-entry lane remains behavior-neutral source work. The goal of this
slice was to define the guard inputs and blocker classes that a future direct
callsite must satisfy before it can branch to any alternate fast-entry path.

No Thor route capture or quiet speed A/B was run.

## Changes

- Added `A64GuestCallFastEntryGuardBlocker` with explicit blocker classes for:
  behavior disabled, incomplete contract, missing fast-entry code, non-direct
  call, unresolved target, missing payload population, missing dirty flush,
  incomplete dirty-flush coverage, missing late-bound fallback, missing
  stackpoint resume, and debug/exception visibility.
- Added `A64GuestCallFastEntryGuardInputs` to model the runtime/source contracts
  a direct-call guard would need.
- Added `A64GuestCallFastEntryGuardDecision` and
  `EvaluateA64GuestCallFastEntryGuard`.
- Added `tools/thor/thor_a64_fast_entry_guard_protocol_audit.ps1` to verify the
  model and prove generated behavior remains unchanged.

## Audit

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_fast_entry_guard_protocol_audit.ps1 |
  Tee-Object -FilePath scratch\thor-debug\20260526-102500-a64-fast-entry-guard-protocol-audit.txt
```

Key checks:

- `guard_blockers_cover_required_classes=true`
- `guard_inputs_cover_runtime_contracts=true`
- `guard_decision_model_present=true`
- `guard_evaluator_blocks_missing_protocols=true`
- `guard_model_unused_by_codegen=true`
- `direct_callsite_guard_absent=true`
- `payload_population_codegen_absent=true`
- `dirty_flush_codegen_absent=true`
- `fast_entry_stub_codegen_absent=true`
- `decision=guard_protocol_source_ready_behavior_blocked`

Prior runtime blockers still apply:

- `unresolved_direct_targets=52`
- `normal_entry_fallback=67`
- `stackpoint_sensitive=67`
- `dirty_flush_points=268`
- `flush_context_barrier=260`
- `parent_pre_call_flush_points=195`
- `callee_first_use_known=0`

## Decision

The source guard protocol is now explicit, but behavior remains blocked.

Do not patch callsite branches, `A64Function::machine_code`, global indirection,
payload materialization, or run a speed A/B from this source-only state.

Next useful fast-entry work is either:

- behavior-neutral source design for payload population and dirty-flush codegen;
- or a lane switch if the payload/flush design cannot produce a reusable backend
  rule.

## Validation

- PowerShell parser checks passed for the new guard audit and updated Thor
  scripts.
- `tools/thor/thor_a64_fast_entry_guard_protocol_audit.ps1` passed and wrote
  `scratch/thor-debug/20260526-102500-a64-fast-entry-guard-protocol-audit.txt`.
- `tools/thor/thor_a64_fast_entry_dirty_flush_protocol_audit.ps1` passed.
- `tools/thor/thor_a64_guest_call_fast_entry_feasibility.ps1` passed.
- `git diff --check` passed with CRLF conversion warnings only.
- `tools/thor/thor_build.ps1 -Mode NativeCore` passed.
