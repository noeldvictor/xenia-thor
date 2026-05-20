# Blue Dragon Exact-State Stricmp Fastpath

Date: 2026-05-19 22:30 EDT

Branch: `master`

Base commit: `84c32b8ae`

Device: AYN Thor Max, serial `c3ca0370`

Target:
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

The first whole-function `826C5620` string-compare fastpath regressed Blue
Dragon to a black idle/stall. Was the missing piece PPC-visible state, rather
than the string loop itself?

## Return-State Probe

The new default-off `arm64_blue_dragon_stricmp_return_profile` samples the
normal generated `826C5620` return state. It logs:

- guest LR and resolved caller name;
- `r3`, `r4`, `r5`, `r6`, and `r9`;
- packed CR plus raw `cr0`, `cr5`, `cr6`, and `cr7`;
- `xer_so`.

Validation capture:
`scratch/thor-debug/20260519-221800-*`

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 95 -PerfSampleSeconds "70" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -Arm64BlueDragonMemcpyFastpath true -Arm64BlueDragonStricmpReturnProfile true -Arm64BlueDragonStricmpReturnProfileStride 4096 -Arm64BlueDragonStricmpReturnProfileBudget 32 -MountCache true
```

Result:

- Screenshot at 95s reached the Blue Dragon loading spinner.
- 32 return-state sample lines were captured.
- Caller distribution:
  - `8215CA10` / `sub_8215C198+878`: 20 samples.
  - `8215BF3C` / `sub_8215BE68+D4`: 8 samples.
  - `8215BAA0` / `sub_8215BA78+28`: 2 samples.
  - `8215C04C` / `sub_8215BF78+D4`: 1 sample.
  - `8215C15C` / `sub_8215C088+D4`: 1 sample.
- CR combinations varied with compare result and terminator state. `cr0`,
  `cr5`, `cr6`, and `cr7` were all observably live; the focused PPC dump also
  shows `cr1` is written by `cmpi crf1, r5, 0x5A`.

## Exact-State Fastpath

`arm64_blue_dragon_stricmp_fastpath` was reintroduced as an exact-state
attempt, not the previous unsafe loop. The A64 body now mirrors the observed PPC
helper:

```text
826C5620 addi   r9, r3, -1
826C5624 addi   r4, r4, -1
826C5628 lbzu   r6, 1(r4)
826C562C lbzu   r5, 1(r9)
826C5630 cmpi   crf7, r6, 0
826C5634 subf.  r3, r6, r5
826C5638 beq    crf7, 826C5670
826C563C beq    crf0, 826C5628
826C5640 cmpi   crf5, r6, 0x41
826C5644 cmpi   crf6, r6, 0x5A
826C5648 blt    crf5, 826C5654
826C564C bgt    crf6, 826C5654
826C5650 ori    r6, r6, 0x20
826C5654 cmpi   crf0, r5, 0x41
826C5658 cmpi   crf1, r5, 0x5A
826C565C blt    crf0, 826C5668
826C5660 bgt    crf1, 826C5668
826C5664 ori    r5, r5, 0x20
826C5668 subf.  r3, r6, r5
826C5670 bclr
```

The fastpath writes the same low-level state the HIR path exposes:

- `r3`, `r4`, `r5`, `r6`, and `r9`;
- CR field bytes 0, 1, and 2 for `cr0`, `cr1`, `cr5`, `cr6`, and `cr7`;
- existing CR SO bytes are left untouched, matching `PPCHIRBuilder::UpdateCR`.

## Fastpath Validation

Validation capture:
`scratch/thor-debug/20260519-222409-*`

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 95 -PerfSampleSeconds "70" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -Arm64BlueDragonMemcpyFastpath true -Arm64BlueDragonStricmpFastpath true -MountCache true
```

Result:

- APK SHA-256:
  `91749745FFC71A8750A4B2359FE5CC79F9AD161F7466E6E6C14C4AE56886948B`
- Screenshot at 95s reached the Blue Dragon loading spinner.
- No fatal signal, AndroidRuntime crash, Vulkan device-loss, GPU-hung, or ANR
  markers were found.
- `826C5620` generated code size dropped from the normal generated 784-byte
  body in the return-profile capture to a 512-byte exact-state body.
- `826C5620` still appears high in entry-count profiles. This does not prove the
  body remains expensive because the profiler counts function entries, not
  elapsed time.

## Decision

Keep the exact-state `826C5620` fastpath default-off and usable for speed A/B
runs. Do not make it default until a longer same-route A/B or simpleperf run
shows a real time-to-scene or CPU-cost win.

Next speed targets:

- use simpleperf or a lower-overhead body-time counter when entry count is
  ambiguous;
- inspect `827294CC`, `8272A3A4`, and `8272A8E8`, which remain hot in the
  spinner route;
- avoid whole-function helper replacement unless CR/register side effects are
  mapped first.
