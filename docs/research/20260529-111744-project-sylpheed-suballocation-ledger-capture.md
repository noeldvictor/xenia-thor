# 2026-05-29 11:17 EDT - Project Sylpheed Suballocation Ledger Capture

## Context

This follows `20260529-105300-project-sylpheed-physical-suballocation-audit.md`.
The goal was to run the real game-library Project Sylpheed route with the new
physical suballocation ledger enabled before any heap ownership behavior patch.

## Route

Build/deploy needed the existing Windows split workaround:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShellDeploy -DeviceSerial c3ca0370 -NoSubst
```

The plain `FullDeploy` path hit Gradle `!directory.isDirectory` through the
subst path, while `FullDeploy -NoSubst` hit the NDK space-path makefile issue.

Deployed APK SHA256:

```text
8FA0750C1D87C4FFBB1C7740C0C116E8C81F66AD032B1AA00D5CF4E5B422971F
```

Launch path was the real library UI, not direct `EmulatorActivity`. I selected
Project Sylpheed from the cover shelf, chose `Start game`, skipped the intro
with the debug gamepad Start button, and selected New Game with A.

Launcher diagnostics:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchLauncher -DisassembleFunctionFilter "824AB214,824AC578,8272D048,82762158" -Arm64SpeedProfileIntervalMs 5000 -Arm64SpeedProfileThreadSnapshot true -Arm64SpeedProfileThreadSnapshotOnIdle true -XboxkrnlThreadWaitTrace true -XboxkrnlThreadWaitTraceBudget 2048 -XboxkrnlThreadWaitTraceAfterMs 0 -XboxkrnlThreadWaitTraceGuestTids "00000006,00000009,0000000A" -XboxkrnlPhysicalMemoryAudit true -XboxkrnlPhysicalMemoryAuditBudget 512 -XboxkrnlPhysicalSuballocationAudit true -XboxkrnlPhysicalSuballocationAuditBudget 512
```

Proof packet:

```text
scratch/thor-debug/remote-debug-20260529-111744
```

The final screen is black with the guest-swap FPS badge still at about 61 FPS,
so presentation is still being swapped even though visible output is black.

## Reports

Generated:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_loading_loop_audit.ps1 -PacketDir scratch\thor-debug\remote-debug-20260529-111744 -OutPath scratch\thor-debug\20260529-111744-sylpheed-loading-loop-audit.txt
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_physical_heap_release_audit.ps1 -PacketDir scratch\thor-debug\remote-debug-20260529-111744 -OutPath scratch\thor-debug\20260529-111744-sylpheed-physical-heap-release-audit.txt
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_free_site_flow_join.ps1 -PacketDir scratch\thor-debug\remote-debug-20260529-111744 -OutPath scratch\thor-debug\20260529-111744-sylpheed-free-site-flow-join.txt
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir scratch\thor-debug\remote-debug-20260529-111744 -OutPath scratch\thor-debug\20260529-111744-sylpheed-late-pc-wait-map.txt
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_physical_suballocation_audit.ps1 -PacketDir scratch\thor-debug\remote-debug-20260529-111744 -OutPath scratch\thor-debug\20260529-111744-sylpheed-physical-suballocation-audit.txt
```

## Findings

`status-report.txt` stayed scoped to Project Sylpheed heap failures with no
scoped crash:

```text
classification=project_sylpheed_heap_release_failures_no_crash_marker
base_heap_release_count=19
physical_heap_count=19
native_signal_count=0
guest_crash_count=0
```

The new suballocation ledger report says:

```text
classification=project_sylpheed_suballocation_children_seen_no_owner_release
audit_row_count=53
interior_request_count=19
region_start_request_count=34
callback_query_unsafe_for_child_count=19
ledger_owner_fully_released_by_bounds_count=0
current_parent_release_would_succeed_count=0
current_physical_release_would_succeed_count=0
current_callback_would_fire_count=0
behavior_changed_count=0
owner_top=BC220000:03A80000:19
```

The key owner row is:

```text
owner=BC220000:03A80000 rows=19 interior=19 region_start=0 unique_children=25 duplicates=0 min_offset=0057C900 max_offset=038F2240 bounds_span=03385940 fully_released_by_bounds=0 parent_release_ok=0 physical_release_ok=0 callback_would_fire=0 callback_unsafe=19
```

The physical heap report agrees:

```text
classification=project_sylpheed_single_owner_interior_physical_heap_frees
physical_memory_audit_failed_interior_result_count=19
failed_interior_owner_group_count=1
top_failed_interior_owner_group=BC220000:03A80000
```

This rules out a simple round-down or bounds-complete owner release. The child
frees are real and grouped, but their min/max bounds do not cover the whole
owner allocation and the current callback query shape is unsafe for child
releases.

The wait trace budget was consumed before the final failed free:

```text
wait_trace_first_time=11:13:27.126
wait_trace_last_time=11:13:30.716
last_failed_free_time=11:14:55.454
wait_budget_exhausted_before_last_failed_free=1
vdswap_after_last_failed_free_count=10587
```

The late-PC report therefore recommends a delayed wait trace:

```text
recommended_wait_trace_after_ms=80000
recommended_wait_trace_guest_tids=00000009,00000006,0000000A
```

## Decision

Do not hide, round down, or owner-release the failed interior physical frees
from this evidence. The safe next step is a delayed post-free wait/progress
capture or source-only design for an explicit physical owner/child contract
that preserves callback invalidation semantics. A behavior patch still needs
proof that child release, not an independent presentation/wait state, is the
blocking compatibility issue.

## Validation

```powershell
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_project_sylpheed_physical_suballocation_audit.ps1', [ref]$null, [ref]$errors)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_physical_suballocation_audit.ps1 -PacketDir scratch\thor-debug\remote-debug-20260529-111744 -OutPath scratch\thor-debug\20260529-111744-sylpheed-physical-suballocation-audit.txt
```
