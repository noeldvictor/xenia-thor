# Blue Dragon Draw-Wait Caller Sampler

Date: 2026-05-19 21:16 EDT

Branch: `master`

Head during captures: `54e6ecf18`

APK SHA-256: `FA44925450FE3FDC398CED00056DB04C49B64858F492DC0FEE368658537B6283`

Device: AYN Thor Max, serial `c3ca0370`

Target:
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

The 2026-05-19 speed lane proved that `8246B408` remained the dominant CPU/A64
wall after GPR/LR, FPR, VMX, PPC thread field, and host-counter-time cleanup.
The next useful question was not only "what is `8246B408` doing?", but "who is
calling it in the hot route?"

The existing compiled-call trace was too invasive for this question because it
adds a native callback in compiled function prologs. In a hot polling loop, that
changes the route and can park the title before the useful scene.

## Patch

Added a default-off, title-specific caller sampler that only fires in the
compiled prolog for guest function `8246B408`.

New cvars:

- `arm64_blue_dragon_draw_wait_caller_profile`
- `arm64_blue_dragon_draw_wait_caller_profile_stride`
- `arm64_blue_dragon_draw_wait_caller_profile_budget`

New Thor script switches:

- `-Arm64BlueDragonDrawWaitCallerProfile true|false`
- `-Arm64BlueDragonDrawWaitCallerProfileStride N`
- `-Arm64BlueDragonDrawWaitCallerProfileBudget N`

The sampler increments a backend-context counter, samples every configured
stride, consumes a per-process line budget, reads guest LR from `PPCContext`,
resolves the guest function name when possible, and logs selected registers:
`r1`, `r3`, `r29`, `r30`, and `r31`.

## Capture 1: Caller Sampler

Capture:
`scratch/thor-debug/20260519-210937-*`

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 120 -LogLevel 1 -HideAndroidOsd true -HidNopConnected true -HidNopButtonSequence "start@45000:3000;a@68000:3000;a@86000:3000;a@106000:3000" -DisassembleFunctions false -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitCallerProfile true -Arm64BlueDragonDrawWaitCallerProfileStride 4096 -Arm64BlueDragonDrawWaitCallerProfileBudget 80 -MountCache true -GpuUnknownRegisterLogBudget 0 -XboxkrnlNtCreateFileFailLogBudget 0
```

Result:

- Screenshot reached the Blue Dragon loading spinner:
  `scratch/thor-debug/20260519-210937-screenshot.png`.
- All 80 budgeted samples had the same caller LR:
  `8246E6A4`, resolved as `sub_8246E618+8C`.
- Sampled register shape was stable:
  `thid 00000007`, `r1 701BF8E0`, `r3 701BF930`, `r29 00000003`,
  `r30 0000000F`, `r31 4000E880`.

Representative log shape:

```text
A64 Blue Dragon draw-wait caller sample thid 00000007 lr 8246E6A4 'sub_8246E618+8C' samples_for_lr=N r1 701BF8E0 r3 701BF930 r29 00000003 r30 0000000F r31 4000E880
```

## Capture 2: Focused PPC Dump

Capture:
`scratch/thor-debug/20260519-211237-*`

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 45 -LogLevel 2 -HideAndroidOsd true -HidNopConnected true -HidNopButtonSequence "start@45000:3000" -DisassembleFunctions true -DisassembleFunctionFilter "8246E618,8246B408,8248B040,8246DBB0" -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -MountCache true -GpuUnknownRegisterLogBudget 0 -XboxkrnlNtCreateFileFailLogBudget 0
```

Result:

- Screenshot reached the Blue Dragon title / press START state:
  `scratch/thor-debug/20260519-211237-screenshot.png`.
- The focused PPC dump confirmed `8246E618` as the caller loop around
  `8246B408`.

Key PPC flow:

```text
8246E620 stwu r1, -0x90(r1)
8246E624 mr r30, r4
8246E628 mr r31, r3
8246E62C mr r29, r5
...
8246E688 mr r5, r29
8246E68C mr r4, r31
8246E690 addi r3, r1, 0x50
8246E694 bl 0x8246B258
8246E698 b 0x8246E6AC

8246E69C addi r3, r1, 0x50
8246E6A0 bl 0x8246B408
8246E6A4 cmpi r3, 0
8246E6A8 beq 0x8246E6D0

8246E6AC lwz r10, 0x2A10(r31)
8246E6B0 lwz r11, 0x2A1C(r31)
8246E6B4 subf r9, r30, r11
8246E6B8 lwz r10, 0(r10)
8246E6BC subf r11, r10, r11
8246E6C0 subfc r11, r11, r9
8246E6C4 subfe r11, r11, r11
8246E6C8 rlwinm. r11, r11, 0, 31, 31
8246E6CC bne 0x8246E69C

8246E6D0 addi r3, r1, 0x50
8246E6D4 bl 0x8246B288
8246E6D8 addi r1, r1, 0x90
```

## Interpretation

`8246E618` is the hot draw/GPU wait caller:

- `r31` is the owning object/context.
- `r30` is a target/cursor token.
- `r29` is a small flag/count passed into wait-state setup.
- `8246B258` initializes a wait state at `r1 + 0x50`.
- `8246B408(wait_state)` polls/waits and returns nonzero while the wait should
  continue.
- The loop rechecks progress through `[r31 + 0x2A10]`, `[r31 + 0x2A1C]`, and
  `r30`.
- `8246B288` cleans up the wait state before return.

This means the remaining wall is not only the `8246B408` function body. The
caller loop at `8246E618` repeatedly calls the already-shortened wait body and
rechecks producer progress. That loop is now the better target for the next
title-specific CPU experiment.

## Decisions

- Keep the caller sampler default-off and use it only for narrow evidence
  captures.
- Do not use broad compiled-call tracing for this wait route unless the goal is
  explicitly trace-heavy correctness rather than speed.
- Keep the safe Blue Dragon route as host-counter fastpath, no native sleep, and
  `-MountCache true`.
- Treat `8246E618` as the next hot loop to inspect/optimize.

## Next Experiment

Preferred next static/runtime pass:

1. Dump and inspect `8246B258` and `8246B288`, because a full caller-loop
   replacement is only safe if setup and cleanup are understood.
2. If setup/cleanup are simple, consider a narrow `8246E618` generated-body
   fastpath that preserves the progress predicate and calls/yields with lower
   overhead.
3. If setup/cleanup are complex, add a less invasive loop-level counter/yield
   probe keyed to `8246E618` rather than inserting native work into every
   `8246B408` entry.
4. Avoid native sleep in the hot route for now; prior `100 us` sleep parked the
   route on black screen.

Rollback:

- Leave `arm64_blue_dragon_draw_wait_caller_profile=false`.
- Remove the sampler cvars and `MaybeEmitBlueDragonDrawWaitCallerProfile()` if
  the prolog hook ever affects speed with the cvar disabled.
