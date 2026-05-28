# Project Sylpheed Reenter Audit Skeleton

## Summary

Project Sylpheed's New Game/default-difficulty abort is now instrumentable
without changing generated-code behavior. This slice adds a default-off
Android/A64 reenter audit around the direct throw path:

- `KeSetCurrentStackPointers_entry`
- `XThread::Reenter`
- the existing export-trampoline route that calls the kernel export

The audit does not catch, suppress, defer, or replace `reenter_exception`. It
only logs the state needed to design a guarded non-throw path later.

## What Changed

New cvars:

```text
xboxkrnl_reenter_audit=false
xboxkrnl_reenter_audit_budget=64
xboxkrnl_reenter_audit_guest_tids=
```

Android launch plumbing now passes those extras through `EmulatorActivity`, and
stable launcher launches force `xboxkrnl_reenter_audit=false` unless a Thor
debug launch explicitly enables it.

Thor debug launch plumbing now accepts:

```powershell
-XboxkrnlReenterAudit true
-XboxkrnlReenterAuditBudget 64
-XboxkrnlReenterAuditGuestTids <filter>
```

## Logged Evidence

`KeSetCurrentStackPointers` audit rows include:

- current and target guest thread IDs
- current handle and guest object
- target guest object
- same-thread / fiber / will-reenter flags
- LR, CTR, old r1, r13
- requested stack pointer/base/limit values
- APC disable count
- explicit `throws_exception=1` and `behavior_changed=0` markers when the
  current code will throw

`XThread::Reenter` audit rows include:

- guest thread ID, handle, guest object
- target reenter address
- LR, CTR, r1
- explicit `throws_exception=1` and `behavior_changed=0`

## Decision

This is not a compatibility fix and should not be used as proof that Sylpheed
New Game works. The next useful proof is a fresh library-launched Project
Sylpheed New Game/default-difficulty repro with the reenter audit enabled.

If the rows confirm the Android/A64 abort still escapes through generated code,
the next design should be a guarded non-throw reenter path that preserves
guest-visible stackpoint, debug, exception, late-bound target, and trampoline
fallback behavior. Keep physical-free diagnostics separate.

## Validation

- `tools/thor/thor_xenia_debug.ps1` parses under Windows PowerShell.
- `git diff --check` passes with the existing CRLF conversion warnings only.
- `tools/thor/thor_build.ps1 -Mode NativeCore` passes.
- `tools/thor/thor_build.ps1 -Mode ApkShell` passes.
