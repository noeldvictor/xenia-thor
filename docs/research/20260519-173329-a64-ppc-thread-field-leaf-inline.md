# A64 PPC Thread-Field Leaf Inline

Date: 2026-05-19 17:33 EDT
Target: AYN Thor Max, Blue Dragon Disc 1
Branch: `master`

## Hypothesis

If the A64 backend inlines tiny PPC leaf helpers matching
`lwz r11,D(r13); lwz r3,D(r11); blr`, then Blue Dragon's draw-wait loop should
spend fewer entries/direct calls around `8248B040` because that function is
currently a full guest call just to read a current-thread field.

## Primary Variable

New cvar:

- `a64_inline_ppc_thread_field_leaf_helpers`

The Thor script exposes it as:

- `-A64InlinePpcThreadFieldLeafHelpers true|false`

## Baseline

Baseline capture:

- `scratch/thor-debug/20260519-171625-*`
- APK SHA-256:
  `E188217B67097FF866006B7115FCB47B3B13AD044550082A6D75DEDDA794BCDB`

Late baseline profile:

- `entry_delta=183881407`
- `direct=183325520/1385684476`
- top 01: `8248B040`, delta `89,485,052`
- top 02: `8246B408`, delta `89,410,658`

Guest disassembly from prior focused dump:

```text
8248B040 816D0100   lwz        r11, 0x100(r13)
8248B044 806B014C   lwz        r3, 0x14C(r11)
8248B048 4E800020   bclr       20, 0
```

The caller `8246B408` calls this helper inside the draw-wait path at
`8246B490`.

## Change Plan

- Match the leaf helper by PPC instruction pattern rather than by Blue Dragon
  address.
- Inline only non-tail calls.
- Preserve the volatile `r11` side effect and the return value in `r3`.
- Keep the rollback cvar available through Android launch extras and the Thor
  debug script.

## Planned Validation

Build:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 155 -PerfSampleSeconds "75,140" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -A64InlinePpcThreadFieldLeafHelpers true
```

Expected signal:

- `8248B040` should drop or disappear from the top A64 speed profile.
- `direct` call deltas should drop materially in intervals dominated by the
  draw-wait loop.
- `8246B408` may remain hot because it is the real polling loop.
- The route must still reach the visible opening scene with no fatal signal,
  `AndroidRuntime`, `VK_ERROR_DEVICE_LOST`, GPU hung, tombstone, or ANR.

## Failure Criteria

- Crash or black-screen regression versus baseline route.
- `8248B040` stays equally hot, meaning the pattern did not trigger.
- Any guest state mismatch visible as watchdog, hang, or route regression.

## Status

Succeeded on the first Thor validation run.

## Validation Result

Build and deploy:

- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`

Capture command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 155 -PerfSampleSeconds "75,140" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -A64InlinePpcThreadFieldLeafHelpers true
```

Capture:

- `scratch/thor-debug/20260519-173541-*`
- APK SHA-256:
  `81499688A9C48C82934F0D8E17A5A4CA49B7D4C8D7009C1E5B7E9CE3EA789918`
- Metadata confirmed:
  `a64_inline_ppc_thread_field_leaf_helpers=true`
- Screenshot:
  `scratch/thor-debug/20260519-173541-screenshot.png`

Result:

- Reached the same visible Blue Dragon opening-scene route.
- No `AndroidRuntime`, fatal signal, `VK_ERROR_DEVICE_LOST`, GPU hung,
  tombstone, or ANR line was found in the capture.
- One ADB logcat pull failed near the end and recovered with reconnect; the
  process stayed alive in `EmulatorActivity`.

Final profile interval:

- `funcs=6126`
- `entry_delta=317645761`
- `h2g=3670/36802`
- `g2h=360979/4494956`
- `direct=317380254/1804216996`
- `indirect=273952/9804910`
- `extern=349526/4385228`
- `resolves=107/6127`
- `resolve_misses=0/0`

Hottest final functions:

- `8246B408`, delta `315,647,367`
- `82154938`, delta `75,940`
- `8214FA18`, delta `65,566`
- `82274DB0`, delta `59,877`
- `82287788`, delta `58,779`

## Interpretation

The inline triggered correctly: `8248B040` disappeared from the top profile,
and the remaining hot pair collapsed into the actual draw-wait function
`8246B408`. The loop now spins much faster than baseline, so total entry and
direct-call deltas rose rather than fell in the wait-heavy final interval. That
is expected when a tiny callee is removed from a busy loop; it exposes the real
polling loop instead of splitting cost across the loop and its helper.

This is a useful speed cleanup but not the final FPS wall. The next bottleneck
is the semantics/per-codegen cost of `8246B408` itself: a draw-wait poll of
token memory, current thread kernel time, and timeout logic. The next pass
should decide whether to:

- lower the A64 compare/branch/context-barrier cost in this loop generally;
- add a focused draw-wait wait/yield experiment;
- inspect the caller around `8246B408` to see why it is called hundreds of
  millions of times per interval;
- or move to the next general helper families once the wait loop is no longer
  the active scene wall.
