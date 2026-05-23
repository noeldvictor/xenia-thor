# A64 Idle Owner Attribution And Patched Route Control

Date: 2026-05-22 23:29 EDT

Branch: `master`

Starting HEAD: `f6d08e6e5`

## Goal

Turn the previous Blue Dragon flatline into actionable thread evidence. The
last patched no-filter control (`scratch/thor-debug/20260522-230518-*`) black-
idled with `entry_delta=0` and could not emit an idle thread snapshot because
the processor/global debug lock was busy. It only reported
`last_global_owner_sys_tid=21741`, which was not enough to map the holder back
to an XThread.

## Patch

- `ThreadDebugInfo` now records the host/native system thread ID.
- `Processor` now keeps a small lock-free native-TID hint cache for guest
  thread ID, handle, and state.
- When the A64 idle thread snapshot cannot acquire the processor debug lock,
  it logs `owner_hint=hit|miss`, `owner_hint_tid`,
  `owner_hint_handle`, and `owner_hint_state` beside
  `last_global_owner_sys_tid`.
- Normal successful A64 thread snapshots now include `native=...` so Android
  top/thread rows can be joined back to guest threads.

This is diagnostic only. It does not change generated guest code, scheduling,
JIT lowering, or game behavior.

## Validation

```powershell
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "82282490,8227FEE8" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

`NativeCore` passed. `FullDeploy` passed and installed the APK. `git diff
--check` passed before the device run.

## Capture

- Capture: `scratch/thor-debug/20260522-232133-*`
- APK SHA-256:
  `E92DAC2CB4E7080C196DB9656305F372DC20C189E7697A2FCCD47D1E12DA3FA3`
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`
- Cvars of note:
  `arm64_context_promotion_gpr_livein_r1=false`,
  `arm64_speed_profile_body_time_filter=82282490,8227FEE8`,
  `arm64_speed_profile_body_time_after_ms=120000`,
  `arm64_speed_profile_thread_snapshot_on_idle=true`,
  no disassembly filter.

## Findings

- The patched no-filter route reached the visible opening sky/wing scene.
- Fatal-marker search found no `Fatal signal`, `AndroidRuntime`, `SIGSEGV`,
  `VK_ERROR_DEVICE_LOST`, `GPU is hung`, or `ANR` markers.
- The idle owner-hint line did not fire because counters stayed active through
  the final sample; that is good route-safety evidence, but not proof of the
  busy-lock attribution path yet.
- Body-time activated at `120001ms`.
- Final body-time interval:
  - `82282490`: `body_ticks_delta=7557487`,
    `body_ticks_total=26866214`, `entries_delta=62328`,
    `ticks_per_entry=121`, `code_size=87168`.
  - `82281D28`: `body_ticks_delta=2687447`,
    `body_ticks_total=7976824`, `entries_delta=29736`,
    `ticks_per_entry=90`, `code_size=96908`.
  - `8227FEE8`: `body_ticks_delta=609294`,
    `body_ticks_total=3609184`, `entries_delta=2066`,
    `ticks_per_entry=294`, `code_size=49804`.
- Final process sample still shows CPU pressure, not a GPU-only wall:
  Main Thread about `100%`, XMA Decoder about `53.8%`, GPU Commands about
  `11.5%`, Draw Thread about `3.8%`.

## Decision

Keep the diagnostic patch. It restores patched no-filter route confidence and
makes the next black-idle more useful by mapping the busy debug-lock owner when
the idle snapshot cannot acquire the processor lock.

Next action: run a control-sandwiched filtered `8227FEE8` capture with the same
delayed body-time route stabilizer. If it reaches opening, use the warning-level
HIR dump for a focused codegen audit. If it black-idles again, read the new
`owner_hint` fields before changing generated-code behavior.
