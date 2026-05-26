# A64 Non-Closed GPR Cache Audit Skeleton

Date: 2026-05-26 09:05 EDT

## Question

Can the guest-state/cache lane continue without reopening closed `r1`/`r11`,
fast-entry, or payload-carrier behavior?

## Change

Added a default-off, counter-only post-promotion audit:

- Cvars:
  - `arm64_guest_state_nonclosed_cache_audit=false`
  - `arm64_guest_state_nonclosed_cache_audit_function=0`
- Thor launcher params:
  - `-Arm64GuestStateNonclosedCacheAudit`
  - `-Arm64GuestStateNonclosedCacheAuditFunction`
- Android intent forwarding for both cvars.

The audit runs after normal `ContextPromotionPass` promotion and DSE. It tracks
only the strict non-closed GPR target set from the prior ranking note:
`r31,r30,r29,r28,r27`.

## Behavior Contract

Generated behavior is unchanged:

- no store elision;
- no `LOAD_CONTEXT` replacement;
- no payload materialization;
- normal entry and global indirection untouched.

Log rows include `payload_materializations_allowed=0`,
`store_elision_allowed=0`, `context_load_replacement_allowed=0`, and
`behavior_changed=0`.

## Counters

The audit emits compile-time rows for:

- candidate loads and stores;
- clean and dirty hits possible;
- per-slot `r31/r30/r29/r28/r27` loads/stores/hits/fallbacks;
- miss reasons: entry, multi-pred, label, call, helper, branch, return, trap,
  external visibility, overlap, volatile;
- flush reasons for the same boundary classes;
- `normal_fallback`;
- `peak_live_slots`;
- `estimated_spill_pressure` using `live_slots_over_2`.

The spill model is intentionally conservative because A64 host GPR pressure is
already known to be real. It is a triage counter, not a behavior claim.

## Validation

Passed:

```powershell
$null = [scriptblock]::Create((Get-Content -Raw tools\thor\thor_xenia_debug.ps1))
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
```

`git diff --check` only reported existing CRLF conversion warnings.

## Decision

This is audit plumbing only, not speed proof. Do not patch guest-state cache
behavior or run a quiet speed A/B from this skeleton.

Next useful slice:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64OffsetMemoryAddressFastpath true -Arm64GuestStateNonclosedCacheAudit true -Arm64GuestStateNonclosedCacheAuditFunction 0x82281D28 -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -StopAppAfterCapture true
```

Repeat the capture for `0x82282490` and `0x82287788` only if the first capture
shows material post-promotion volume with manageable flush/spill pressure.
