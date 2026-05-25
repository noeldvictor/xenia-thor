# A64 Guest-State Register-Cache Audit Skeleton

## Summary

Commit-under-test: pending at note time.

This slice adds a default-off, counter-only `arm64_guest_state_register_cache_audit`
at the `ContextPromotionPass` boundary. It does not replace HIR loads, elide
stores, materialize payloads, or change generated code behavior. The first
tracked slots are the mature-backend gate's clean INT64 candidates:

- `r[1]` at `PPCContext::r + 1 * 8`
- `r[11]` at `PPCContext::r + 11 * 8`

The audit can be scoped with:

```text
arm64_guest_state_register_cache_audit=true
arm64_guest_state_register_cache_audit_function=0x82282490
```

The Thor launcher bridge now exposes these as:

```powershell
-Arm64GuestStateRegisterCacheAudit true
-Arm64GuestStateRegisterCacheAuditFunction 0x82282490
```

## Counters

The compile-time audit logs:

- `candidate_loads`
- `candidate_stores`
- `clean_hits_possible`
- `dirty_hits_possible`
- miss reasons: entry, multi-pred, volatile/helper, overlap, call, branch,
  label, return, trap, external visibility
- flush reasons: call, helper, branch, label, return, trap, external visibility
- `estimated_spill_pressure`
- `normal_fallback`
- per-slot `r1` / `r11` load, store, hit, and fallback totals

The first row also prints:

```text
payload_materializations_allowed=0 behavior_changed=0
```

This is deliberate: the path is only a structural feasibility counter until a
route-clean Thor capture proves enough clean/dirty hit opportunities and sane
flush pressure.

## Files

- `src/xenia/cpu/compiler/passes/context_promotion_pass.cc`
- `src/xenia/cpu/compiler/passes/context_promotion_pass.h`
- `tools/thor/thor_xenia_debug.ps1`
- `android/android_studio_project/app/src/main/java/jp/xenia/emulator/EmulatorActivity.java`

## Validation

```powershell
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell
```

Results:

- `git diff --check` passed, with the repo's expected CRLF warnings only.
- `NativeCore` passed.
- `ApkShell` passed.

## Decision

Do not patch register-cache behavior yet. The next slice should deploy/run a
route-clean Thor audit capture with the audit enabled for `82282490`, then
inspect the `A64 guest-state register-cache audit` rows for clean/dirty hit
volume, miss causes, and spill pressure.

Suggested capture shape:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64GuestStateRegisterCacheAudit true -Arm64GuestStateRegisterCacheAuditFunction 0x82282490 -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

If this capture is route-clean and the counters show enough opportunity, the
next behavior design can stay default-off and function-filtered. If clean hits
are low or flush pressure is high, return to caller-local/side-table edge
variant storage for `82282490:82282598 -> 82287788`.
