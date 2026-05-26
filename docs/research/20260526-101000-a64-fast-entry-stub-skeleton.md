# A64 Fast-Entry Stub Skeleton

Date: 2026-05-26 10:10 local

## Context

This follows `docs/research/20260526-094500-a64-fast-entry-dirty-flush-protocol.md`.
The fast-entry lane remains source-only. The goal of this slice was to add a
behavior-neutral skeleton that makes the future guarded-stub contract explicit
without changing generated code, direct calls, normal entry, or global
indirection.

No Thor route capture or quiet speed A/B was run.

## Changes

- Added `A64GuestCallFastEntryFlags` with separate skeleton and behavior bits.
- Added required payload and dirty-flush masks for the complete `r3-r10/lr`
  payload plus the current required flush boundary set.
- Added `MakeA64GuestCallFastEntryStubSkeletonContract`,
  `A64GuestCallFastEntryContractCoversStubSkeleton`, and
  `A64GuestCallFastEntryContractEnablesBehavior`.
- Added `A64Function::SetupGuestCallFastEntryStubSkeleton`, but no callsite uses
  it yet.
- Added the default-off cvar `arm64_guest_call_fast_entry_stub_skeleton` and
  Android/Thor launch plumbing. Enabling the cvar only logs that alternate
  codegen, direct calls, global indirection, and behavior are unchanged.
- Extended
  `tools/thor/thor_a64_fast_entry_dirty_flush_protocol_audit.ps1` to verify the
  skeleton and cvar while still requiring generated behavior to be absent.
- Tightened
  `tools/thor/thor_a64_guest_call_fast_entry_feasibility.ps1` so the
  default-off skeleton cvar is not classified as generated fast-entry behavior.

## Audit

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_fast_entry_dirty_flush_protocol_audit.ps1 |
  Tee-Object -FilePath scratch\thor-debug\20260526-101000-a64-fast-entry-stub-skeleton-audit.txt
```

Key checks:

- `stub_skeleton_protocol_present=true`
- `stub_skeleton_default_off_cvar_present=true`
- `direct_calls_still_use_normal_entry=true`
- `late_bound_paths_still_use_normal_entry=true`
- `payload_population_codegen_absent=true`
- `dirty_flush_codegen_absent=true`
- `fast_entry_stub_codegen_absent=true`
- `direct_callsite_guard_absent=true`
- `stackpoint_resume_protocol_absent=true`
- `decision=stub_skeleton_source_ready_but_behavior_blocked`

## Decision

The source skeleton is now explicit enough for the next source-design step, but
behavior remains blocked.

Do not patch direct callsites, `A64Function::machine_code`, global indirection,
payload materialization, or run a quiet speed A/B from this source-only patch.

Next useful fast-entry work must stay behavior-neutral unless it first proves:

- direct-call guard and normal-entry fallback rules;
- payload population from parent context or live values;
- dirty flush codegen to `PPCContext` before each named boundary;
- late-bound/unresolved target behavior;
- stackpoint resume invalidation or flush behavior.

## Validation

- PowerShell parser checks passed for the updated Thor scripts.
- Updated protocol audit passed and wrote
  `scratch/thor-debug/20260526-101000-a64-fast-entry-stub-skeleton-audit.txt`.
- `tools/thor/thor_a64_guest_call_fast_entry_feasibility.ps1` passed after the
  skeleton-cvar behavior classification fix.
- `tools/thor/thor_a64_guarded_stub_entry_design_audit.ps1` passed.
- `git diff --check` passed with CRLF conversion warnings only.
- `tools/thor/thor_build.ps1 -Mode NativeCore` passed.
- `tools/thor/thor_build.ps1 -Mode ApkShell` passed.
