# HIR Block Profile Stamps

## Summary

The `82281D28` block-profile to OptHIR join is still unsafe on existing logs,
but the next capture will now have explicit per-block HIR metadata in the
filtered HIR dump. This is a behavior-neutral tooling patch: it changes debug
dump text only and does not alter generated code.

## Change

- `src/xenia/cpu/hir/hir_builder.cc` now emits one `block_profile` comment at
  the start of each `HIRBuilder::Dump()` block.
- Each stamp includes:
  `ordinal`, `first_source`, `last_source`, `first_guest`, `last_guest`,
  `first_comment`, `last_comment`, `label`, and `hir_instrs`.
- `tools/thor/thor_hir_block_profile_join_audit.ps1` parses those stamps into
  `hir_block_*` fields and treats a profile row as mappable when the runtime
  guest address falls inside the stamped source or guest span.
- The tool still refuses ordinal fallback. Old logs without block stamps remain
  unsafe.

## Validation

Commands run:

```powershell
git diff --check
$tokens=$null; $errors=$null; [System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_hir_block_profile_join_audit.ps1',[ref]$tokens,[ref]$errors)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_block_profile_join_audit.ps1 -LogPath scratch\thor-debug\20260526-021159-speed-logcat.txt -BlockProfileLog scratch\thor-debug\20260526-021159-speed-logcat.txt -Function 82281D28 -Phase OptHIR -ProfileKind Body -Top 1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
```

Results:

- PowerShell parser check passed.
- Old-log audit still reports `join_status=unsafe` with
  `hir_block_metadata_rows=0`, proving the tool did not fake a mapping from
  stale data.
- `NativeCore` passed.

## Decision

Do not patch `82281D28` behavior yet. The next useful slice is FullDeploy plus a
route-clean one-function `82281D28` capture with disassembly and block body-time
enabled, then rerun `tools/thor/thor_hir_block_profile_join_audit.ps1`.

Expected good result:

- `hir_block_metadata_rows > 0`
- `hir_block_mappable_rows > 0`
- active hot row `82281D28:block20 guest=8228233C` maps to
  `hir_block_first_source=8228233C` / `hir_block_last_source=82282370`
- `join_status` is no longer `unsafe`

Only after that should weighted `82281D28` HIR evidence be used for behavior
work.
