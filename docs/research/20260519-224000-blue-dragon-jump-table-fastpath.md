# Blue Dragon Jump-Table Helper Fastpath

Date: 2026-05-19 22:40 EDT

Branch: `master`

Base commit: `cacdd71c9`

Device: AYN Thor Max, serial `c3ca0370`

Target:
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

Can the hot Blue Dragon `827294CC` jump-table tail-dispatch helper be replaced
with a tiny exact A64 body without regressing the loading-spinner route?

## PPC Shape

Focused PPC/HIR evidence shows `827294CC` is a compact branch-table helper:

```text
827294CC addi   r25, r0, 0xB
827294D0 addi   r26, r0, 0x5
827294D4 addis  r12, r0, -0x7D8D
827294D8 addi   r12, r12, -0x6B14   ; r12 = 0x827294EC
827294DC rlwinm r0, r10, 2, 0, 29
827294E0 lwzx   r0, r12, r0
827294E4 mtspr  ctr, r0
827294E8 bcctr  20, 0
```

No CR fields are written. The relevant PPC-visible side effects are:

- `r25 = 0xB`
- `r26 = 0x5`
- `r12 = 0x827294EC`
- `r0 = be32[0x827294EC + ((r10 << 2) & 0xFFFFFFFC)]`
- `ctr = r0`
- tail branch to `ctr`

## Implementation

Added default-off `arm64_blue_dragon_jump_table_fastpath`, routed through:

- Android launch extra: `arm64_blue_dragon_jump_table_fastpath`
- Thor script: `-Arm64BlueDragonJumpTableFastpath true|false`

The body uses caller-scratch registers, performs the big-endian table load,
writes the PPC-visible GPR/CTR state, then follows the existing A64 tail-call
sequence through the indirection table or `ResolveFunction`.

## Validation

Final validation capture:
`scratch/thor-debug/20260519-223650-*`

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 95 -PerfSampleSeconds "70" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -Arm64BlueDragonMemcpyFastpath true -Arm64BlueDragonStricmpFastpath true -Arm64BlueDragonJumpTableFastpath true -MountCache true
```

Result:

- APK SHA-256:
  `A6631DCA1EF840305E6F1D00F376C4B28CC0E6E89CE12D3DBA10474C12DCFDDF`
- Screenshot at 95s reached the Blue Dragon loading spinner.
- No fatal signal, AndroidRuntime crash, Vulkan device-loss, GPU-hung, or ANR
  markers were found.
- `827294CC` generated code size dropped from 356 bytes in the previous
  stricmp-only capture to 308 bytes with the jump-table fastpath enabled.
- Final 15s interval entry delta rose from `19990709` in the stricmp-only
  capture `scratch/thor-debug/20260519-222409-*` to `25821887` in this capture.
  This suggests the route is chewing through more guest work, but it is not
  enough to reach the title screen yet.

## Current Wall

The final interval is still CPU/JIT dominated:

- `826C5620` stricmp helper: `3532434` entries, code size 512.
- `827294CC` jump-table helper: `2249324` entries, code size 308.
- `8272A3A4`: `1640911` entries, code size 14176.
- `826BF770`: still high as a byte-copy helper.
- GPU command thread is visible but not the dominant thread; the main generated
  guest-code thread remains the bottleneck.

## Decision

Keep `arm64_blue_dragon_jump_table_fastpath` default-off and usable in the
Blue Dragon speed bundle. It is stable on Thor and slightly smaller, but by
itself it does not solve the loading-spinner wall.

Next experiments:

- inspect `8272A3A4` and `8272A8E8` before adding more micro-fastpaths;
- add body-time counters or simpleperf-symbolized generated-code buckets so
  frequent tiny helpers do not mislead the OODA loop;
- consider syscall/HLE thunk pressure (`RtlEnterCriticalSection`,
  `RtlLeaveCriticalSection`, IRQL helpers) once the direct helper loops stop
  dominating.
