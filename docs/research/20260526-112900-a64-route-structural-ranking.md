# A64 Route Structural Ranking Audit

Date: 2026-05-26 11:29 -04:00
Base commit: `bd795aba0`
Status: compile-time/source audit, no generated behavior changed

## Context

The fast-entry source-only chain is closed for now by
`docs/research/20260526-111500-a64-fast-entry-emitter-planning.md`: the source
has a data model, guard/protocol helpers, and attachment-point map, but
generated behavior still lacks guard emission, payload population, dirty flush
codegen, late-bound fallback handling, stackpoint resume, and debug/exception
visibility.

This slice adds a deterministic route-ranking audit to pick a different
body-dominant A64 backend class before another risky Thor capture.

## Tool

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_route_structural_ranking_audit.ps1
```

Report:

```text
scratch/thor-debug/20260526-112900-a64-route-structural-ranking-audit.txt
```

Inputs:

- `scratch/thor-debug/20260526-080921-82282490-822825e0-codegen-audit.txt`
- `scratch/thor-debug/20260526-054200-82281d28-hot-block-codegen-audit.txt`
- `scratch/thor-debug/20260526-080921-82282490-8228252c-codegen-audit.txt`
- `scratch/thor-debug/20260526-080921-82287788-822877bc-codegen-audit.txt`
- `docs/research/20260526-083000-a64-no-wrap-memory-eligibility-audit.md`
- `docs/research/20260526-092500-a64-nonclosed-gpr-cache-capture.md`
- `docs/research/20260526-012000-vmx128-route-stabilized-counters.md`
- `docs/research/20260526-111500-a64-fast-entry-emitter-planning.md`

## Result

The audit ranks weighted codegen-floor classes across the mapped wall blocks:

```text
audit=a64_route_structural_ranking
input_blocks=4 total_body_ticks=126880028 total_estimated_floor=367
rank class=context_state body_weighted_floor=1953705181 floor_total=112 status=open_source_audit_only
rank class=vmx_vector body_weighted_floor=1283823333 floor_total=83 status=closed_behavior
rank class=memory_addressing body_weighted_floor=1103606362 floor_total=68 status=closed_behavior
rank class=scalar_dataflow body_weighted_floor=719565533 floor_total=56 status=open_source_audit
rank class=scalar_cr_branch body_weighted_floor=421469392 floor_total=40 status=open_source_audit_only
route_flags fastmem_behavior_closed=true nonclosed_cache_closed=true vmx_behavior_closed=true fast_entry_closed=true
weighted_context_barriers=211244887 weighted_cr_stores=316102044 weighted_gpr_stores=692172255
decision=rank_context_cr_scalar_state_source_audit_next
```

Candidate blocks:

```text
82282490:822825E0-822825F0
82281D28:8228233C-82282374
82287788:822877BC-82287864
```

The audit intentionally marks these as source-audit candidates only. It does
not justify a behavior patch, Thor route capture, or speed A/B by itself.

## Decision

Next lane:

```text
source_audit_a64_context_cr_compare_branch_lowering_for_body_dominant_blocks
```

Do not patch:

- fast-entry behavior;
- host-pointer plus immediate fastmem without no-wrap proof;
- closed VMX128 shapes;
- nonclosed GPR cache behavior;
- single-PC barrier fusion;
- speed A/B from this compile-time audit.

The next useful slice is a source/codegen audit of scalar `PPCContext` traffic,
CR compare/branch lowering, and context-barrier costs in the candidate blocks.
Patch behavior only if that audit proves a reusable backend rule with explicit
fallback/correctness constraints.
