# A64 Guest-Call Fast-Entry Audit Skeleton

Date: 2026-05-26

## Summary

Added the default-off `arm64_guest_call_fast_entry_audit` skeleton for direct
guest calls. This is a counter/logging surface only. It does not emit an
alternate entry path, does not replace `A64Function::machine_code()`, and does
not change the global indirection slot.

## Why This Exists

`docs/research/20260526-040500-a64-guest-call-fast-entry-feasibility.md`
showed that a fast-entry design is only viable as a separate guarded entry path
or stub. The next evidence needed is route-wide eligibility and blocker
pressure, not behavior.

## Implementation

- Added native cvars:
  - `arm64_guest_call_fast_entry_audit`
  - `arm64_guest_call_fast_entry_audit_function`
  - `arm64_guest_call_fast_entry_audit_budget`
- Added `A64Emitter::MaybeLogGuestCallFastEntryAudit`.
- Added Android `EmulatorActivity` extra pass-through.
- Added Thor launcher parameters:
  - `-Arm64GuestCallFastEntryAudit`
  - `-Arm64GuestCallFastEntryAuditFunction`
  - `-Arm64GuestCallFastEntryAuditBudget`

The audit scans HIR at compile time and logs one summary row per selected
function. It counts direct calls, conditional calls, eligible regular calls,
tail/indirect/extern/unresolved/helper blockers, normal-entry fallback
pressure, stackpoint sensitivity, parent `r3-r10/lr` store traffic, dirty flush
points, and the explicit invariants:

- `payload_materializations_allowed=0`
- `behavior_changed=0`
- `alternate_codegen=0`
- `normal_entry=unchanged`
- `global_indirection=unchanged`

## Important Limitation

The compile-time row sees caller HIR only. Callee first-use coverage is left as
`callee_first_use_missing` for the existing log-backed HIR coverage tool to join
later. This is intentional: it avoids pretending the compiler can infer callee
entry safety without a real callee HIR map.

## Next Step

FullDeploy, then run a route-clean Blue Dragon capture with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64GuestCallFastEntryAudit true -Arm64GuestCallFastEntryAuditFunction 0x82281D28 -Arm64GuestCallFastEntryAuditBudget 16 -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -StopAppAfterCapture true
```

Do not run a quiet speed A/B until the audit capture proves broad, body-weighted
eligible traffic with manageable flush/fallback pressure.
