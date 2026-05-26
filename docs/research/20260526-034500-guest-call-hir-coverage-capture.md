# Guest-Call HIR Coverage Capture

## Summary

Ran a targeted Blue Dragon Thor route capture to provide broader callee HIR for
the generic guest-call argument handoff audit. This was an evidence-only slice:
no APK rebuild, no generated-code behavior change, and no speed claim.

The capture materially improved coverage for direct-call callees from
`82281D28`, reducing missing callee stores from `364` to `126` and raising
proven `callee_first_load_stores` from `62` to `247`. The dominant
body-weighted call boundaries are still ABI-live, so local store elision stays
closed. The next useful work is a source-level fast-entry feasibility audit,
not another local 82281D28 patch.

## Capture

- Capture prefix: `scratch/thor-debug/20260526-034119-*`
- Commit: `682043bb0`
- APK SHA256:
  `32AB811EF1A77BF57EE93F0E079A5B73FD2FAC15CFD3239A47EA91CA2D315AEC`
- Route: Blue Dragon Disc 1 from the known Thor SD-card path
- Screenshot: `scratch/thor-debug/20260526-034119-screenshot.png`
- Visible evidence: opening sky / dragon-wing frame
- Fatal marker search: clean for Xenia, AndroidRuntime, tombstone, assert,
  backtrace, SIGSEGV, SIGABRT, crash, and ANR
- Final perf: Main Thread `92.3%`, XMA Decoder `42.3%`, GPU Commands `7.6%`
- Final delayed body-time rows:
  - `82282490=33831839`
  - `82281D28=7930867`
  - `82287788=3525919`

Disassembly filter:

```text
82281D28,826BF770,82168610,822750F8,82274DB0,82275C08,82277100,82286BC0,82287788,82198088,822753E0,8227FEE8,8228AC30,8228AEF0
```

## Tooling Fix

The combined primary-plus-extra audit initially double-counted `82281D28`
because extra logs were also eligible as caller functions. Fixed
`tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1` so:

- the primary `-LogPath` supplies selected caller functions and block body-time;
- `-ExtraLogPath` supplies callee HIR lookup only.

Parser validation passed after the fix.

## Audit Result

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -Command "& 'tools\thor\thor_hir_guest_call_arg_handoff_audit.ps1' -LogPath 'scratch\thor-debug\20260526-025401-speed-logcat.txt' -ExtraLogPath @('scratch\thor-debug\20260526-034119-speed-logcat.txt') -Function '82281D28' -Phase OptHIR -Top 30"
```

Summary:

- `parsed_functions=12`
- `selected_functions=1`
- `direct_ppc_bl_calls=81`
- `calls_with_argument_stores=81`
- `argument_store_fields=439`
- `argument_store_bytes=3512`
- `callee_first_load_stores=247`
- `callee_first_load_bytes=1976`
- `callee_hir_missing_stores=126`
- `normal_entry_fallback_required=81`
- `classification_counts=callee_first_load:247,callee_hir_missing:126,callee_first_store:51,callee_absent:15`

Body-weighted top calls remain live-in:

- `82282370 -> 0x82281D28`: nine setup stores, nine callee-first-loads.
- `8228234C -> 0x826BF770`: four setup stores, four callee-first-loads.
- `82281DEC -> 0x82277100`: nine setup stores, nine callee-first-loads.
- `82282218/82282298 -> 0x822750F8`: nine setup stores each, all
  callee-first-loads.
- `8228203C/82281F78/822820BC -> 0x82275C08`: nine setup stores each, all
  callee-first-loads.

Remaining notable missing callees are lower or uncovered in this capture,
including `82276E70`, `82274E38`, `8228AC30`, `8228AEF0`, and restore/save
helper targets.

## Decision

This is now enough evidence to stop treating the guest-call lane as a local
store-elision problem. The route has material parent argument-store traffic and
many callee-first-loads, but any performance path must be a general backend
entry/call ABI design with normal-entry fallback.

Do not patch behavior yet. Next useful slice:

1. Source-audit `A64Emitter::Call`, `A64Function::CallImpl`, function entry
   conventions, stackpoint/longjmp handling, debug/exception visibility, and
   normal-entry fallback.
2. Decide whether a default-off fast-entry eligibility audit can count direct
   guest calls where `r3-r10/lr` could be handed off out-of-band without
   changing normal entry behavior.
3. Only after that, consider a route-clean counter-only runtime skeleton. No
   quiet speed A/B until behavior actually changes.
