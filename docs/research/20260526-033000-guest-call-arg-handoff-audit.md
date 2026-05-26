# Guest-Call Argument Handoff Audit

## Summary

Added `tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1`, a log-backed
audit for direct PPC guest calls. It measures caller-side `r3-r10/lr`
`store_context` traffic before `bl` calls, classifies the callee's first touch
of those fields when callee HIR is available, and reports missing-callee
coverage before any fast-entry ABI design.

This is behavior-neutral tooling. No Thor capture was run, no generated-code
behavior changed, and the audit reports `payload_materializations_allowed=0`
and `behavior_changed=0`.

## Inputs

- Primary route log:
  `scratch/thor-debug/20260526-025401-speed-logcat.txt`
- Extra callee HIR log:
  `scratch/thor-debug/20260519-213553-live-logcat.txt`
- Function filter: `82281D28`
- Phase: `OptHIR`

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_guest_call_arg_handoff_audit.ps1 -LogPath scratch\thor-debug\20260526-025401-speed-logcat.txt -ExtraLogPath scratch\thor-debug\20260519-213553-live-logcat.txt -Function 82281D28 -Phase OptHIR -Top 24
```

## Result

- `parsed_functions=10`
- `selected_functions=1`
- `direct_ppc_bl_calls=81`
- `calls_with_argument_stores=81`
- `argument_store_fields=439`
- `argument_store_bytes=3512`
- `callee_first_load_stores=62`
- `callee_first_load_bytes=496`
- `callee_hir_missing_stores=364`
- `normal_entry_fallback_required=81`
- `classification_counts=callee_hir_missing:364,callee_first_load:62,callee_first_store:13`
- Top fields: `lr=81`, `r[3]=71`, `r[4]=62`, `r[5]=48`,
  `r[10]=46`, `r[8]=38`, `r[9]=36`, `r[7]=29`, `r[6]=28`

Top body-weighted calls:

- `82282370 -> 0x82281D28`: nine setup stores, all classified as
  `callee_first_load` with the current recursive callee HIR.
- `8228234C -> 0x826BF770`: four setup stores, all classified as
  `callee_first_load` with the extra `826BF770` HIR.
- Several other direct calls have nine setup stores each, but their callee HIR
  is missing from the current logs.

## Decision

The audit proves there is broad caller-side argument-store traffic in
`82281D28`, but the current HIR coverage is not enough to design a fast-entry
ABI safely. Most candidate stores are blocked by `callee_hir_missing`.

Do not implement a guest-call argument handoff fast-entry variant from this
single-function sample. The next useful slice is a route-clean or file-backed
HIR capture that includes the missing callees for the direct-call-heavy hot
functions, then rerun this audit to quantify:

- callee-first-load volume,
- callee-first-store blockers,
- indirect/tail/extern blockers,
- normal-entry fallback requirements,
- and stackpoint/exception/debug visibility constraints.

If that broader audit still shows mostly missing or low body-weighted
callee-first-load traffic, close this lane and move to another structural
A64/JIT lane.
