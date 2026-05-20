# Blue Dragon Hot Helper Fastpaths

Date: 2026-05-19 21:43 EDT

Branch: `master`

Base commit: `2e30ffa10`

Device: AYN Thor Max, serial `c3ca0370`

Target:
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

After inlining the hot `8246B408` draw-wait body at caller `8246E618`, the
speed profile exposed a new hot set:

- `827294CC`
- `8272A3A4`
- `8272A8E8`
- `826C5620`
- `826BF770`
- `RtlEnterCriticalSection`, `RtlLeaveCriticalSection`,
  `KeRaiseIrqlToDpcLevel`, and `KfLowerIrql`

The question was whether these are still A64/JIT codegen problems or whether
the run has moved into library/HLE/GPU synchronization and loading helpers.

## Classification Capture

Capture:
`scratch/thor-debug/20260519-213553-*`

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 95 -LogLevel 2 -HideAndroidOsd true -HidNopConnected true -HidNopButtonSequence "start@45000:3000;a@68000:3000" -DisassembleFunctions true -DisassembleFunctionFilter "827294CC,8272A3A4,8272A8E8,826C5620,826BF770,8273EF84,8273EF74,8273F7B4,8273F7C4" -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -MountCache true -GpuUnknownRegisterLogBudget 0 -XboxkrnlNtCreateFileFailLogBudget 0
```

Findings:

- `826BF770` is byte-copy / `memcpy` shaped. It saves original `r3`, copies from
  `r4` to `r3` for `r5` bytes with alignment paths, then returns original `r3`.
- `826C5620` is case-insensitive string-compare shaped. It lowercases ASCII
  `A-Z` by ORing `0x20` before comparing.
- `8273EF84`, `8273EF74`, `8273F7B4`, and `8273F7C4` are syscall/import thunks:
  each is a single `sc 2` into Xenia's extern/HLE path.
- `827294CC` is a compact jump-table dispatch helper.
- `8272A3A4` and `8272A8E8` are large bit/byte packing or decompression-style
  loops; they touch byte streams and branch heavily.

This hot set looks like loading/decompression/string/kernel work, with GPU
Commands also hot in the speed run. It is no longer just the old draw-wait
callee wall.

## Patch

Added a default-off, title-specific cvar:

- `arm64_blue_dragon_memcpy_fastpath`

New Thor script switch:

- `-Arm64BlueDragonMemcpyFastpath true|false`

Behavior:

- Only fires for guest function `826BF770`.
- Reads guest `r3` destination, `r4` source, and `r5` byte count.
- Translates guest addresses through the A64 membase path.
- Calls host `std::memmove`.
- Restores original `r3` as the return value.

Caveat:

- This does not preserve volatile-register side effects from the original PPC
  helper. It is a default-off ABI-shaped experiment, not a global memcpy import.

## Validation Capture

Capture:
`scratch/thor-debug/20260519-214026-*`

APK SHA-256:
`340CB68339C9AA6E74BE6030B3446ACDA3BDF9FA59DB102F31F952DCCCEC73A3`

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 130 -PerfSampleSeconds "70,115" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -Arm64BlueDragonMemcpyFastpath true -MountCache true
```

Result:

- The run built, deployed, launched, and reached the loading spinner at 130s.
- No fatal signal, AndroidRuntime crash, `VK_ERROR_DEVICE_LOST`, GPU-hung, or
  ANR markers appeared in the checked filtered log.
- The speed profile still lists `826BF770` because the current top-function
  profiler counts entries, not elapsed time. After a function-body fastpath, a
  high entry count means "called often" rather than "still slow."
- The perf sample still showed Main Thread and GPU Commands both around a full
  core during loading, so the next comparison needs a better CPU-vs-GPU split
  than entry counts alone.

## Decision

Keep `arm64_blue_dragon_memcpy_fastpath` default-off.

Use it as an experiment in same-route A/B speed captures, but do not treat the
entry-count profile as proof of failure. A body replacement can reduce cost
while leaving the function's entry count unchanged.

## Next Actions

- Add a timing/cost lane for hot guest functions or use Android simpleperf to
  separate entry count from actual CPU time.
- Consider a default-off `826C5620` ASCII case-insensitive string compare
  fastpath.
- Classify `8272A3A4` / `8272A8E8` as decompression/bitstream work before
  patching them.
- Keep an eye on GPU Commands during loading; the route is now mixed CPU/GPU,
  not purely CPU.
