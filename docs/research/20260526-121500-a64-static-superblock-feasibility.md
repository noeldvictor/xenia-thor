# A64 Static Superblock Feasibility Audit

Date: 2026-05-26 12:15 EDT

## Status

This closes the immediate static-superblock / alternate-entry behavior lane for
the current Blue Dragon wall cluster. It is source/tooling proof only: no
generated behavior changed, no Thor route capture was run, and no speed claim
exists.

## Inputs

New deterministic tool:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_static_superblock_feasibility_audit.ps1
```

Output:

`scratch/thor-debug/20260526-121500-a64-static-superblock-feasibility-audit.txt`

Joined mapped codegen-floor reports:

- `scratch/thor-debug/20260526-080921-82282490-822825e0-codegen-audit.txt`
- `scratch/thor-debug/20260526-054200-82281d28-hot-block-codegen-audit.txt`
- `scratch/thor-debug/20260526-080921-82282490-8228252c-codegen-audit.txt`
- `scratch/thor-debug/20260526-080921-82287788-822877bc-codegen-audit.txt`

Source and closure inputs:

- `src/xenia/cpu/backend/a64/a64_function.h`
- `src/xenia/cpu/backend/a64/a64_function.cc`
- `src/xenia/cpu/backend/a64/a64_emitter.cc`
- `src/xenia/cpu/backend/a64/a64_seq_control.cc`
- `src/xenia/cpu/backend/a64/a64_backend.cc`
- `src/xenia/cpu/backend/a64/a64_backend.h`
- `src/xenia/cpu/function.h`
- `src/xenia/cpu/function.cc`
- `docs/research/20260526-115700-a64-scalar-context-load-store-lowering.md`
- `docs/research/20260526-111500-a64-fast-entry-emitter-planning.md`
- `docs/research/20260526-083000-a64-no-wrap-memory-eligibility-audit.md`
- `docs/research/20260526-092500-a64-nonclosed-gpr-cache-capture.md`
- `docs/research/20260526-012000-vmx128-route-stabilized-counters.md`

## Source Checks

All source checks passed:

- `normal_machine_code_single_entry_present=true`
- `fast_entry_storage_source_only_present=true`
- `fast_entry_codegen_behavior_absent=true`
- `direct_guest_calls_use_normal_machine_code=true`
- `direct_guest_call_payload_is_only_return_address=true`
- `unresolved_targets_resolve_to_machine_code=true`
- `stackpoint_longjmp_visibility_present=true`
- `source_map_debug_exception_visibility_present=true`
- `static_superblock_storage_absent=true`
- `closed_micro_lanes_present=true`

The current backend has one normal generated-code entry through
`A64Function::machine_code()`. A separate fast-entry storage model exists, but
codegen does not use it. Direct guest calls branch to the normal entry and pass
only the guest return address in `x0`. Late-bound targets resolve back to
normal `machine_code()`. Stackpoint/longjmp recovery, source maps,
debug/trap/exception visibility, and breakpoint mapping are all tied to the
normal entry/source-map model.

## Hot Rows

The mapped rows are not a simple static-superblock patch target:

- `82282490:822825E0-822825F0`: `calls=0`, `branches=1`,
  `context_barriers=1`, `cr_stores=3`; blocked by the existing CR
  branch/barrier closure.
- `82281D28:8228233C-82282374`: `calls=2`, `branches=0`,
  `context_barriers=2`, `gpr_stores=11`, `lr_stores=2`, `load_offset=6`;
  blocked by direct guest calls, stackpoint/longjmp visibility, and live
  call-argument state.
- `82282490:8228252C-822825C4`: `calls=1`, `branches=2`,
  `context_barriers=3`, `vmx_ops=15`; closed as the already-audited
  stvewx / three-PC `MUL_ADD_V128` / fpscr local shape.
- `82287788:822877BC-82287864`: `calls=1`, `branches=7`,
  `context_barriers=8`, `cr_stores=21`; blocked by many CR branches and
  guest-visible barriers.

Route totals across these mapped rows:

- `calls=4`
- `branches=10`
- `context_barriers=14`
- `cr_stores=30`
- `gpr_stores=24`
- `vmx_ops=15`

## Decision

Tool decision:

```text
decision=static_superblock_behavior_blocked_pending_ir_level_design
safe_next_patch=run_continual_harness_rerank_or_switch_structural_class
required_contracts=per_guest_pc_source_map;normal_entry_fallback;late_bound_target_fallback;stackpoint_resume;debug_exception_trap_visibility;dirty_flush_for_context_cr_fpscr_gpr_vmx;guest_stack_longjmp_recovery;no_hidden_guest_visible_state
```

A behavior patch would need a real IR-level design, not another one-block
peephole. Required contracts include per-PC source mapping, normal-entry
fallback, late-bound target fallback, stackpoint resume, debug/trap/exception
visibility, dirty flushes for context/CR/FPSCR/GPR/VMX state, guest stack
longjmp recovery, and no hidden guest-visible state.

`tools/thor/thor_a64_route_structural_ranking_audit.ps1` was updated to treat
this note as a closure. Once present, its decision becomes
`body_dominant_a64_micro_lanes_closed_need_refiner_or_new_evidence`.

Do not patch:

- multi-block fusion;
- alternate-entry behavior;
- direct-call fast-entry behavior;
- global indirection rewrites;
- context-barrier fusion;
- CR branch/barrier fusion;
- state payload materialization;
- speed A/B from this source-only lane.

## Next

The body-dominant A64 micro-lanes are now saturated for this route window. Run
the continual harness rerank before another speed micro-audit, switch to a
different structural class with fresh route evidence, or take the separate
Android controller/settings UX lane if playability is the priority. Keep
`-Arm64OffsetMemoryAddressFastpath true` explicitly enabled for future Blue
Dragon speed captures, while leaving the cvar globally default-off.
