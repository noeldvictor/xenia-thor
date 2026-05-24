# A64 Edge Variant Design Audit

Date: 2026-05-24 16:51 EDT

Branch: `master`

Starting commit: `9d24d9307`

## Goal

Turn the state-carrier ABI conclusion from
`docs/research/20260524-163338-a64-state-carrier-abi-audit.md` into a more
specific edge-variant design gate before any generated-code behavior patch.

The target edge is still the hot Blue Dragon path:

- parent function: `82282490`
- call PC: `82282598`
- callee function: `82287788`
- first payload candidate: read-only `f[1]`
- `fpscr`: audit-only until exact CFG writebacks and external flushes are
  proven

This was an offline source audit only. No Thor runtime capture was needed
because no generated behavior changed.

## New Tool

Added:

```text
tools/thor/thor_a64_edge_variant_design_audit.ps1
```

The tool checks the normal entry, direct call, unresolved call, code-cache,
indirection, `A64Function`, and `GuestFunction` contracts that any
edge-specific carrier variant would have to respect.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_edge_variant_design_audit.ps1
```

## Result

All source checks passed:

```text
check name=host_to_guest_entry_passes_guest_return_in_x0 status=ok source=src/xenia/cpu/backend/a64/a64_backend.cc:947 impact=normal host entry cannot repurpose x0 for a state payload
check name=resolve_thunk_preserves_x0_and_jumps_normal_entry status=ok source=src/xenia/cpu/backend/a64/a64_backend.cc:1125 impact=uncompiled direct-call resolution restores guest-return x0 before jumping
check name=resolve_function_returns_normal_machine_code status=ok source=src/xenia/cpu/backend/a64/a64_backend.cc:1310 impact=runtime resolve has no edge-specific variant lookup
check name=compiled_direct_call_uses_normal_machine_code_and_x0_return status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:4344 impact=already-compiled calls have no explicit state-payload argument
check name=compiled_tail_call_uses_normal_machine_code_and_x0_return status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:4355 impact=tail-call behavior must stay normal-entry compatible
check name=unresolved_indirection_key_is_guest_address status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:4379 impact=the indirection table cannot distinguish caller edge variants
check name=unresolved_resolve_path_returns_normal_entry status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:4384 impact=fallback resolution needs normal-entry fallback semantics
check name=call_edge_profile_records_edge_but_not_payload status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:4314 impact=existing call-edge data is useful for counters but not an ABI
check name=emitter_places_one_guest_code_entry status=ok source=src/xenia/cpu/backend/a64/a64_emitter.cc:2701 impact=current emit path installs one normal guest entry
check name=code_cache_indirection_slot_is_guest_address_only status=ok source=src/xenia/cpu/backend/code_cache_base.h:126 impact=do not replace the global 82287788 indirection slot with an edge variant
check name=code_cache_generated_map_points_to_one_guest_function status=ok source=src/xenia/cpu/backend/code_cache_base.h:187 impact=variant code would need explicit source-map/unwind/profiling ownership
check name=a64_function_has_single_machine_code_slot status=ok source=src/xenia/cpu/backend/a64/a64_function.h:70 impact=normal A64Function entry is a singleton
check name=host_call_entry_uses_single_machine_code_slot status=ok source=src/xenia/cpu/backend/a64/a64_function.cc:171 impact=host-side function calls also enter the singleton normal entry
check name=guest_function_interface_exposes_single_machine_code status=ok source=src/xenia/cpu/function.h:111 impact=the shared GuestFunction contract has no variant ABI
check name=prior_state_carrier_abi_audit_blocks_plain_payload status=ok source=docs/research/20260524-163338-a64-state-carrier-abi-audit.md impact=this design audit must not reopen plain register preloading as a behavior patch
```

Derived contract:

```text
normal_entry_singleton=true
indirection_key=guest_address_only
machine_code_slot=single_per_guest_function
resolve_path=normal_machine_code_only
current_call_abi=guest_return_only_in_x0
edge_variant_without_global_entrypoint=caller_local_or_side_table_required
full_call_abi_extension_risk=high
safe_behavior_patch=none
recommended_next_patch=default_off_compile_runtime_counter_only
```

Required counter shape for the next patch:

```text
compile_time_counters=eligible_edge_compiles,normal_function_compiles,variant_codegen_skipped,variant_storage_missing
runtime_counters=eligible_edge_calls,normal_entry_fallbacks,payload_candidate_reads,payload_materializations,child_call_kills,variant_miss_or_unresolved
fallback_rules=normal_entry_context_load_fallback,unresolved_indirection_normal_entry,tail_call_unchanged,host_to_guest_entry_unchanged,source_map_unwind_unchanged
```

## Interpretation

There is no safe path where the caller simply places payload state in a spare
register and the normal `82287788` entry consumes it. The normal entry ABI uses
`x0` for guest return metadata, unresolved calls resolve by guest address to
the normal `machine_code()` pointer, the indirection table is guest-address
keyed, and `A64Function` exposes a single normal machine-code slot.

That means an edge-specific carrier for `82282490:82282598 -> 82287788` needs
one of these shapes:

- caller-local codegen that materializes payload and jumps through a separate
  side-table variant without changing the normal `82287788` entry; or
- an explicit ABI/variant storage extension with normal-entry fallback, unwind
  and source-map ownership, unresolved-call behavior, host-entry behavior, and
  profile ownership all accounted for.

Replacing `82287788`'s normal machine-code pointer, changing the global
indirection slot, or using `x1` as a global payload register would risk normal
entry correctness and is not a safe next patch.

## Decision

Do not patch generated behavior or run a quiet speed A/B from this state-carrier
evidence.

The next useful patch is a default-off compile/runtime counter-only probe for
the exact edge variant shape. It should count eligible edge compiles, eligible
runtime calls, normal-entry fallbacks, payload materializations, child-call
kills, and variant misses before any payload-changing behavior is enabled.
