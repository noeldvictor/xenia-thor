# Blue Dragon 82287788 f[1] Carrier Runtime Audit

Date: 2026-05-24 10:41 EDT

## Goal

Turn the offline `82287788` `f[1]` carrier evidence into a default-off runtime
counter before changing generated behavior. The probe should answer whether the
loads that are now known to be helper-preserved or `821CE028`-child-preserved
are frequent enough to justify a real carrier-lowering experiment.

## Implementation

Added default-off cvar:

```text
arm64_blue_dragon_f1_carrier_audit
```

It instruments only `LOAD_CONTEXT_F64` for Blue Dragon function `82287788`,
offset `296` (`f[1]`), and only these audited PPC PCs:

- Helper-preserved: `82287798`, `82287828`.
- Child-preserved through `821CE028`: `82287A1C`, `82287A2C`, `82287AA4`,
  `82287CF8`, `82287D10`, `82287D8C`, `82287EA8`, `82287F1C`.

The cvar emits counters only; it does not replace the load, keep a carrier, or
change guest-visible state.

Files changed:

- `src/xenia/cpu/backend/a64/a64_backend.cc`
- `src/xenia/cpu/backend/a64/a64_backend.h`
- `src/xenia/cpu/backend/a64/a64_sequences.cc`
- `android/android_studio_project/app/src/main/java/jp/xenia/emulator/EmulatorActivity.java`
- `tools/thor/thor_xenia_debug.ps1`

## Validation

Build/deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both passed. Installed APK SHA-256:

```text
514C6A8C0C14820BFE0ED48CF280D00371B30602AEEE5CD86AE06A82E05D139F
```

Targeted capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64BlueDragonF1CarrierAudit true -StopAppAfterCapture true
```

Artifacts:

- Capture: `scratch/thor-debug/20260524-103438-*`
- Log: `scratch/thor-debug/20260524-103438-speed-logcat.txt`
- Metadata: `scratch/thor-debug/20260524-103438-meta.txt`
- Screenshot: `scratch/thor-debug/20260524-103438-screenshot.png`

Route and health:

- Final screenshot reached the visible opening sky / dragon-wing route.
- Fatal-marker search: no `Fatal signal`, `AndroidRuntime`, `FATAL EXCEPTION`,
  `VK_ERROR_DEVICE_LOST`, `GPU is hung`, tombstone, or crash markers found.
- Metadata `head=b9ad485bf` is pre-commit because the capture was taken before
  committing this slice; use the APK SHA above to identify the tested binary.

Final audit rows:

```text
A64 Blue Dragon f1 carrier audit:
  total=755910/2159587
  helper_preserved=379058/1081707
  child_preserved=376852/1077880
```

Final body-time rows:

```text
82282490 body_ticks_total=32144132 entries_delta=63812 ticks_per_entry=143 code_size=87168
82281D28 body_ticks_total=7991955 entries_delta=30444 ticks_per_entry=91 code_size=96908
82287788 body_ticks_total=3578711 entries_delta=253513 ticks_per_entry=5 code_size=35732
```

Final 175s perf snapshot still shows the route is CPU/JIT-heavy:

```text
Main Thread 96.1%
XMA Decoder 7.6%
GPU Commands 7.6%
```

## Interpretation

The `f[1]` carrier lane is dynamically real: the audited capture counted about
2.16 million possible `f[1]` load replacements in the opening route, split
almost evenly between GPR/LR-helper-preserved and `821CE028`-child-preserved
regions.

This is still not speed proof. The audit uses atomic counters in generated
code, so the capture is trace-heavy. Also, `82287788` is a high-entry but cheap
callee in this route (`ticks_per_entry=5`), so the upside is bounded unless the
real carrier also reduces parent/callee state traffic around
`82282490 -> 82287788`.

## Decision

Keep `arm64_blue_dragon_f1_carrier_audit` default-off and out of presets. Do
not combine this with `fpscr`, broad FPR caches, or call-boundary store
suppression yet.

Next useful slice: implement a default-off, function-gated `82287788` `f[1]`
carrier replacement probe, only for offset `296`, with explicit kills at
unknown calls, indirect calls, exits, exceptions, and any exact or overlapping
`f[1]` store. Treat only `__savegprlr_28`, `__restgprlr_28`, and direct calls
to `0x821CE028` as preserving. First require route-clean proof with counters,
then run a quiet same-APK A/B with only the carrier cvar changed.
