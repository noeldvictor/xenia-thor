# 82282490 r[1] Load Shape Report

Date: 2026-05-22 12:52 EDT

Branch: `master`

Starting HEAD: `bdf7ee45d`

Source log:
`scratch/thor-debug/20260521-170941-speed-logcat.txt`

Raw report:
`scratch/thor-debug/20260522-125206-r1-load-shape-report.md`

Tool:
`tools/thor/thor_hir_gpr_load_shape_report.ps1`

## Question

The local-slot promotion and pinned `x29`/`r[1]` cache both missed. This report
classifies the exact `82282490` PPC `r[1]` context loads by HIR block shape,
predecessor shape, branch/call/barrier position, and aliasing stores before
attempting another state-cache patch.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/thor/thor_hir_gpr_load_shape_report.ps1 -LogPath scratch/thor-debug/20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Gpr 1 -Top 25
```

## Results

- `target_loads=107`
- `target_stores=11`
- `target_alias_stores=0`
- `blocks_with_target_loads=76`
- `first_target_loads_in_blocks=76`
- `loads_after_global_load=95`
- `loads_after_global_store=11`
- `loads_in_single_dominating_pred_blocks=19`
- `loads_in_multi_pred_blocks=87`
- `loads_in_self_pred_blocks=8`
- `loads_after_barrier_before_branch=31`
- `loads_after_any_branch_in_block=50`
- `loads_after_any_call_in_block=34`
- `loads_with_alias_store_since_previous_target_load=11`

Top repeated target-load blocks:

- `loc_82282F28`: 4 loads, `in=2 out=4`
- `loc_82283640`: 4 loads, `in=3 out=3`
- `loc_822837A8`: 4 loads, `in=1 out=2`
- `loc_822847E8`: 4 loads, `in=4 out=3`

## Interpretation

The report explains the zero-hit pinned `r[1]` experiment. Most exact loads are
first target accesses in their local block, and most are in multi-predecessor
blocks. A post-register-allocation fallthrough cache cannot prove the incoming
value and must reset at the same joins/branches that dominate the hot path.

The data does not mean `r[1]` is a bad target. It means the reusable state, if
safe, must be proven before A64 register allocation with CFG-aware availability
or a real GPR state-cache pass. The lack of overlapping alias stores is useful:
the hard killers are exact `r[1]` stores, calls/helpers, branches/joins, and
context barriers rather than sub-GPR aliasing.

## Decision

Keep these default-off and do not tune them next:

- `arm64_context_promotion_gpr_local_slots`
- `arm64_context_pinned_gpr_r1`
- `arm64_context_pinned_gpr_r1_fallthrough`

Next useful slice: add a CFG/live-in availability report for `82282490` `r[1]`
that proves which target-load blocks have all predecessors leaving `r[1]`
clean, and where exact stores, calls/helpers, returns, exceptions, barriers, or
volatile context operations force a flush. Only implement a pre-register
allocation GPR live-in/cache path after that report shows a bounded,
control-flow-safe replacement set.
