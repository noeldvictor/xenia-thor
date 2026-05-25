# Blue Dragon fpscr CFG Writeback Audit

## Summary

Implemented a default-off, counter-only `arm64_blue_dragon_fpscr_cfg_writeback_audit`
skeleton for the Blue Dragon `82287788` fpscr writeback plan. It only counts
static sites, dynamic `fpscr` context loads/stores, required call writeback PCs,
and payload materialization. It does not change normal entry behavior and does
not materialize an alternate payload.

## Build and Deploy

- Base head before commit: `95ab009a4`.
- `tools/thor/thor_build.ps1 -Mode NativeCore`: passed.
- `tools/thor/thor_build.ps1 -Mode FullDeploy`: passed and installed.
- APK: `android/android_studio_project/app/build/outputs/apk/github/debug/app-github-debug.apk`
- APK SHA256: `754EC8F2F04A71F45603EFFA79A8E6843CCBADE7A8B201DC295337E7E5FA390A`

## Capture

- Command: `tools/thor/thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64BlueDragonFpscrCfgWritebackAudit true -Arm64SpeedProfileBodyTimeFilter "82282490,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true`
- Log: `scratch/thor-debug/20260525-132931-speed-logcat-filtered.txt`
- Meta: `scratch/thor-debug/20260525-132931-meta.txt`
- Screenshot: `scratch/thor-debug/20260525-132931-screenshot.png`
- Result: route-clean visible opening sky/dragon-wing screenshot.
- Fatal marker search: clean for `FATAL`, `SIGSEGV`, `SIGABRT`, `Abort`,
  `DebugBreak`, `ASSERT`, and crash markers.

## Counter Evidence

At body-time activation (`13:31:32`):

```text
static_load_sites=26/26 static_store_sites=26/26
static_cfg_transition_sites=24/24 static_external_transition_sites=2/2
static_call_writeback_sites=4/4 loads=53886/53886 stores=53886/53886
cfg_transition_stores=52991/52991 external_transition_stores=895/895
required_call_writebacks=17861/17861 call_82287ED4=0/0
call_82287EDC=17377/17377 call_82287EE4=242/242
call_82288220=242/242 payload_materializations=0/0
unclassified_stores=0/0 normal_entry=unchanged
```

Final interval (`13:32:32`):

```text
loads=680095/2153538 stores=680095/2153538
cfg_transition_stores=668899/2116145 external_transition_stores=11196/37393
required_call_writebacks=221177/698088 call_82287ED4=0/0
call_82287EDC=215109/678946 call_82287EE4=3034/9571
call_82288220=3034/9571 payload_materializations=0/0
unclassified_stores=0/0 normal_entry=unchanged
```

Body-time remained CPU/JIT-heavy:

```text
82282490 body_ticks_total=33811022 entries_delta=54908 ticks_per_entry=202
82281D28 body_ticks_total=7646617 entries_delta=26196 ticks_per_entry=92
82287788 body_ticks_total=3872874 entries_delta=218143 ticks_per_entry=5
```

Final perf still shows the CPU wall:

```text
Main Thread 92.3%, GPU Commands 11.5%, Draw Thread 3.8%
```

## Interpretation

The counter skeleton is route-clean and confirms the offline CFG writeback plan
is dynamically relevant: the route hit all 26 static load/store sites, all 24
CFG-transition stores, both external-transition stores, and three of the four
required call writeback PCs. `82287ED4` remains required statically but was
route-dormant in this capture. There were no unclassified fpscr stores and no
payload materializations.

This is correctness/provenance evidence, not a speed win. The next behavior
patch, if attempted, should stay default-off and preserve required writebacks at
`82287ED4`, `82287EDC`, `82287EE4`, and `82288220`, even though `82287ED4` did
not fire on this route. A safer next slice is either a source/codegen design pass
for the real fpscr CFG carrier payload or returning to the caller-local
`82282490:82282598 -> 82287788` edge-variant storage lane.
