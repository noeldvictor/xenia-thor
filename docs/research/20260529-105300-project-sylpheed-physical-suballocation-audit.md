# 2026-05-29 10:53 EDT - Project Sylpheed Physical Suballocation Audit

## Context

This follows `20260529-104300-project-sylpheed-heap-wait-contract.md`.
Project Sylpheed New Game is currently a live black/wait loop with continuing
VdSwap and repeated interior physical frees under owner
`BC220000:03A80000`.

## Change

Added default-off `xboxkrnl_physical_suballocation_audit` with
`xboxkrnl_physical_suballocation_audit_budget`.

The audit is behavior-neutral:

- `MmFreePhysicalMemory` behavior is unchanged.
- Exact allocation and region-start frees remain unchanged.
- Interior frees are logged as child offsets under their physical owner
  allocation.
- The ledger tracks per-owner interior free count, unique child offsets,
  duplicates, min/max child offsets, rough bounds span, and whether bounds alone
  would cover the whole owner.
- Rows also report whether the current release path would reach parent release,
  physical release, and callbacks, plus whether using the current callback
  `QuerySize` shape for a child free would be unsafe.

The real launcher/game-library path can now receive:

```powershell
-XboxkrnlPhysicalSuballocationAudit true
-XboxkrnlPhysicalSuballocationAuditBudget 512
```

Android launcher settings, `LauncherActivity`, `EmulatorActivity`, and
`tools/thor/thor_xenia_debug.ps1` all forward the new cvars. The Sylpheed late
PC wait-map tool now includes them in its recommended follow-up command.

## Expected Row

```text
Xboxkrnl physical suballocation audit free phase request ... owner_physical_base ... owner_size ... request_offset ... request_interior ... ledger_interior_unique_count ... ledger_owner_fully_released_by_bounds ... current_parent_release_would_succeed ... current_callback_would_fire ... callback_query_unsafe_for_child ... behavior_changed 0
```

## Decision

Next runtime proof should use the real game library with physical memory audit,
physical suballocation audit, wait trace, and A64 snapshots enabled. If the
ledger shows many child frees but no owner-full-release proof, the safe behavior
candidate is still not round-down release; it must be an explicit owner/child
contract.

## Validation

```powershell
$errors = $null; [void][System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_xenia_debug.ps1', [ref]$null, [ref]$errors)
$errors = $null; [void][System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1', [ref]$null, [ref]$errors)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir scratch\thor-debug\remote-debug-20260529-103800-sylpheed-filtered-late-window -OutPath scratch\thor-debug\20260529-103800-sylpheed-late-pc-wait-map.txt
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell -NoSubst
```
