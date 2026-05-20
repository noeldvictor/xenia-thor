# Thor Simpleperf Speed Lane

Date: 2026-05-19 21:57 EDT

Branch: `master`

Base commit: `01777deaa`

Device: AYN Thor Max, serial `c3ca0370`

Target:
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

The A64 top-function profiler reports guest function entry counts, not elapsed
time. After adding title-specific helper body fastpaths, entry counts alone are
no longer enough to decide whether the next bottleneck is generated guest code,
native codegen, GPU work, XMA/audio, or HLE.

## Patch

`tools/thor/thor_xenia_debug.ps1` now has an opt-in simpleperf lane for
`LaunchBlueDragonSpeedCapture`:

```powershell
-Simpleperf true
-SimpleperfStartSecond 105
-SimpleperfSeconds 12
-SimpleperfFrequency 1000
-SimpleperfEvent cpu-cycles:u
-SimpleperfCallGraph fp
-SimpleperfPercentLimit 0.5
```

The lane:

- records on device with `simpleperf record --app jp.xenia.emulator.github.debug`;
- pulls the resulting `perf.data` into `scratch/thor-debug`;
- resolves the host NDK `simpleperf.exe`;
- writes a host-side report with `--symdir` pointed at the unstripped
  `libxenia-app.so`.

## Validation

Smoke capture:
`scratch/thor-debug/20260519-215248-*`

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 20 -PerfSampleSeconds "10" -Simpleperf true -SimpleperfStartSecond 5 -SimpleperfSeconds 2 -SimpleperfFrequency 500 -Arm64SpeedProfileIntervalMs 0 -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -Arm64BlueDragonMemcpyFastpath true -MountCache true
```

Result:

- `simpleperf` recorded 1497 samples with 0 samples lost.
- The report was symbolized with native names from the unstripped Android
  build.
- The first attempted script version tripped on `adb pull` progress text being
  surfaced as a PowerShell native-command error; the script now pulls via a
  small `cmd /c` wrapper and logs the output instead of failing.

## Long Capture

Capture:
`scratch/thor-debug/20260519-215347-*`

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 130 -PerfSampleSeconds "70,115" -Simpleperf true -SimpleperfStartSecond 105 -SimpleperfSeconds 12 -SimpleperfFrequency 1000 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -Arm64BlueDragonMemcpyFastpath true -MountCache true
```

Result:

- Screenshot at 130s was still the Blue Dragon loading spinner.
- `simpleperf` recorded 19676 samples with 0 samples lost.
- The 105s-117s native sample reported:
  - Main Thread / generated guest code around 34% self in unknown executable
    memory.
  - Remaining runtime compilation / function resolution around 8%.
  - GPU command work around 4%.
  - XMA/audio around 3%.
- The overlapping A64 entry intervals pointed at guest hot spots:
  `827294CC`, `8272A3A4`, `8272A8E8`, `826C5620`, and `826BF770`.

## Decision

Keep simpleperf as the standard low-noise CPU truth lane when entry-count
profiles become ambiguous.

The next direct speed experiment should be a default-off fastpath for
`826C5620`, which the focused PPC dump already classified as an ASCII
case-insensitive string compare and which repeatedly becomes a top guest entry
hotspot during the loading route.

## Caveats

- `--call-graph fp` still has failed callchains when samples land in generated
  guest code. That is expected until generated-code unwind/symbol metadata
  improves.
- `simpleperf` proves CPU distribution, not correctness. Every speed patch still
  needs a same-route screenshot/log and crash-marker check.
