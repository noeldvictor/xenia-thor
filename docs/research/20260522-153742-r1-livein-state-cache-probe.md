# 2026-05-22 15:37 EDT - r[1] Live-In State Cache Probe

## Goal

Implement and test a default-off, function-filtered pre-register-allocation
`r[1]` live-in/cache probe for Blue Dragon hot function `82282490`.

This followed the offline live-in report:

- strict helper plus barrier flushing: `14` replaceable first loads;
- helper flushing while preserving clean state across `context_barrier`: `56`
  replaceable first loads and `61` replaceable loads total.

## Patch

Added guarded cvars:

```text
arm64_context_promotion_gpr_livein_r1=false
arm64_context_promotion_gpr_livein_r1_function=0
arm64_context_promotion_gpr_livein_r1_preserve_barrier=true
arm64_context_promotion_gpr_livein_r1_audit=false
```

The probe runs inside `ContextPromotionPass` before the normal block-local
promotion pass. It targets exact PPC `r[1]` context loads/stores, uses a local
slot as the cross-block carrier, and kills availability at calls/helpers,
exits, exceptions, volatile context operations, and exact or aliasing writes.
The Thor launcher and Android activity now forward the cvars so captures can
turn the probe on without changing defaults.

## Validation

Build and deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/thor/thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both completed successfully.

Blue Dragon capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 130 -PerfSampleSeconds "75,115,125" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 true -Arm64ContextPromotionGprLiveInR1Function 0x82282490 -Arm64ContextPromotionGprLiveInR1PreserveBarrier true -Arm64ContextPromotionGprLiveInR1Audit true -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 100000 -StopAppAfterCapture true
```

Artifacts:

- logcat: `scratch/thor-debug/20260522-152727-speed-logcat.txt`;
- filtered logcat: `scratch/thor-debug/20260522-152727-speed-logcat-filtered.txt`;
- metadata: `scratch/thor-debug/20260522-152727-meta.txt`;
- screenshot: `scratch/thor-debug/20260522-152727-screenshot.png`;
- perf snapshots: `scratch/thor-debug/20260522-152727-perf-75s.txt`,
  `scratch/thor-debug/20260522-152727-perf-115s.txt`,
  `scratch/thor-debug/20260522-152727-perf-125s.txt`, and
  `scratch/thor-debug/20260522-152727-perf-final.txt`.

Crash scan found no searched fatal markers:

```powershell
rg -n "Fatal signal|AndroidRuntime|FATAL EXCEPTION|signal 11|Guest crash|VK_ERROR_DEVICE_LOST|GPU is hung|CHECK failed|ASSERT" scratch\thor-debug\20260522-152727-speed-logcat.txt scratch\thor-debug\20260522-152727-speed-logcat-filtered.txt
```

## Results

The probe was route-clean but did not reach the offline opportunity size.

Audit:

```text
blocks=164 entry_clean_blocks=40 needs_entry_local_blocks=16
loads_attempted=107 loads_replaced=16 loads_seeded=91
local_loads=16 local_stores=14 branch_stores=12 final_stores=2
stores_seen=11 alias_stores=0 call_resets=91 barrier_resets=0
alias_resets=0 exit_resets=0 preserve_barrier=true
skipped dirty_entry=91 after_call=0 after_barrier=0 after_alias=0
```

Body-time samples for `82282490`:

```text
15:29:28 body_ticks_delta=24374665 entries_delta=4725 ticks_per_entry=5158 code_size=87224
15:29:43 body_ticks_delta=5501536 entries_delta=18337 ticks_per_entry=300 code_size=87224
```

Compared with the prior clean control code size around `87168`, this probe grew
`82282490` by about `56` bytes. It is much smaller than the earlier pinned
`r[1]` attempt, but it is not a speed proof.

The final screenshot was still on a loading spinner route, so this capture is
evidence of clean execution and instrumentation only. It is not title-screen or
full-speed proof.

## Interpretation

The live-in idea is still plausible, but this implementation is too
conservative or is not matching the offline CFG model. It replaced only `16`
loads, while the barrier-preserving offline report predicted `56` replaceable
first loads. The likely gap is in edge/fallthrough availability, block entry
local selection, or the local-carrier merge strategy.

The current skip counters are also not diagnostic enough: `call_resets=91`
matches the high kill pressure, but the skipped-by-reason line mostly collapses
to `dirty_entry=91`. The next patch should carry a historical dirty reason so
the audit explains why each load was not replaced.

## Decision

Keep the probe default-off:

```text
arm64_context_promotion_gpr_livein_r1=false
```

Do not use it as a Blue Dragon speed preset yet.

Next action: add an exact runtime eligibility/audit report that compares the
offline live-in report against the actual pass rewrite state, or improve the
pre-RA carrier so the route-clean probe reaches the expected `50+` replacement
range before another long Thor speed run.
