# 2026-05-26 - Guest-Stack Argument Handoff Audit Skeleton

## Context

The prior mapped hot-block audit showed `82281D28:8228233C-82282370` is scalar
guest-stack argument setup, not VMX/vector/GPU work. The concrete shape is:

1. `LOAD_OFFSET` from the guest stack pointer `r1 + constant`.
2. `STORE_CONTEXT` into direct-call argument registers `r3-r10/lr`.
3. A direct guest call.

This slice adds a default-off counter-only audit for that shape. It does not
emit alternate code, replace normal entry, replace global indirection, or
materialize payload state.

## Changes

- Added cvars:
  - `arm64_guest_stack_arg_handoff_audit`
  - `arm64_guest_stack_arg_handoff_audit_function`
  - `arm64_guest_stack_arg_handoff_audit_budget`
- Added Android intent extra plumbing in
  `android/android_studio_project/app/src/main/java/jp/xenia/emulator/EmulatorActivity.java`.
- Added Thor launcher parameters and capture metadata rows in
  `tools/thor/thor_xenia_debug.ps1`:
  - `-Arm64GuestStackArgHandoffAudit`
  - `-Arm64GuestStackArgHandoffAuditFunction`
  - `-Arm64GuestStackArgHandoffAuditBudget`
- Added native compile-time rows in
  `src/xenia/cpu/backend/a64/a64_emitter.cc`:
  - `A64 guest-stack arg handoff audit ...`
  - `A64 guest-stack arg handoff target ...`
- Extended `tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1` with
  `-GuestStackArgHandoffAuditLogPath` so future captures can print
  body-weighted target rows when matching block-profile metadata is present.

## Counters

The audit counts only argument stores fed by guest-stack loads:

- `LOAD_OFFSET` from a value proven to derive from `LOAD_CONTEXT r1` plus a
  constant offset.
- `STORE_CONTEXT` into `r3-r10/lr`.
- Direct call target and callsite rows.

Rows include direct/conditional/tail call counts, resolved/compiled state,
helper blockers, normal-entry fallback pressure, stackpoint sensitivity,
context-barrier/return/debug/extern blockers, stack offsets, argument slots,
estimated avoidable bytes, and explicit behavior-neutral markers:

- `payload_materializations_allowed=0`
- `behavior_changed=0`
- `alternate_codegen=0`
- `normal_entry=unchanged`
- `global_indirection=unchanged`

## Validation

- `git diff --check` passed with existing CRLF warnings only.
- `tools/thor/thor_xenia_debug.ps1` PowerShell parser check passed.
- `tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1` PowerShell parser
  check passed.
- `tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1` old-log parser smoke
  passed against the latest `82281D28` HIR / fast-entry logs.
- First `NativeCore` build failed because the new anonymous-namespace helpers
  needed a `hir` namespace alias; this was fixed.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore`
  passed after the fix.
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell`
  passed with the existing Android Gradle compileSdk warning.

## Next

Do not run a quiet speed A/B from this skeleton. The next useful slice is a
route-clean Thor capture for `0x82281D28`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370

powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64GuestStackArgHandoffAudit true -Arm64GuestStackArgHandoffAuditFunction 0x82281D28 -Arm64GuestStackArgHandoffAuditBudget 16 -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 30 -Arm64SpeedProfileMinDelta 1 -StopAppAfterCapture true

powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_guest_call_arg_handoff_audit.ps1 -LogPath <new-speed-logcat> -ExtraLogPath scratch\thor-debug\20260526-034119-speed-logcat.txt -GuestStackArgHandoffAuditLogPath <new-speed-logcat> -Function 82281D28 -Phase OptHIR -Top 16
```

Close the lane if target rows do not show broad body-weighted guest-stack
argument handoff volume with manageable fallback and blocker pressure.
