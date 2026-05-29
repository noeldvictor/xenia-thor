# 2026-05-29 16:00 EDT - Project Sylpheed VdSwap Probe Live Presenter

## Context

This slice followed `20260529-191800-project-sylpheed-vdswap-gpu-state-probe-prep.md`.
The goal was to run a fresh real-library Project Sylpheed packet with
`gpu_trace_vd_swap` enabled so the earlier stale no-present proof could be
joined to exact `VdSwap` enter/exit rows and compact presenter state.

## Patch

Added Android launch plumbing for the default-off probe:

- `tools/thor/thor_xenia_debug.ps1`: `-GpuTraceVdSwap` parameter, direct emulator
  extra, real-library `LaunchLauncher` extra, metadata line, and Blue Dragon speed
  reset coverage.
- `EmulatorActivity`: copies `gpu_trace_vd_swap` into native launch cvars.
- `LauncherActivity`: forwards `gpu_trace_vd_swap` through debug real-library
  launches.
- `XeniaAndroidSettings`: keeps `gpu_trace_vd_swap` false for stable normal
  app launches.

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell -NoSubst
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode Install
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchLauncher -DisassembleFunctionFilter "824AB214,824AC578,8272D048,82762158" -Arm64SpeedProfileIntervalMs 5000 -Arm64SpeedProfileThreadSnapshot true -Arm64SpeedProfileThreadSnapshotOnIdle true -XboxkrnlThreadWaitTrace true -XboxkrnlThreadWaitTraceBudget 2048 -XboxkrnlThreadWaitTraceAfterMs 80000 -XboxkrnlThreadWaitTraceGuestTids "00000009,00000006,0000000A" -XboxkrnlPhysicalMemoryAudit true -XboxkrnlPhysicalMemoryAuditBudget 512 -XboxkrnlPhysicalSuballocationAudit true -XboxkrnlPhysicalSuballocationAuditBudget 512 -GpuTraceVdSwap true
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenshot -OutDir scratch\thor-debug\remote-debug-20260529-160000-vdswap-probe -LogcatTailLines 200000
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir 'scratch\thor-debug\remote-debug-20260529-160000-vdswap-probe' -OutPath 'scratch\thor-debug\20260529-160000-sylpheed-vdswap-probe-join.txt'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_physical_suballocation_audit.ps1 -PacketDir 'scratch\thor-debug\remote-debug-20260529-160000-vdswap-probe' -OutPath 'scratch\thor-debug\20260529-160000-sylpheed-physical-suballocation-audit.txt'
adb -s c3ca0370 shell am force-stop jp.xenia.emulator.github.debug
```

APK:

```text
android/android_studio_project/app/build/outputs/apk/github/debug/app-github-debug.apk
sha256=09A886D7F4D12FF15B1B9F4B48D696A5E964B2FC1ADFDA398B28E8306430161D
```

## Packet

```text
scratch/thor-debug/remote-debug-20260529-160000-vdswap-probe
```

The game was launched from the real game library card for Project Sylpheed. The
final screenshot is black, but the overlay reads `59.2 FPS`, not the prior
stale `0.0 FPS`.

## Findings

This run does not reproduce the earlier presenter-stop condition. Instead, the
instrumented rows show `VdSwap` and presenter issue state continuing through
capture:

```text
vdswap_last_time=15:58:44.182
vdswap_last_caller=F8000084
vdswap_caller_counts=F8000084:19550,F8000028:2
vdswap_last_caller_status_count=9775
vdswap_last_caller_status_before=caller=F8000084 status=00000103 mapped=000003E5 line=77124 time=15:58:44.165 delta_ms=-17.0
vdswap_last_caller_status_after=caller=F8000084 status=00000103 mapped=000003E5 line=77131 time=15:58:44.182 delta_ms=0.0
gpu_presenter_event_count=9789
gpu_presenter_after_last_vdswap_count=1
gpu_presenter_first_after_vdswap=kind=VulkanPresenter line=77135 time=15:58:44.188 delta_ms=6.0 text=xenia : i> F8000004 VulkanPresenter: IssueSwap compact state refreshed=1 frame_current=9776 frame_completed=9774 submission=9776
```

Wait objects remain visible after the failed frees, but they now coexist with
continuing swaps:

```text
wait_trace_count=2048
wait_trace_first_time=15:57:19.594
wait_trace_last_time=15:57:23.793
post_failed_target_wait_handle_count=4
target_pc[0]=824AB214 ... wait_threads=00000009,0000000A ... wait_statuses=00000000:519,00000102:519
target_pc[1]=824AC578 ... wait_threads=00000006 ... wait_statuses=00000000:1010
```

Physical frees remain behavior-blocked in the same direction as prior packets:

```text
classification=project_sylpheed_suballocation_children_seen_no_owner_release
interior_request_count=15
callback_query_unsafe_for_child_count=15
ledger_owner_fully_released_by_bounds_count=0
current_parent_release_would_succeed_count=0
current_physical_release_would_succeed_count=0
behavior_changed_count=0
owner=BC220000:03A80000 rows=15 interior=15 region_start=0 unique_children=21 ... fully_released_by_bounds=0 callback_unsafe=15
```

## Decision

Do not round down, hide, or owner-release physical frees. This fresh packet says
the live black-screen state can happen with active `VdSwap`/presenter progress
and no native crash marker, so the earlier `0.0 FPS` stale no-present packet is
not the only black-screen mode.

The next useful slice is to keep `gpu_trace_vd_swap` on and add a compact
frontbuffer/present-source sampler near `IssueSwap`, or rerun with the same probe
and timed screenshots to separate:

- stale presenter stop (`0.0 FPS`, no post-stop presenter rows), and
- live black presenter (`~60 FPS`, continuing `VdSwap`/presenter rows).

## Validation

```text
PowerShell parser: tools/thor/thor_xenia_debug.ps1 passed.
ApkShell: passed.
Install: passed on c3ca0370.
Remote screenshot/log packet: captured.
Late-PC wait map: passed.
Physical suballocation audit: passed.
```
