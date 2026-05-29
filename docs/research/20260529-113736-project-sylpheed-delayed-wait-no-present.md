# 2026-05-29 11:37 EDT - Project Sylpheed Delayed Wait / No-Present

## Context

This follows `20260529-111744-project-sylpheed-suballocation-ledger-capture.md`.
The previous packet proved Project Sylpheed's interior physical frees are real
child frees under one owner allocation, but it did not prove that releasing the
owner or hiding those frees is safe.

## Route

Used the existing deployed APK from commit `7738c886d`; current repo commit for
the capture packet was `532e3e755` with only host-side/reporting changes.

Launch path was the real game-library cover shelf. The launcher received a
delayed post-free wait trace:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchLauncher -DisassembleFunctionFilter "824AB214,824AC578,8272D048,82762158" -Arm64SpeedProfileIntervalMs 5000 -Arm64SpeedProfileThreadSnapshot true -Arm64SpeedProfileThreadSnapshotOnIdle true -XboxkrnlThreadWaitTrace true -XboxkrnlThreadWaitTraceBudget 2048 -XboxkrnlThreadWaitTraceAfterMs 80000 -XboxkrnlThreadWaitTraceGuestTids "00000009,00000006,0000000A" -XboxkrnlPhysicalMemoryAudit true -XboxkrnlPhysicalMemoryAuditBudget 512 -XboxkrnlPhysicalSuballocationAudit true -XboxkrnlPhysicalSuballocationAuditBudget 512
```

Proof packet:

```text
scratch/thor-debug/remote-debug-20260529-113736
```

The final screenshot is a black frame with the guest-swap FPS badge showing
`0.0 FPS`.

## Tooling Update

`tools/thor/thor_project_sylpheed_loading_loop_audit.ps1` now reads packet
`meta.txt`, reports first/last `VdSwap` times, and classifies stale swap streams
as no-present stalls when the last swap is older than the screenshot packet.

## Findings

Updated loading-loop report:

```text
classification=project_sylpheed_black_no_present_stall_no_scoped_crash
presentation_evidence=vdswap_stale_no_present
vdswap_count=2549
vdswap_first_time=11:34:29.463
vdswap_last_time=11:35:19.199
packet_created_at=2026-05-29T11:37:37.7403397-04:00
seconds_since_last_vdswap=138.541
native_abort_marker_count=0
invalid_pthread_join_count=0
```

The delayed wait trace is now after the final failed free:

```text
last_failed_free_time=11:35:17.570
wait_trace_first_time=11:35:48.819
wait_trace_last_time=11:35:49.969
wait_trace_after_last_failed_free_count=2048
```

Wait groups are timeout-heavy:

```text
thread=00000006 api=NtWaitForSingleObjectEx status=00000102 lr=824AC578 ctr=82506708 count=882
thread=00000009 api=NtWaitForMultipleObjectsEx status=00000102 lr=824AB214 ctr=00000000 count=71
thread=0000000A api=NtWaitForMultipleObjectsEx status=00000102 lr=824AB214 ctr=00000000 count=71
```

A64 snapshots keep the post-free wait PCs alive after presentation stops:

```text
target_pc[0]=824AB214 post_failed_snapshot_last_ret=174
target_pc[1]=824AC578 post_failed_snapshot_last_ret=199
```

Physical ownership remains unsafe for behavior:

```text
classification=project_sylpheed_suballocation_children_seen_no_owner_release
interior_request_count=21
owner_top=BC220000:03A80000:21
unique_children=33
ledger_owner_fully_released_by_bounds_count=0
callback_query_unsafe_for_child_count=21
behavior_changed_count=0
```

The free-site flow report lost PPC table matches in this shorter tail because
the log tail no longer contains the earlier filtered PPC/HIR dump, but the
previous `20260529-111744` packet already has the filtered dump proof. This
packet's value is the delayed wait/no-present timing.

## Decision

Do not patch physical frees yet. The newest route no longer looks like a live
presentation loop; it is a stale/no-present stall after the late child-free
cluster. The next useful slice is to join the presentation stop to guest wait
objects or GPU/presenter state:

- add/report last `VdSwap` caller and return status around the stop,
- map wait object handles for `824AC578` / `824AB214`,
- or add a targeted post-free sampler for the threads that keep timing out.

Any owner/child physical-memory behavior must stay source-only until a route
proves child release is causal and preserves callback invalidation semantics.

## Validation

```powershell
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_project_sylpheed_loading_loop_audit.ps1', [ref]$null, [ref]$errors)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_loading_loop_audit.ps1 -PacketDir scratch\thor-debug\remote-debug-20260529-113736 -OutPath scratch\thor-debug\20260529-113736-sylpheed-loading-loop-audit.txt
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_physical_suballocation_audit.ps1 -PacketDir scratch\thor-debug\remote-debug-20260529-113736 -OutPath scratch\thor-debug\20260529-113736-sylpheed-physical-suballocation-audit.txt
```
