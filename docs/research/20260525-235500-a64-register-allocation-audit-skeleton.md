# A64 Register Allocation Audit Skeleton

## Summary

The reset plan ranks structural register allocation / guest-state cache work
above another micro-fastpath. This slice adds a default-off,
behavior-neutral `arm64_register_allocation_audit` so the next Thor capture can
measure whether the A64 register allocator is actually spilling in the Blue
Dragon hot functions.

This is not speed proof and not a behavior patch. With the cvar off, generated
code behavior is unchanged.

## Why This Slice

Current Blue Dragon evidence remains Main Thread / generated-code bound around
`82282490`, `82281D28`, and `82287788`. Previous guest-state cache probes
proved that narrow `r1`/`r11`, `f[1]`, fpscr, stvewx, and three-PC
`MUL_ADD_V128` lanes are not ready for another speed A/B.

The open structural question is whether A64's small integer allocation set
(`x22-x28`) is forcing spills in the hot route. Before changing allocator
behavior, the route needs direct allocator pressure counters.

## Patch

Added C++ cvars:

- `arm64_register_allocation_audit`
- `arm64_register_allocation_audit_function`

The audit logs per compiled function:

- blocks and blocks with spills
- instruction and destination value counts
- locals before/after and local slots added
- per INT/FLOAT/VEC set destination values
- allocation successes
- preferred-register attempts/hits/fallbacks
- spill requests and successful spills
- max active registers and max upcoming uses

Android / Thor launcher plumbing was added so ADB captures can enable the audit
through `EmulatorActivity` and `tools/thor/thor_xenia_debug.ps1`.

## Validation

Commands:

```powershell
powershell -NoProfile -Command '$tokens=$null; $errors=$null; [System.Management.Automation.PSParser]::Tokenize((Get-Content -Raw tools\thor\thor_xenia_debug.ps1), [ref]$errors) | Out-Null; if ($errors) { $errors | Format-List; exit 1 } else { Write-Output parser_ok }'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell
```

Result:

- PowerShell parser check passed.
- `NativeCore` passed.
- `ApkShell` passed; Gradle emitted only the existing compileSdk / deprecation
  warnings.

## Next

Run FullDeploy plus a route-clean Thor audit capture, starting with
`0x82282490`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -Arm64RegisterAllocationAudit true -Arm64RegisterAllocationAuditFunction 0x82282490 -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Decision rule:

- If `82282490` shows material INT or VEC spills and local slots added, inspect
  the worst type family before a behavior patch.
- If spill pressure is low, close the register-allocation-pressure lane for
  this route and move to helper ABI / block linking or the PERMUTE /
  LOAD_VECTOR_SHL/SHR VMX128 route-counter lane.
