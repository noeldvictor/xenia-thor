# A64 Fast-Entry Dirty-Flush Protocol Audit

Date: 2026-05-26 09:45 local

## Context

The fast-entry lane was reopened only as behavior-neutral source work after
`docs/research/20260526-093500-a64-fast-entry-data-model-rerank.md`.
`A64Function` has separate guest-call fast-entry storage plus a payload and
dirty-flush contract, but generated behavior remains absent. This slice checks
whether the source data model is sufficient to define the next protocol step
without touching direct callsites, normal entry, or global indirection.

No Thor route capture or speed A/B was run.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_fast_entry_dirty_flush_protocol_audit.ps1 |
  Tee-Object -FilePath scratch\thor-debug\20260526-094500-a64-fast-entry-dirty-flush-protocol-audit.txt
```

## Findings

- Payload metadata covers all intended direct-call payload slots:
  `r3,r4,r5,r6,r7,r8,r9,r10,lr` (`9/9`).
- Dirty-flush metadata covers the required boundary classes:
  `context_barrier`, `helper_call`, `host_call`, `debug_trap`, `tail_call`,
  `return`, `exception`, and `unresolved_target` (`8/8`).
- `A64Function` has separate fast-entry code storage and contract storage, and
  the latest rerank report agrees with source state.
- Normal entry remains unchanged: it still receives only the guest PPC return
  address in `x0`.
- Direct compiled calls still branch to `fn->machine_code()` and do not test or
  consume `guest_call_fast_entry_code()` / contract metadata.
- Indirection and resolve paths still target normal entry.
- Generated behavior remains absent: no payload population codegen, no dirty
  flush codegen, no fast-entry stub codegen, no direct-call guard, and no
  fast-entry stackpoint resume protocol.

Prior runtime blockers from
`docs/research/20260526-053600-a64-guest-call-fast-entry-target-row-capture.md`
still matter:

- `unresolved_direct_targets=52`
- `normal_entry_fallback=67`
- `stackpoint_sensitive=67`
- `dirty_flush_points=268`
- `flush_context_barrier=260`
- `parent_pre_call_flush_points=195`
- `callee_first_use_known=0`

## Decision

The current data model is sufficient for more source/protocol work, but still
blocks generated behavior.

Safe next patch:

- default-off stub skeleton or no-op codegen protocol helpers only;
- keep normal `machine_code()`, global indirection, and direct-call behavior
  unchanged;
- define payload population, dirty flush to `PPCContext`, unresolved/indirect/
  extern/tail/debug fallback, and stackpoint resume invalidation before any
  callsite behavior.

Do not patch direct callsites, replace `A64Function::machine_code`, rewrite
global indirection, materialize payload state, or run a quiet speed A/B from
this evidence.
