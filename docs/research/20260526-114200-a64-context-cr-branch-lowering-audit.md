# A64 Context / CR Branch Lowering Audit

Date: 2026-05-26 11:42 -04:00
Base commit: `83a6eb66b`
Status: source/tooling audit, no generated behavior changed

## Context

`docs/research/20260526-112900-a64-route-structural-ranking.md` ranked scalar
context / CR traffic as the next source-audit lane. This slice checks whether
the mapped body-dominant blocks expose a safe CR compare/branch behavior patch,
or whether the useful work needs to move to non-CR scalar context traffic.

## Tool

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_context_cr_branch_lowering_audit.ps1
```

Report:

```text
scratch/thor-debug/20260526-114200-a64-context-cr-branch-lowering-audit.txt
```

Inputs:

- `scratch/thor-debug/20260526-080921-82282490-822825e0-codegen-audit.txt`
- `scratch/thor-debug/20260526-054200-82281d28-hot-block-codegen-audit.txt`
- `scratch/thor-debug/20260526-080921-82287788-822877bc-codegen-audit.txt`
- `src/xenia/cpu/backend/a64/a64_sequences.cc`
- `src/xenia/cpu/backend/a64/a64_backend.cc`
- `src/xenia/cpu/backend/a64/a64_emitter.cc`
- `docs/research/20260521-153300-a64-context-cache-cr-branch-negative.md`

## Findings

Source checks all passed:

```text
cr_triplet_selector_present=true
cr_gt_eq_selector_present=true
branch_across_barrier_cvar_present=true
branch_across_barrier_default_false=true
store_elide_cvar_present=true
store_elide_default_false=true
store_elide_marked_unsafe=true
prior_crash_report_present=true
context_traffic_cr_shape_counters_present=true
select_sequence_uses_cr_selectors=true
interleaved_store_order_present=true
```

Mapped block results:

```text
82282490:822825E0-822825F0 compare_count=3 cr_store_count=3 branch_count=1 context_barriers=1 status=blocked_by_barrier_and_prior_crash
82281D28:8228233C-82282374 compare_count=0 cr_store_count=0 gpr_store_count=11 context_barriers=2 status=context_state_not_cr_branch
82287788:822877BC-82287864 compare_count=21 cr_store_count=21 branch_count=7 context_barriers=8 status=blocked_by_barrier_and_prior_crash
```

Weighted totals:

```text
weighted cr_stores=223295538 compares=223295538 branches=74431846 context_barriers=164841634
```

The two CR blocks need branch-across-`CONTEXT_BARRIER` fusion and/or CR-store
elision to become a larger behavior win. Those are exactly the default-off
paths from `docs/research/20260521-153300-a64-context-cache-cr-branch-negative.md`
that crashed or regressed Blue Dragon. The safe existing CR compare-store
peepholes already remain in source and preserve interleaved `cset` / `strb`
ordering.

`82281D28:8228233C-82282374` has no CR branch shape. Its dominant source-audit
surface is scalar GPR/context load-store and call setup.

## Decision

```text
decision=close_cr_branch_behavior_keep_source_audit_only
safe_next_patch=source_audit_scalar_context_load_store_lowering_without_cr_store_elide
blocked_cr_blocks=82282490:822825E0-822825F0;82287788:822877BC-82287864
context_state_blocks=82281D28:8228233C-82282374
```

Do not patch:

- `arm64_cr_compare_branch_across_context_barrier`;
- `arm64_cr_store_elide_for_fused_branch`;
- single-PC barrier fusion;
- speed A/B from this source audit.

Next useful slice is source-only scalar context load/store lowering for
body-dominant blocks, especially `82281D28:8228233C-82282374`, without CR
store elision or barrier-branch fusion.
