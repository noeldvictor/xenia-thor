# 2026-05-26 - Guest-Call Callee-Aware Audit

## Context

Current Blue Dragon captures remain Main Thread / A64 generated-code bound, not
broad Vulkan bound. The active fast-entry lane was blocked because the Thor
compile-time `arm64_guest_call_fast_entry_audit` row for `0x82281D28` saw direct
call traffic but reported no known callee first-use information:

- `direct_calls=84`
- `eligible_regular=67`
- `arg_store_fields=421`
- `already_compiled_targets=32`
- `callee_first_use_known=0`
- `callee_first_use_missing=421`
- `normal_entry_fallback=67`
- `flush_context_barrier=260`

The goal of this slice was to make the existing offline HIR audit callee-aware
before any behavior work, not to run another Thor capture or speed A/B.

## Changes

Updated `tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1`:

- Adds `-FastEntryAuditLogPath` to ingest the route-clean compile-time fast-entry
  audit row from `scratch/thor-debug/20260526-043446-speed-logcat.txt`.
- Adds per-target summaries with `live_in`, `first_store`, `absent`,
  `missing`, body-weighted argument fields, body-weighted live-in fields, and
  body-weighted missing fields.
- Keeps output behavior-neutral: `payload_materializations_allowed=0` and
  `behavior_changed=0`.

Validation:

```powershell
$tokens=$null; $errors=$null; $null=[System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_hir_guest_call_arg_handoff_audit.ps1',[ref]$tokens,[ref]$errors); if ($errors -and $errors.Count -gt 0) { $errors | Format-List | Out-String; exit 1 }; 'parser-ok'
```

Result: `parser-ok`.

## Audit Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_guest_call_arg_handoff_audit.ps1 -LogPath scratch\thor-debug\20260526-025401-speed-logcat.txt -ExtraLogPath scratch\thor-debug\20260526-034119-speed-logcat.txt -FastEntryAuditLogPath scratch\thor-debug\20260526-043446-speed-logcat.txt -Function 82281D28 -Phase OptHIR -Top 16
```

Saved output:

`scratch/thor-debug/20260526-044800-guest-call-callee-aware-audit.txt`

## Results

The offline callee-aware join now shows real body-weighted known live-in traffic:

- `direct_ppc_bl_calls=81`
- `argument_store_fields=439`
- `callee_first_load_stores=247`
- `callee_first_store_stores=51`
- `callee_absent_stores=15`
- `callee_hir_missing_stores=126`
- `normal_entry_fallback_required=81`
- `body_weighted_argument_fields=748407618`
- `body_weighted_live_in_fields=690421033`
- `body_weighted_missing_fields=30011199`

Top target summaries:

- `0x82281D28`: `calls=2`, `stores=18`, `live_in=18`, `missing=0`,
  `body_weighted_live_in=401150187`, top call `82282370->0x82281D28`.
- `0x826BF770`: `calls=11`, `stores=57`, `live_in=44`, `first_store=13`,
  `missing=0`, `body_weighted_live_in=206739640`, top call
  `8228234C->0x826BF770`.
- `0x822750F8`: `calls=5`, `stores=45`, `live_in=45`, `missing=0`,
  `body_weighted_live_in=42703776`.
- `0x82276E70`: still missing HIR for the body-weighted candidate
  `8228219C->0x82276E70`, with `stores=9`, `missing=9`,
  `body_weighted_missing=20915802`.

The joined compile-time row still blocks behavior work:

- `callee_first_use_known=0`
- `callee_first_use_missing=421`
- `normal_entry_fallback=67`
- `stackpoint_sensitive=67`
- `dirty_flush_points=268`
- `flush_context_barrier=260`
- `payload_materializations_allowed=0`
- `behavior_changed=0`
- `alternate_codegen=0`
- `normal_entry=unchanged`
- `global_indirection=unchanged`

The continual harness window review was also refreshed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_continual_harness_review.ps1 -Mode Window
```

Output: `scratch/thor-debug/continual-harness-window.md`.

## Decision

Do not implement guest-call fast-entry behavior from this evidence and do not run
a quiet speed A/B. The offline callee-aware report proves material known live-in
traffic, but the runtime compile audit still has no per-target first-use
knowledge and still reports high dirty flush / context-barrier pressure.

The fast-entry lane is not speed-proven. If continued, the next useful slice is a
default-off runtime target-row audit for direct guest calls that emits per-call
target, blocker, already-compiled/resolved state, parent argument-store fields,
dirty flush points, and normal-entry fallback pressure. That slice must have a
hard stop: if target rows do not show broad known first-use traffic with
manageable flush pressure, close fast-entry behavior and move to another
structural A64 lane.
