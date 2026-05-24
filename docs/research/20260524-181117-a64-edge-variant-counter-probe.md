# A64 Edge-Variant Counter Probe

Date: 2026-05-24 18:11 EDT

## Goal

Add and validate a default-off counter-only probe for the hot Blue Dragon edge
`82282490:82282598 -> 82287788`.

The prior edge-variant design audit proved that the normal A64 entry is a
singleton, so this probe does not change generated behavior. It only reports
whether the edge compiles, how often it calls, whether a payload is being
materialized, and whether calls still fall back through the normal entry path.

## Patch

Files changed:

- `src/xenia/cpu/backend/a64/a64_backend.cc`
- `src/xenia/cpu/backend/a64/a64_backend.h`
- `src/xenia/cpu/backend/a64/a64_emitter.cc`
- `android/android_studio_project/app/src/main/java/jp/xenia/emulator/EmulatorActivity.java`
- `tools/thor/thor_xenia_debug.ps1`

New cvar:

- `arm64_blue_dragon_edge_variant_audit=false`
- Thor launcher: `-Arm64BlueDragonEdgeVariantAudit true`
- Android extra: `arm64_blue_dragon_edge_variant_audit`

Counters:

- `eligible_compiles`: exact edge found while compiling `82282490`.
- `variant_storage_missing`: exact edge is known, but no edge-variant storage or
  side table exists yet.
- `eligible_calls`: runtime executions of the exact edge.
- `payload_materializations`: remains zero in this patch because no payload is
  emitted.
- `normal_entry_fallbacks`: exact edge used normal call semantics.
- `indirection_fallbacks`: exact edge went through the guest-address
  indirection table rather than an edge-specific entry.
- `call_kills`: call-visible callee sites in `82287788` that would kill or
  force flushing a carried payload.
- `variant_misses`: exact edge had no variant entry.

## Validation

Builds:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy
```

Both passed.

Route capture:

- Capture: `scratch/thor-debug/20260524-180737-*`
- Base HEAD in capture metadata: `6ca6651b4`
- APK SHA: `25BEA1C91D7F9D6F57F808A1C8CC91F8115E485E3FDA491D2D81EB600B5F7B97`
- Cvars: `arm64_blue_dragon_edge_variant_audit=true`,
  `arm64_speed_profile_interval_ms=15000`, `log_level=1`
- Route: visible opening sky / dragon-wing screenshot.
- Fatal-marker search: clean.
- Final thread sample: Main Thread `100%`, GPU Commands `7.6%`,
  Draw Thread `0.0%`, XMA Decoder `7.6%`.

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -LiveCaptureSeconds 180 -PerfSampleSeconds "60,120,180" -LogLevel 1 -Arm64BlueDragonEdgeVariantAudit true
```

## Final Counter Row

From `scratch/thor-debug/20260524-180737-speed-logcat.txt`:

```text
eligible_compiles=0/1 variant_storage_missing=0/1
eligible_calls=240752/675279 payload_materializations=0/0
normal_entry_fallbacks=240752/675279 indirection_fallbacks=240752/675279
call_kills=605393/1695703 variant_misses=240752/675279
```

Earlier in the same route the exact edge first compiled:

```text
eligible_compiles=1/1 variant_storage_missing=1/1
eligible_calls=17613/17613 payload_materializations=0/0
normal_entry_fallbacks=17613/17613 indirection_fallbacks=17613/17613
call_kills=44173/44173 variant_misses=17613/17613
```

## Interpretation

- The exact hot edge compiles once and is very active on the opening route.
- Every eligible call is still a normal-entry fallback and variant miss.
- The path currently uses the global guest-address indirection table, matching
  the prior singleton-entry audit.
- No payload is materialized, by design, so this patch is route-clean audit
  evidence only.
- The callee has substantial call-kill pressure: `call_kills=1695703` by the
  final row. A future edge variant needs explicit helper/child-call kill and
  writeback rules before changing behavior.
- The final thread sample still points to the CPU/JIT lane first: Main Thread is
  one full core while GPU Commands is low.

## Decision

Keep `arm64_blue_dragon_edge_variant_audit` default-off and out of presets.

Do not run a quiet speed A/B from this patch; there is no behavior change and no
payload. The next useful slice is an offline design for the edge side-table or
caller-local variant storage plus exact f[1]/fpscr payload kill/writeback rules,
or a CFG-aware fpscr writeback audit before any generated-code change.
