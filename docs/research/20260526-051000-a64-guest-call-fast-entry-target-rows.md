# 2026-05-26 - A64 Guest-Call Fast-Entry Target Rows

## Context

The previous callee-aware offline audit showed real known live-in traffic for
`0x82281D28`, but behavior was still blocked because the runtime compile audit
only emitted a function-level summary:

- `callee_first_use_known=0`
- `dirty_flush_points=268`
- `flush_context_barrier=260`
- `normal_entry=unchanged`
- `global_indirection=unchanged`

This slice adds runtime per-target rows to the existing default-off
`arm64_guest_call_fast_entry_audit` path. It does not emit alternate code and
does not change generated behavior.

## Patch

Updated `src/xenia/cpu/backend/a64/a64_emitter.cc`:

- Adds bounded `A64 guest-call fast-entry target` rows, sorted by parent
  `arg_store_fields`.
- Each target row includes:
  - `target`
  - `calls`, `regular`, `conditional`, `tail`
  - `eligible_regular`
  - `already_compiled`, `unresolved`
  - `helper_blockers`
  - `normal_entry_fallback`
  - `stackpoint_sensitive`
  - `arg_store_calls`, `arg_store_fields`, `arg_store_bytes`
  - `parent_pre_call_flush_points`
  - `first_call`, `first_block`
  - `arg_store_top`
- Keeps behavior-neutral markers:
  - `payload_materializations_allowed=0`
  - `behavior_changed=0`
  - `alternate_codegen=0`
  - `normal_entry=unchanged`
  - `global_indirection=unchanged`
- Adds `target_rows` and `parent_pre_call_flush_points` to the existing
  function-level summary row.

Updated `tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1`:

- Parses future runtime target rows from `-FastEntryAuditLogPath`.
- Emits a `Fast-Entry Runtime Target Rows` section when such rows are present.

## Validation

PowerShell parser:

```powershell
$tokens=$null; $errors=$null; $null=[System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_hir_guest_call_arg_handoff_audit.ps1',[ref]$tokens,[ref]$errors); if ($errors -and $errors.Count -gt 0) { $errors | Format-List | Out-String; exit 1 }; 'guest-call-parser-ok'
```

Result: `guest-call-parser-ok`.

Whitespace:

```powershell
git diff --check
```

Result: passed with existing CRLF warnings only.

Native build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
```

Result: `BUILD SUCCESSFUL`.

Parser smoke on old logs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_guest_call_arg_handoff_audit.ps1 -LogPath scratch\thor-debug\20260526-025401-speed-logcat.txt -ExtraLogPath scratch\thor-debug\20260526-034119-speed-logcat.txt -FastEntryAuditLogPath scratch\thor-debug\20260526-043446-speed-logcat.txt -Function 82281D28 -Phase OptHIR -Top 4
```

Result: existing logs still parse. Old logs naturally contain no runtime target
rows because this patch had not been deployed yet.

## Decision

Do not implement guest-call fast-entry behavior and do not run a quiet speed A/B
from this patch. This is a default-off counter/logging surface only.

Next useful slice: FullDeploy and run a route-clean capture with
`-Arm64GuestCallFastEntryAudit true`,
`-Arm64GuestCallFastEntryAuditFunction 0x82281D28`, and a budget high enough to
emit the function summary plus target rows. Then join the target rows with the
offline callee-aware HIR audit. Close the fast-entry behavior lane if the target
rows do not show broad known first-use traffic with manageable flush pressure.
