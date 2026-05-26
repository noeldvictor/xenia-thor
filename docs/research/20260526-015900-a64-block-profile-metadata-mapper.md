# A64 Block-Profile Metadata Mapper

Date: 2026-05-26
Branch: `master`

## Problem

`docs/research/20260526-014000-82281d28-hir-block-profile-join.md` showed that
the old `82281D28` block-profile/HIR join was unsafe. Runtime block rows used
`FindBlockGuestAddress`, which can report a hidden `SOURCE_OFFSET` such as
`8228233C`, while printed OptHIR labels/comments start later, for example at
`822823B0` or `loc_82282490`.

That made ordinal fallback actively dangerous: block `20` in the runtime
profile mapped to a different printed HIR label when joined by index.

## Change

Added runtime block metadata to A64 speed-profile rows:

- `first_source`
- `last_source`
- `first_guest`
- `last_guest`
- `first_comment`
- `last_comment`
- `label`
- `hir_instrs`

Source changes:

- `src/xenia/cpu/backend/a64/a64_function.h`
- `src/xenia/cpu/backend/a64/a64_function.cc`
- `src/xenia/cpu/backend/a64/a64_emitter.h`
- `src/xenia/cpu/backend/a64/a64_emitter.cc`
- `src/xenia/cpu/backend/a64/a64_backend.cc`

Implementation details:

- `A64ProfileBlockMetadata` stores per-profiled-block source/comment/label
  metadata beside the existing block counters.
- `A64Emitter::CollectBlockProfileMetadata` records the first and last hidden
  `SOURCE_OFFSET`, first and last guest address seen by normal HIR ops, first
  and last PPC comment address, first `loc_XXXXXXXX` label address if present,
  and non-hidden HIR instruction count.
- Block profile and block body-time log rows now append the metadata without
  changing existing row prefixes, so old regexes that stop at `total` or
  `ticks_per_entry` still work.

## Tooling Guard

Updated:

- `tools/thor/thor_hir_block_profile_join_audit.ps1`
- `tools/thor/thor_hir_vector_shape_report.ps1`
- `tools/thor/thor_hir_block_mix_report.ps1`
- `tools/thor/thor_hir_call_path_report.ps1`

The join audit now parses the new metadata fields when present. The vector,
block-mix, and call-path reports now refuse ordinal fallback by default and
report:

- `profile_guest_matches`
- `profile_ordinal_fallbacks`
- `profile_unmatched`
- `profile_allow_ordinal_fallback`

`-AllowOrdinalFallback` exists only for explicit legacy comparison. Do not use
it for behavior decisions unless a separate join audit proves the fallback safe.

## Validation

PowerShell parser validation passed for:

- `tools/thor/thor_hir_block_profile_join_audit.ps1`
- `tools/thor/thor_hir_vector_shape_report.ps1`
- `tools/thor/thor_hir_block_mix_report.ps1`
- `tools/thor/thor_hir_call_path_report.ps1`

Old-log behavior was checked against
`scratch/thor-debug/20260526-013226-speed-logcat.txt`:

- `thor_hir_block_profile_join_audit.ps1` still reports
  `join_status=unsafe`, `metadata_rows=0`, and `metadata_mappable_rows=0`.
- `thor_hir_vector_shape_report.ps1` now reports `profile_unmatched=154`,
  `profile_ordinal_fallbacks=0`, and zero weighted counters for that old log
  instead of producing fake ordinal-joined dynamic rows.
- `thor_hir_block_mix_report.ps1` and `thor_hir_call_path_report.ps1` now
  report `profile_unmatched=242` and no ordinal fallback rows for that old log.

Native build:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
```

Result: `BUILD SUCCESSFUL in 15s`.

## Decision

This is behavior-neutral profiling/tooling. It does not prove speed and does
not justify a behavior patch by itself.

The next useful slice is FullDeploy plus a route-clean `82281D28` one-function
capture with block body-time and disassembly enabled, then run the join audit
on the new log. Expected good outcome: `metadata_rows > 0` and top
`82281D28` rows become `metadata_mappable` through `first_comment`, `label`, or
another explicit metadata field instead of ordinal fallback.

Do not run a quiet speed A/B from this patch.
