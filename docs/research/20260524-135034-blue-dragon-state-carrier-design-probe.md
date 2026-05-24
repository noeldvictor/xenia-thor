# Blue Dragon State-Carrier Design Probe

Date: 2026-05-24 13:50 EDT

Branch: `master`

Starting commit: `984458da8`

## Goal

Turn the offline state-carrier requirements table into a default-off runtime
probe that can measure the dynamic size and safety shape of a future
`82282490 -> 82287788` carrier design without changing generated behavior.

Input audit:

- `docs/research/20260524-133027-8228252c-state-carrier-design-audit.md`

## Patch

Added default-off cvar:

```text
arm64_blue_dragon_state_carrier_design_audit=false
```

The probe is title/function/PC/offset gated to the audited path and only emits
atomic counters when the cvar is enabled.

It counts:

- parent `82282490` seed stores for `f[1]` and `fpscr` at `82282594`;
- callee `82287788` `f[1]` candidate reads, split into helper-preserved and
  child-preserved read classes;
- callee `82287788` child-call points that any `f[1]` design must preserve
  across;
- callee `82287788` `fpscr` reads and dirty writes;
- callee `82287788` call-visible `fpscr` writeback points at `82287ED4`,
  `82287EDC`, `82287EE4`, and `82288220`;
- all six callee call/kill points that would break a naive `fpscr` carrier;
  and
- unclassified/fallback `f[1]` or `fpscr` accesses if the route diverges from
  the audited HIR shape.

The speed-profile log row is:

```text
A64 Blue Dragon state-carrier design audit: f1_seed=... f1_read=... f1_helper_read=... f1_child_read=... f1_child_call=... f1_fallback=... fpscr_seed=... fpscr_read=... fpscr_dirty_write=... fpscr_required_writeback=... fpscr_call_kill=... fpscr_fallback=...
```

The Android launcher bridge and `tools/thor/thor_xenia_debug.ps1` now expose the
cvar as:

```powershell
-Arm64BlueDragonStateCarrierDesignAudit true
```

## Files Changed

- `src/xenia/cpu/backend/a64/a64_backend.cc`
- `src/xenia/cpu/backend/a64/a64_backend.h`
- `src/xenia/cpu/backend/a64/a64_emitter.cc`
- `src/xenia/cpu/backend/a64/a64_sequences.cc`
- `android/android_studio_project/app/src/main/java/jp/xenia/emulator/EmulatorActivity.java`
- `tools/thor/thor_xenia_debug.ps1`

## Validation

Native build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
```

Result:

```text
BUILD SUCCESSFUL in 14s
```

APK shell / Java bridge build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell
```

Result:

```text
BUILD SUCCESSFUL in 3s
```

Packaged APK SHA-256 after the build:

```text
5A80DD15AF4471FABE16E7370D6CA5EB25FC8582993F0208BE98DB33EFCFA2FD
```

No Thor route capture was run in this slice; this is a committed probe patch.

## Next Experiment

Deploy and run a route-stabilized audit capture, not a quiet speed A/B:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64BlueDragonStateCarrierDesignAudit true -StopAppAfterCapture true
```

Use the counter row to decide whether the next behavior experiment should be a
caller-seeded `f[1]` design, an `fpscr` dirty/writeback design, or a rejection
of both. Do not enable the probe in presets and do not judge FPS from the audit
capture.
