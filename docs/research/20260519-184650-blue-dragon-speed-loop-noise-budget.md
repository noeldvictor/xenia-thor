# Blue Dragon Speed Loop Noise Budget

Date: 2026-05-19 18:46 EDT
Branch: `master`
Head at capture time: `bfe69d2e3` plus local changes
Device: AYN Thor Max, ADB serial `c3ca0370`
Target: `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Goal

Keep pushing Blue Dragon toward a faster title/opening route on Thor while
making the OODA loop less noisy. The immediate hypothesis was:

If the quiet speed lane suppresses repeated GPU unknown-register warnings and
`NtCreateFile failed` warnings while preserving the A64 speed profiler, then
captures should still reach the same route milestone with cleaner logs and less
host logging overhead.

## Baseline Signals

Useful earlier captures:

- `scratch/thor-debug/20260519-173541-*`: reached visible opening scene with
  `a64_inline_ppc_thread_field_leaf_helpers=true`; final top PC was
  `8246B408`, delta `315,647,367`.
- `scratch/thor-debug/20260519-180205-*`: unthrottled
  `arm64_blue_dragon_draw_wait_probe=true` reached opening scene and changed
  the late profile shape; final `8246B408` delta `43,505,070`.
- `scratch/thor-debug/20260519-180854-*`: draw-wait probe stride `4096`
  reached opening scene but lost the useful wait-loop behavior; final
  `8246B408` delta `303,960,450`.

## Changes Tested

Code and tooling changes:

- Kept `arm64_blue_dragon_draw_wait_probe` out of the speed-default reset list
  so a caller can request the known-fast route explicitly.
- Added `arm64_blue_dragon_draw_wait_probe_stride`, default `1`, so every-hit
  behavior remains the default and larger power-of-two strides can be tested.
- Added `arm64_blue_dragon_draw_wait_inline_tick_step`, default `0`, as a
  rollback-safe wait-loop experiment. Step `1` was not a speed win in initial
  110s runs.
- Added `gpu_unknown_register_log_budget`, default `128`, and set it to `0` in
  Blue Dragon speed defaults.
- Added `xboxkrnl_nt_create_file_fail_log_budget`, default `512`, and set it to
  `0` in Blue Dragon speed defaults.
- Kept `MountCache` explicit: speed defaults no longer force it off if the
  caller asks for `-MountCache true`.

Canonical command after these changes:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 155 -PerfSampleSeconds "75,140" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true
```

## Results

Latest validation capture:

- Capture: `scratch/thor-debug/20260519-184120-*`
- APK SHA-256:
  `C50C9C40EF73AF84F8675388EBD7A0BD491D0DB1F1ED6C03B2ACF191832001E9`
- Route label: `opening-scene`
- Screenshot: `scratch/thor-debug/20260519-184120-screenshot.png`
- Filtered log line count: `648`
- `NtCreateFile failed` lines: `0`
- `unknown register` lines: `0`
- No `AndroidRuntime`, fatal signal, `VK_ERROR_DEVICE_LOST`, GPU hung,
  tombstone, or ANR marker was found.

Final profile interval:

```text
funcs=6136 entry_delta=291310454
h2g=3661/36746 g2h=435891/4857227 direct=290962320/1681928431
indirect=359649/9768935 extern=353892/4350451
```

Final top functions:

```text
8246B408 delta=289004366
82154938 delta=75940
82274DB0 delta=65860
8214FA18 delta=65566
82287788 delta=64687
827294CC delta=63021
826AF690 delta=56952
826BF770 delta=55011
8272A3A4 delta=46281
```

## Interpretation

The log-budget work is a measurement and OODA win, not a visible FPS
breakthrough. The route remains stable and cleaner, but the CPU wall remains
guest function `8246B408`, with direct guest call count tracking most of the
work in late intervals.

The next high-value work is not more broad logging. It is either:

- static/runtime proof of what `8246B408` waits on, using focused PPC dumps or
  Ghidra; or
- broad A64 dispatch/direct-link/call-return cost reduction so the backend does
  less host work per guest edge.

Keep `arm64_blue_dragon_draw_wait_inline_tick_step` off unless testing that one
variable. Keep `MountCache true` as a separate A/B lane; the first 110s
mount-cache run did not improve the current route and caused early noise before
the log budgets existed.
