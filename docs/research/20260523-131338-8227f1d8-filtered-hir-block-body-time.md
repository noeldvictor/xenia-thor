# 8227F1D8 Filtered HIR And Block Body-Time

Date: 2026-05-23 13:13 EDT

## Goal

Dump `8227F1D8` OptHIR and measure delayed block body-time after the prior
`8227FEE8` callee split showed `8227F1D8` as the dominant child target.

## Capture

- Capture prefix: `scratch/thor-debug/20260523-130934-*`
- Logcat: `scratch/thor-debug/20260523-130934-speed-logcat.txt`
- Screenshot: `scratch/thor-debug/20260523-130934-screenshot.png`
- HEAD: `a1e62d275`
- APK SHA-256:
  `962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5`
- Route: visible opening sky/wing scene
- Fatal-marker search: clean

Launch lane:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -DisassembleFunctions true -DisassembleFunctionFilter 8227F1D8 -Arm64SpeedProfileBodyTimeFilter "8227FEE8,8227F1D8" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileBlockFilter "8227F1D8" -Arm64SpeedProfileBlockBodyTime true -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

## Results

The filtered warning-level `8227F1D8` OptHIR dump landed.

Final delayed body-time rows:

```text
8227FEE8 body_ticks_delta=1090229 body_ticks_total=4092954 entries_delta=1908 ticks_per_entry=571 code_size=49804
8227F1D8 body_ticks_delta=907049 body_ticks_total=4238549 entries_delta=9675 ticks_per_entry=93 code_size=28180
8227EFC8 body_ticks_delta=12672 body_ticks_total=31182 entries_delta=2171 ticks_per_entry=5 code_size=25752
```

Final `8227F1D8` block body-time:

```text
block=0 guest=8227F1D8 body_ticks_delta=877521 body_ticks_total=4152240 entries_delta=9675 ticks_per_entry=90
block=2 guest=8227F288 body_ticks_delta=7061 body_ticks_total=20441 entries_delta=9675 ticks_per_entry=0
```

The block mix report says block `8227F1D8` is small but call-heavy:

```text
block=0 guest=8227F1D8 instr=69 ctx=7/20 mem=4/3 branches=2 calls=2 barriers=4
ppc=addi:7,addis:3,or:3,bl:2,lwzx:2,stwx:2
call_targets=0x82490030:1,0x826BFC7C:1
```

The hotpath report says the whole function is state-traffic heavy:

```text
hir_lines=2326
store_context=546
load_context=289
context_barrier=115
calls=52
branches=75
permutes=24
byte_swaps=8
```

Top context slots:

```text
loads:  r[1]=43, r[31]=36, r[30]=20, r[11]=17, r[23]=16
stores: r[11]=60, lr=50, r[3]=48, r[4]=46, cr6.*=37 each
```

Final perf sample still points at CPU/JIT:

```text
Main Thread 103%
XMA Decoder 7.6%
GPU Commands 7.6%
Draw Thread 0.0%
```

## Decision

Do not start a vector, GPR-cache, or CR-store peephole from this capture alone.
The measured block wall is inclusive call work in block `8227F1D8`, and block
`8227F1D8` has two static call targets:

- `0x82490030`
- `0x826BFC7C`

Next useful slice:

1. Run a route-stabilized call-edge split for `8227F1D8`.
2. Include delayed body-time for `8227F1D8,82490030,826BFC7C`.
3. Keep broad experiments default-off.
4. If the run is route-clean, choose the next target from dynamic edge rows.

Suggested capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "8227F1D8,82490030,826BFC7C" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileCallEdgeFilter "8227F1D8" -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```
