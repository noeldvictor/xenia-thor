# Arm64 Guest-State Register-Cache Residual Audit

## Summary

Implemented the default-off post-promotion residual audit requested by
`docs/research/20260525-161334-r1-clean-load-source-gate.md`.

The new audit is behavior-neutral. It does not replace loads, elide stores,
materialize payloads, or change generated code. It reuses the existing
guest-state register-cache accounting for `r[1]` and `r[11]`, but runs after
the normal `ContextPromotionPass::PromoteBlock` path and dead-store removal.
This should tell us how much `r1` / `r11` traffic remains after the same-block
clean-load opportunities have already been consumed by the normal pass.

## Cvars And Launch Flags

Native cvars:

```text
arm64_guest_state_register_cache_residual_audit=false
arm64_guest_state_register_cache_residual_audit_function=0x82282490
```

Thor launcher flags:

```text
-Arm64GuestStateRegisterCacheResidualAudit true
-Arm64GuestStateRegisterCacheResidualAuditFunction 0x82282490
```

Android `EmulatorActivity` now forwards both extras to native.

## Log Rows

The existing pre-promotion audit row name is preserved:

```text
A64 guest-state register-cache audit fn <guest>: ...
```

The new post-promotion row is distinct:

```text
A64 guest-state register-cache residual audit fn <guest>: ...
```

It reports the same fields as the pre-promotion audit:

```text
candidate_loads candidate_stores clean_hits_possible dirty_hits_possible
normal_fallback estimated_spill_pressure
payload_materializations_allowed=0 behavior_changed=0
miss_* flush_* r1/r11 per-slot totals
```

## Files Changed

- `src/xenia/cpu/compiler/passes/context_promotion_pass.cc`
- `src/xenia/cpu/compiler/passes/context_promotion_pass.h`
- `android/android_studio_project/app/src/main/java/jp/xenia/emulator/EmulatorActivity.java`
- `tools/thor/thor_xenia_debug.ps1`

## Validation

Passed:

```text
PowerShell parser for tools/thor/thor_xenia_debug.ps1
tools/thor/thor_build.ps1 -Mode NativeCore
tools/thor/thor_build.ps1 -Mode ApkShell
```

`ApkShell` emitted only the existing Android Gradle plugin compileSdk warning
and Java deprecation notes.

## Next Capture

Next useful slice is a FullDeploy plus route-clean Thor residual capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64GuestStateRegisterCacheResidualAudit true -Arm64GuestStateRegisterCacheResidualAuditFunction 0x82282490 -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Do not run a quiet speed A/B or implement register-cache behavior until this
residual row is route-clean on Thor. If residual `r1` volume collapses after
normal promotion and DSE, return to caller-local/side-table edge-variant
storage for `82282490:82282598 -> 82287788`.
