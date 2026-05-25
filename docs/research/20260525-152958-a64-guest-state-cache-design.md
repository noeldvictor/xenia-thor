# A64 Guest-State Register Cache Design

Date: 2026-05-25 15:29 EDT

## Goal

Convert the mature A64 backend gate and the `82282490 -> 82287788` register
cache gap report into a concrete no-behavior design contract for the next
structural Blue Dragon CPU/JIT slice.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_guest_state_cache_design.ps1
```

## Tool Added

- `tools/thor/thor_a64_guest_state_cache_design.ps1`

The tool joins:

- `82282490` HIR state-span traffic from
  `scratch/thor-debug/20260521-170941-speed-logcat.txt`;
- pre-register-allocation GPR promotion candidates for `82282490`;
- the latest `82282490:82282598 -> 82287788` register-cache gap output;
- source checks around `ContextPromotionPass`, `RegisterAllocationPass`,
  `PPCTranslator`, the A64 emit-time context cache, and fixed A64 host
  registers.

## Key Output

The current repo has no structural guest-state register-cache cvar or skeleton:

```text
structural_cache_cvar_exists=False
```

The target remains heavy state traffic:

```text
accesses gpr=1108 vmx=528 cr=345 fpr=90 lr_ctr=78 fp_vmx_status=24 context_barriers=213
whole_gpr_loads=546 whole_gpr_stores=562 dominated_single_pred_blocks=29 multi_pred_blocks=84
top_state_candidate=+40 r[1] score=220
second_state_candidate=+120 r[11] score=169
edge block=8228252C calls_total=1691272 body_ticks_total=5653971 ticks_per_call=4
```

The source checks explain why the next patch should not be another emit-time
fold:

```text
context_promotion per_block_mem2reg=True
register_allocator per_block=True spills_to_locals=True
a64_emit_time_cache exists=True resets_per_block=True
a64_host_register_contract fixed_x19_x20_x21=True allocatable_gprs_x22_x28=True
helper_call_surface try_emit_helper_call_sites=6
```

## Design Decision

No generated behavior patch from this slice.

Best next audit point is a default-off, no-behavior
`hir_context_promotion_cfg_cache` skeleton at the HIR/context-promotion side of
the pipeline. It can see `LoadContext` / `StoreContext` before A64 emission and
before register allocation inserts local spill loads/stores, but it must count
opportunities first. It must not create cross-block SSA values or elide stores
until CFG dataflow, flush rules, and spill pressure are proven.

The first audit should be intentionally narrow:

```text
cvar=arm64_guest_state_register_cache_audit default=false behavior_change=0
first_fields=clean_int64_gpr_r1_and_r11_only
store_elision_allowed=0
context_load_replacement_allowed=0
payload_materialization_allowed=0
flush_model=reset_on_calls_helpers_branches_labels_returns_traps_volatile_ops_overlapping_context_writes_and_external_context_visibility
```

Required counters:

```text
candidate_loads,candidate_stores,clean_hits_possible,dirty_hits_possible,
miss_no_entry,miss_multi_pred,miss_volatile,miss_overlap,flush_call,
flush_helper,flush_branch,flush_label,flush_return,flush_trap,
flush_external_visibility,estimated_spill_pressure,normal_fallback
```

## Next Slice

Implement only a default-off counter-only
`arm64_guest_state_register_cache_audit` skeleton, preferably in or adjacent to
`ContextPromotionPass`, with function filtering and an `82282490` route-clean
audit before any behavior patch or quiet speed A/B.

Parallel lane remains caller-local / side-table edge-variant payload storage
for `82282490:82282598 -> 82287788`, but the guest-state cache audit is the
cleaner next structural step.
