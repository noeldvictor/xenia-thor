# 2026-05-29 09:45 EDT - Project Sylpheed Late-PC Capture Plan

## Context

This continues `20260529-010500-project-sylpheed-late-pc-wait-map.md`.
The repaired library packet `scratch/thor-debug/remote-debug-20260528-180311`
still proves a live black/loading state, but the wait trace budget was consumed
before the post-free black-loop window and the late PCs had no filtered PPC/HIR
dumps.

## Change

Updated `tools/thor/thor_project_sylpheed_late_pc_wait_map.ps1` so the report
now emits a concrete next-capture recipe instead of only saying that another
capture is needed.

The tool now derives:

- the first wait-trace timestamp,
- a stable delayed wait-trace start time relative to the first traced wait,
- a focused guest-thread filter,
- a bounded remote-debug logcat tail size for the larger disassembly packet,
- exact launcher and follow-up capture commands.

## Proof

Validation:

```powershell
$errors = $null; [System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1', [ref]$null, [ref]$errors)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir scratch\thor-debug\remote-debug-20260528-180311 -OutPath scratch\thor-debug\20260528-180311-sylpheed-late-pc-wait-map-capture-plan.txt
```

Key output:

- `classification=project_sylpheed_late_pc_wait_map_needs_capture`
- `recommended_disassemble_function_filter=824AB214,824AC578,8272D048,82762158`
- `wait_trace_first_time=18:02:10.655`
- `last_failed_free_time=18:03:02.018`
- `recommended_wait_trace_after_ms=45000`
- `recommended_wait_trace_guest_tids=00000006,00000009,00000008,0000000A`
- `recommended_wait_trace_budget=2048`
- `recommended_remote_debug_logcat_tail_lines=200000`

Recommended launch command from the report:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchLauncher -DisassembleFunctionFilter "824AB214,824AC578,8272D048,82762158" -Arm64SpeedProfileIntervalMs 5000 -Arm64SpeedProfileThreadSnapshot true -Arm64SpeedProfileThreadSnapshotOnIdle true -XboxkrnlThreadWaitTrace true -XboxkrnlThreadWaitTraceBudget 2048 -XboxkrnlThreadWaitTraceAfterMs 45000 -XboxkrnlThreadWaitTraceGuestTids "00000006,00000009,00000008,0000000A" -XboxkrnlPhysicalMemoryAudit true -XboxkrnlPhysicalMemoryAuditBudget 512
```

Recommended follow-up packet command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenshot -LogcatTailLines 200000
```

## Decision

Do not patch heap ownership or presentation behavior from the current packet.
The next runtime pass should use the generated delayed/thread-filtered
game-library route, then rerun the late-PC wait map, loading-loop audit, and
free-site flow join on the new packet.
