# 8227FEE8 Callee Call-Edge Split

Date: 2026-05-23 12:40 EDT

## Goal

Split the call-heavy `822809F4` block found in the route-clean `8227FEE8`
block body-time capture, so the next A64 speed patch targets the callee that
actually burns cycles instead of the stale static vector blocks.

## Capture

- Capture prefix: `scratch/thor-debug/20260523-123406-*`
- Logcat: `scratch/thor-debug/20260523-123406-speed-logcat.txt`
- Screenshot: `scratch/thor-debug/20260523-123406-screenshot.png`
- HEAD: `b26dfa100`
- APK SHA-256:
  `962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5`
- Route: visible opening sky/wing scene
- Fatal-marker search: clean

Launch lane:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "8227FEE8,8227F1D8,8247BE20" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileCallEdgeFilter "8227FEE8" -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Key cvars:

- `arm64_context_promotion_gpr_livein_r1=false`
- `arm64_speed_profile_body_time_filter=8227FEE8,8227F1D8,8247BE20`
- `arm64_speed_profile_body_time_after_ms=120000`
- `arm64_speed_profile_call_edge_filter=8227FEE8`
- `arm64_speed_profile_call_edge_audit_only=false`
- `log_level=1`

## Results

Final delayed body-time rows:

```text
8227F1D8 body_ticks_delta=844204 body_ticks_total=3714635 entries_delta=9941 ticks_per_entry=84 code_size=20180
8227FEE8 body_ticks_delta=688276 body_ticks_total=4265926 entries_delta=1921 ticks_per_entry=358 code_size=57084
8227EFC8 body_ticks_delta=10597 body_ticks_total=38217 entries_delta=2210 ticks_per_entry=4 code_size=25752
8247BE20 body_ticks_delta=5431 body_ticks_total=27747 entries_delta=2600 ticks_per_entry=2 code_size=796
```

Dynamic call-edge split from `8227FEE8`:

```text
edge=21 block_guest=822809F4 target=0x8227F1D8 calls_total=26098 calls_peak_delta=8094 body_ticks_total=2031295 body_ticks_peak_delta=1137492 ticks_per_call_peak=216
edge=53 block_guest=82281938 target=0x82285308 calls_total=27327 calls_peak_delta=8885 body_ticks_total=105077 body_ticks_peak_delta=37235 ticks_per_call_peak=4
edge=16 block_guest=8228094C target=0x821981E0 calls_total=26098 calls_peak_delta=8094 body_ticks_total=97749 body_ticks_peak_delta=32543 ticks_per_call_peak=4
```

The earlier suspect `8247BE20` is not a priority in this route. It ended at
only `27747` body ticks and `code_size=796`, while `8227F1D8` reached
`3714635` function body ticks and owns the dominant measured edge under
`8227FEE8`.

The final perf sample still points at CPU/JIT work rather than a GPU wall:
Main Thread `92.3%`, XMA Decoder `11.5%`, GPU Commands `7.6%`, Draw Thread
`0.0%`. Android `gfxinfo` frame counts are UI frames, not Blue Dragon FPS
proof.

## Offline Reports

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_call_path_report.ps1 -LogPath scratch\thor-debug\20260522-233545-speed-logcat.txt -Function 8227FEE8 -Phase OptHIR -BlockProfileLog scratch\thor-debug\20260523-123406-speed-logcat.txt -Top 16
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_hotpath_report.ps1 -LogPath scratch\thor-debug\20260523-123406-speed-logcat.txt -Function 8227F1D8 -Phase OptHIR -Top 20
```

`thor_hir_hotpath_report.ps1` found no `8227F1D8` HIR in the current capture
because this run did not enable a filtered function dump for that callee.

## Decision

Do not write a codegen patch from the old static `82280A68` / `82280E1C`
vector candidates or from broad GPR cache ideas yet. The next measured target
is `8227F1D8`.

Next useful slice:

1. Run a route-stabilized filtered HIR and delayed body/block-time capture for
   `8227F1D8`.
2. Keep `8227FEE8` in the body-time filter as the parent comparator.
3. Keep call-edge cvars off unless the capture specifically needs another edge
   split.
4. If the filtered capture black-idles, inspect the owner attribution fields
   before changing generated-code behavior.

Suggested capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -DisassembleFunctions true -DisassembleFunctionFilter 8227F1D8 -Arm64SpeedProfileBodyTimeFilter "8227FEE8,8227F1D8" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileBlockFilter "8227F1D8" -Arm64SpeedProfileBlockBodyTime true -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```
