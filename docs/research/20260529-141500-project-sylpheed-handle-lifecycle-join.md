# 2026-05-29 14:15 EDT - Project Sylpheed Handle Lifecycle Join

## Context

This follows `20260529-113736-project-sylpheed-delayed-wait-no-present.md`.
Project Sylpheed still stalls at black `0.0 FPS` after `VdSwap` ends, with delayed
wait evidence proving timeout waits continue after the final failed free.

## Route

Using the same packet `scratch/thor-debug/remote-debug-20260529-113736`, I reran
`thor_project_sylpheed_late_pc_wait_map.ps1` after adding handle-lifecycle
correlation output:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir 'scratch\thor-debug\remote-debug-20260529-113736' -OutPath 'scratch\thor-debug\20260529-134500-sylpheed-late-pc-wait-map-post-free.txt'
```

## Findings

The output now maps both target-PC wait handles and post-failed-handle rows to
kernel object lifecycle rows for the three active handles, especially
`824AC578`/`824AB214` entries:

- `post_failed_target_handle[0]` (`F8000260`) now has `lifecycle_rows=3`
  (`ADDED:2, REMOVED:1`) with owners `F8000028:2,F80001A8:1` and types
  `N2XE6KERNEL7XOBJECTE:2,N2XE6KERNEL6XEVENTE:1`.
- `post_failed_target_handle[1]` (`F8000054`) has `lifecycle_rows=1` with
  owner `F8000028` and type `N2XE6KERNEL7XOBJECTE`.
- `post_failed_target_handle[2]` (`F8000068`) has `lifecycle_rows=1` with owner
  `F800005C` and type `N2XE6KERNEL7XOBJECTE`.
- `target_pc[1]_wait_handle[0]` (`pc=824AC578`, `handle=F8000260`) now includes the
  same lifecycle summary and three sampled lifecycle rows.
- The handle is not added after the final wait loop start, and there is a `REMOVED`
  event at `11:35:17.407` by `F8000028` shortly before late waits begin.

Interpretation: these waits are on handles with mixed lifecycle churn but no fresh
post-stop handle allocation pattern; this supports pivoting to present/GPU state
or VdSwap caller-chain continuation next, with the same handle IDs.

## Validation

```powershell
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1'), [ref]$null, [ref]$errors)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir 'scratch\thor-debug\remote-debug-20260529-113736' -OutPath 'scratch\thor-debug\20260529-134500-sylpheed-late-pc-wait-map-post-free.txt'
```

