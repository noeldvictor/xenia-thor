# 8227FEE8 Block Body-Time Route-Clean Capture

Date: 2026-05-23 12:27 EDT

## Goal

Repeat delayed `8227FEE8` block body-time on the patched APK from
`docs/research/20260523-001018-a64-owner-thread-id-attribution.md`. The
previous attempt black-idled before target rows, so this run needed either
route-clean internal block attribution or a black-idle owner hint.

## Capture

Capture: `scratch/thor-debug/20260523-122223-*`

- HEAD: `1f6d75624`
- APK SHA256:
  `962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5`
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`
- Screenshot:
  `scratch/thor-debug/20260523-122223-screenshot.png`
- Log:
  `scratch/thor-debug/20260523-122223-speed-logcat.txt`
- Metadata:
  `scratch/thor-debug/20260523-122223-meta.txt`

Flags:

```text
arm64_speed_profile_body_time_filter=8227FEE8
arm64_speed_profile_body_time_after_ms=120000
arm64_speed_profile_block_filter=8227FEE8
arm64_speed_profile_block_body_time=true
arm64_speed_profile_thread_snapshot_on_idle=true
arm64_context_promotion_gpr_livein_r1=false
arm64_speed_profile_call_edge_filter=
```

Result:

- The route reached the visible opening sky/wing scene.
- Fatal-marker search was clean for `Fatal signal`, `AndroidRuntime`,
  `VK_ERROR_DEVICE_LOST`, `GPU is hung`, `Unhandled`, `ASSERT`, and
  `CHECK failed`.
- No idle owner line fired because counters kept advancing.
- `8227FEE8` body-time rows appeared after delayed activation.
- Final perf sample still showed a CPU wall: Main Thread `96.1%`, GPU
  Commands `11.5%`, XMA Decoder `11.5%`, Draw Thread `3.8%`.
- Android `gfxinfo` only reported the Java/UI surface frames, not emulator FPS:
  `Total frames rendered: 3`. Do not use it as game FPS proof.

## 8227FEE8 Body-Time

Final function row:

```text
8227FEE8 body_ticks_delta=1871176 body_ticks_total=6914659 entries_delta=2041 ticks_per_entry=916 code_size=71868
```

The function remains a real secondary target, but it is not the top-level wall:
previous route-clean captures keep `82282490` dominant. This capture answers
the internal `8227FEE8` block question.

## Dynamic Block Body-Time

`tools/thor/thor_hir_block_mix_report.ps1` joined the route-clean block rows
with the warning-level `8227FEE8` OptHIR dump from
`scratch/thor-debug/20260522-233545-speed-logcat.txt`.

Top dynamic body-time blocks:

```text
block=48  guest=822809F4 body_total=1937201 body_peak_delta=665665 body_peak_tpe=128 calls=2 call_targets=0x8227F1D8,0x8247BE20
block=40  guest=822808F4 body_total=219635  body_peak_delta=65284  body_peak_tpe=9   calls=1 indirect bcctrl shape
block=26  guest=82280598 body_total=212460  body_peak_delta=79026  body_peak_tpe=0   calls=2 call_targets=0x8227FDC8,0x8246D788
block=27  guest=8228068C body_total=193035  body_peak_delta=61364  body_peak_tpe=0   calls=0 branch/CR block
block=104 guest=8228130C body_total=141877  body_peak_delta=48141  body_peak_tpe=4   calls=3
block=130 guest=82281938 body_total=132094  body_peak_delta=45954  body_peak_tpe=4   calls=4
```

Call-path report:

```text
target=0x8227F1D8 blocks=1 block_guests=822809F4 charged_body_total=1937201 charged_body_peak_delta=665665
target=0x8247BE20 blocks=2 block_guests=822809D0,822809F4 charged_body_total=1937201 charged_body_peak_delta=665665
target=0x8227FDC8 blocks=2 block_guests=82280598,82281114 charged_body_total=267405 charged_body_peak_delta=79026
target=0x8246D788 blocks=5 block_guests=82280598,822812B8,8228178C,822819FC,82281B54 charged_body_total=212460
```

The hottest `8227FEE8` row is not the static vector-heavy block. It is
`822809F4`, a short call-heavy block:

```text
call_targets=bl->0x8227F1D8:1,bl->0x8247BE20:1
context_load_slots=r[1]:1,r[14]:1,r[15]:1,r[19]:1,r[26]:1,r[27]:1
context_store_slots=lr:2,r[3]:2,r[4]:2,r[5]:2,r[6]:2,cr6.1:1,cr6.all_equal:1,cr6.none_equal:1,r[11]:1,r[7]:1,r[8]:1
```

The static vector-heavy blocks are still visible, but not justified as the next
peephole:

```text
82280A68 body_total not in top body blocks; static vmx=14/16, perm=24, calls=3
82280E1C body_total=15296, static vmx=15/15, perm=24
```

## Interpretation

This capture changes the next action. `8227FEE8` block body-time now exists,
and it says not to start the earlier `82280A68` / `82280E1C` vector load/store
experiment yet. The dynamic wall inside `8227FEE8` is mostly inclusive callee
work charged to `822809F4`, especially the direct calls to `0x8227F1D8` and
`0x8247BE20`.

Do not patch `8227FEE8` vector lowering or broad GPR caches from this capture
alone. The next evidence step should isolate which callee is hot, then inspect
that callee's HIR/body shape before a default-off codegen experiment.

## Next Action

Run a route-stabilized callee/call-edge capture for `8227FEE8`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "8227FEE8,8227F1D8,8247BE20" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileCallEdgeFilter "8227FEE8" -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

If that capture is route-clean, use the call-edge/body rows to choose between:

- focused HIR/body-time for `8227F1D8`;
- focused HIR/body-time for `8247BE20`;
- a small, default-off, function-gated call setup/state-traffic patch around
  `822809F4` only if the callee split proves the call setup itself is the cost.

Keep `arm64_context_promotion_gpr_livein_r1`, broad GPR caches, call-edge
cvars, and stale vector/block peepholes default-off unless a fresh capture
enables them for one filtered run.
