# Project Sylpheed Library Wait/Progress Launch Plumbing

## Summary

The previous loading-loop join audit showed Project Sylpheed is alive in a
near-black loading/presentation loop:

```text
classification=project_sylpheed_live_loading_guest_cpu_vdswap_no_crash
guest_cpu_sum=126.5
guest_top_thread=XThread3ACD6CB0
vdswap_count=19975
wait_trace_count=0
a64_thread_snapshot_count=0
```

The missing piece was not native instrumentation. The cvars already exist:

```text
arm64_speed_profile_thread_snapshot
xboxkrnl_thread_wait_trace
```

`EmulatorActivity` already knows how to pass them to native code, and
`thor_xenia_debug.ps1` already supports them for direct emulator launches. The
real app-library route did not forward those debug extras through
`LauncherActivity`, so picker/library repros could not preserve the exact UX
path while also enabling wait/progress evidence.

## Patch

Updated `LauncherActivity.getDebugLaunchOverrides()` to forward:

```text
arm64_speed_profile_interval_ms
arm64_speed_profile_top_functions
arm64_speed_profile_min_delta
arm64_speed_profile_thread_snapshot
arm64_speed_profile_thread_snapshot_on_idle
xboxkrnl_thread_wait_trace
xboxkrnl_thread_wait_trace_budget
xboxkrnl_thread_wait_trace_after_ms
xboxkrnl_thread_wait_trace_guest_tids
```

Updated `XeniaAndroidSettings` stable launch overrides to keep the new
debug-only booleans default-off during normal app use.

Updated `tools/thor/thor_xenia_debug.ps1 -Mode LaunchLauncher` to pass the same
debug extras to the launcher. This lets the next repro use the real game
library row while preserving the extra cvars into `EmulatorActivity`.

Updated `tools/thor/thor_project_sylpheed_loading_loop_audit.ps1` to parse A64
thread snapshot rows:

```text
A64 thread snapshot tid=... state=... last_fn=... last_ret=...
```

Old packets still report:

```text
guest_progress_evidence=missing_a64_thread_snapshot
a64_thread_snapshot_count=0
```

## Validation

```text
PowerShell parser: passed for thor_xenia_debug.ps1
PowerShell parser: passed for thor_project_sylpheed_loading_loop_audit.ps1
Old packet smoke: passed against scratch/thor-debug/remote-debug-20260528-134556
ApkShell: passed
git diff --check: CRLF warnings only
```

## Next

Deploy this APK, launch the real game library with:

```text
-Arm64SpeedProfileIntervalMs 5000
-Arm64SpeedProfileThreadSnapshot true
-XboxkrnlThreadWaitTrace true
-XboxkrnlThreadWaitTraceBudget 256
```

Then reproduce the Project Sylpheed New Game / loading path and run the loading
loop audit on the new packet. The expected useful evidence is wait rows plus A64
thread snapshot rows showing whether the top XThreads are stuck in one guest
function, advancing through a small loop, or making broader progress while
presenting near-black frames.
