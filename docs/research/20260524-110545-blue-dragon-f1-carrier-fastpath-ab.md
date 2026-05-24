# Blue Dragon f[1] Carrier Fastpath A/B

Date: 2026-05-24 11:05 EDT

## Goal

Turn the route-clean `82287788` `f[1]` carrier audit into a default-off
generated-code probe and test whether replacing the audited `f[1]` context
reloads helps the Blue Dragon opening route.

The prior audit was:

- `docs/research/20260524-104145-blue-dragon-f1-carrier-runtime-audit.md`

## Implementation

Added default-off cvar:

```text
arm64_blue_dragon_f1_carrier_fastpath
```

The fastpath is gated to Blue Dragon function `82287788`, context offset `296`
(`f[1]`), and the audited static PCs only.

Instead of reserving a host FPR across the function, the A64 emitter allocates
one function-local stack slot when the cvar is enabled for `82287788`:

- `82287798`: load `f[1]` from the PPC context and seed the stack slot.
- `82287828`, `82287A1C`, `82287A2C`, `82287AA4`, `82287CF8`, `82287D10`,
  `82287D8C`, `82287EA8`, `82287F1C`: load `f[1]` from that stack slot instead
  of reloading context offset `296`.

The route-specific assumption is the one proven by the prior audits:

- `__savegprlr_28` and `__restgprlr_28` are GPR/LR-only helpers;
- direct child calls to `0x821CE028` do not access `f[1]`;
- no `f[1]` stores were observed in `82287788`.

The existing default-off audit cvar now also counts fastpath seed and reuse
hits:

```text
arm64_blue_dragon_f1_carrier_audit
```

Files changed:

- `src/xenia/cpu/backend/a64/a64_backend.cc`
- `src/xenia/cpu/backend/a64/a64_backend.h`
- `src/xenia/cpu/backend/a64/a64_emitter.cc`
- `src/xenia/cpu/backend/a64/a64_emitter.h`
- `src/xenia/cpu/backend/a64/a64_sequences.cc`
- `android/android_studio_project/app/src/main/java/jp/xenia/emulator/EmulatorActivity.java`
- `tools/thor/thor_xenia_debug.ps1`

## Validation

Build/deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both passed. Tested APK SHA-256:

```text
A2A59F7777F77B52526ABF5E401194A4A7490D71B4F7A878ED8099B0CA7557A9
```

Route-safety / audit capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64BlueDragonF1CarrierFastpath true -Arm64BlueDragonF1CarrierAudit true -StopAppAfterCapture true
```

Artifacts:

- Capture: `scratch/thor-debug/20260524-105424-*`
- Log: `scratch/thor-debug/20260524-105424-speed-logcat.txt`
- Metadata: `scratch/thor-debug/20260524-105424-meta.txt`
- Screenshot: `scratch/thor-debug/20260524-105424-screenshot.png`

Route and health:

- Final screenshot reached the visible opening sky / dragon-wing route.
- Fatal-marker search was clean for raw and filtered logcat.
- Metadata `head=1bd6d0a5f` is pre-commit because this capture was taken before
  committing the current slice; use the APK SHA above for the tested binary.

Final audit row:

```text
total=667554/2053645
helper_preserved=334522/1028349
child_preserved=333032/1025296
seed=223805/688280
reuse=443749/1365365
```

Final body-time rows:

```text
82282490 body_ticks_total=30401688 entries_delta=56392 ticks_per_entry=163 code_size=87168
82281D28 body_ticks_total=7648314 entries_delta=26904 ticks_per_entry=92 code_size=96908
82287788 body_ticks_total=3636251 entries_delta=223805 ticks_per_entry=5 code_size=36016
```

## Quiet Same-APK A/B

Control:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64BlueDragonF1CarrierFastpath false -Arm64BlueDragonF1CarrierAudit false -StopAppAfterCapture true
```

Experiment:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64BlueDragonF1CarrierFastpath true -Arm64BlueDragonF1CarrierAudit false -StopAppAfterCapture true
```

Control capture:

- `scratch/thor-debug/20260524-105813-*`
- visible route: opening sequence, further along at the
  `Microsoft Game Studios Presents` frame
- fatal-marker search: clean
- final `82287788`: `delta=177057`, `total=1685197`, `code_size=35568`
- final `82282490`: `delta=49488`, `total=440594`, `code_size=86828`
- final perf: Main Thread `100%`, GPU Commands `7.6%`, XMA Decoder `0.0%`

Fastpath capture:

- `scratch/thor-debug/20260524-110156-*`
- visible route: opening sky / dragon-wing frame, earlier than control
- fatal-marker search: clean
- final `82287788`: `delta=241703`, `total=688517`, `code_size=35572`
- final `82282490`: `delta=60844`, `total=177562`, `code_size=86828`
- final perf: Main Thread `96.1%`, GPU Commands `15.3%`, XMA Decoder `53.8%`

## Interpretation

The fastpath is route-safe and the audit proves it activates heavily. It is not
a speed win in the quiet A/B.

The fastpath made `82287788` four bytes larger in the quiet run
(`35568 -> 35572`), did not reduce the main `82282490` wall, and the fastpath
run visibly reached an earlier opening frame than the same-APK control. The
route remains CPU/JIT-heavy, but this narrow stack-slot carrier is too small and
too local to move it.

## Decision

Keep both `arm64_blue_dragon_f1_carrier_fastpath` and
`arm64_blue_dragon_f1_carrier_audit` default-off and out of Blue Dragon
presets.

Do not repeat this exact f[1] carrier A/B unchanged. The next useful speed
lane should move to a broader `8228252C-822825C4` parent/callee
state/vector/FPR reduction, especially a design that reduces the live
`82282490 -> 82287788` state round-trip without hiding guest-visible `r[3]`,
`f[1]`, `fpscr`, or `lr` state. Require another offline audit before the next
Thor behavior patch.
