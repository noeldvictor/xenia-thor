# A64 GPR/LR Helper Inline

Date: 2026-05-19 17:07 EDT
Target: AYN Thor Max, Blue Dragon Disc 1
Branch: `master`

## Question

Can the A64 backend inline the PPC compiler `__savegprlr_*` and
`__restgprlr_*` ABI helpers safely enough to remove them from Blue Dragon's
hot guest-call profile?

## Change

Added `a64_inline_gprlr_helpers`, default-on, to the A64 backend. When a direct
guest call targets a helper discovered by `XexModule::FindSaveRest()`:

- `__savegprlr_14..31` is inlined as big-endian 64-bit GPR stack stores plus
  the 32-bit LR slot from `r12`.
- `__restgprlr_14..31` is inlined as big-endian 64-bit GPR stack loads plus the
  32-bit LR slot into `r12` and `lr`.
- The restore path compares the restored LR with this function's guest return
  address. Normal returns branch to the current A64 epilog; nonstandard LR
  values fall back to the normal tail-jump/code-cache path.

The Android activity and Thor debug script forward the cvar so captures can
force it on or off.

## Failed First Probe

The first restore implementation always branched to the current epilog after
loading LR. It stayed alive, but Blue Dragon failed to reach the known visible
route:

- Capture: `scratch/thor-debug/20260519-170818-*`
- APK SHA-256:
  `9EF18F96D2C86D6C743B49E5E0B740CB48EF10092E3EB98AAD3AE8DABE0F18A3`
- Screenshot: black.
- Profile: stuck in `8246B408` / `8248B040`.

Control run on the same APK with `-A64InlineGprLrHelpers false` reached the
opening scene:

- Capture: `scratch/thor-debug/20260519-171136-*`

Conclusion: `__restgprlr_*` cannot blindly be treated as "return from this
host frame"; it must preserve the old LR-mismatch tail branch behavior.

## Validation Run

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 155 -PerfSampleSeconds "75,140" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -A64InlineGprLrHelpers true
```

Capture:

- `scratch/thor-debug/20260519-171625-*`
- APK SHA-256:
  `E188217B67097FF866006B7115FCB47B3B13AD044550082A6D75DEDDA794BCDB`

Result:

- Reached the visible Blue Dragon opening scene.
- No `AndroidRuntime`, fatal signal, `VK_ERROR_DEVICE_LOST`, GPU hung,
  tombstone, or ANR line was found.
- One ADB logcat pull failed near the end and recovered with reconnect; the
  process stayed alive in `EmulatorActivity`.

## Findings

The final profiled run had no `__savegprlr_*` or `__restgprlr_*` entries in the
top list.

Late profile interval:

- `funcs=6142`
- `entry_delta=183881407`
- `h2g=2243/30808`
- `g2h=256746/4134422`
- `direct=183325520/1385684476`
- `indirect=497721/10205240`
- `extern=249422/4036425`
- `resolves=126/6144`
- `resolve_misses=0/0`

Hottest late functions:

- `8248B040`, delta `89,485,052`
- `8246B408`, delta `89,410,658`
- `82274DB0`, delta `311,422`
- `82287788`, delta `306,183`
- `826BF770`, delta `218,414`

Newly visible helper families:

- `__savevmx_124` / `__restvmx_124`
- `__savefpr_27` / `__restfpr_27`

## Decision

Keep `a64_inline_gprlr_helpers` on by default. The runtime LR mismatch check is
required. The next A64 helper-inline candidates are FPR and VMX save/restore
helpers, but the dominant wall remains the Blue Dragon draw-wait pair
`8246B408` / `8248B040`.
