# A64 Edge-Variant Storage Plan

Date: 2026-05-24 18:26 EDT

Branch: `master`

## Goal

Turn the route-clean edge-variant counter probe into an explicit implementation
contract for the hot Blue Dragon edge `82282490:82282598 -> 82287788`.

This slice is offline-only. No Thor runtime capture was needed because it adds
deterministic planning/tooling and does not change generated behavior.

## New Tool

Added:

```text
tools/thor/thor_a64_edge_variant_storage_plan.ps1
```

The tool joins:

- the latest `A64 Blue Dragon edge-variant audit` counter row;
- the singleton-entry / indirection source audit;
- the interprocedural `f[1]` / `fpscr` roundtrip evidence;
- the `fpscr` dirty-cache writeback audit.

It then prints source-contract checks, dynamic edge shape, storage options, and
the exact rules that must exist before any behavior patch.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_edge_variant_storage_plan.ps1 -EdgeCounterLogPath scratch\thor-debug\20260524-180737-speed-logcat.txt
```

## Result

All source-contract checks passed:

```text
source_check name=normal_entry_is_singleton status=ok
source_check name=indirection_key_is_guest_address_only status=ok
source_check name=direct_call_uses_normal_machine_code status=ok
source_check name=edge_probe_is_counter_only status=ok
source_check name=design_audit_requires_variant_storage status=ok
```

The final edge-counter row remains:

```text
eligible_compiles=0/1 variant_storage_missing=0/1
eligible_calls=240752/675279 payload_materializations=0/0
normal_entry_fallbacks=240752/675279
indirection_fallbacks=240752/675279
call_kills=605393/1695703 variant_misses=240752/675279
```

Derived dynamic edge shape:

```text
eligible_calls=675279
normal_fallback_share=100.00%
indirection_fallbacks=675279
variant_misses=675279
payload_materializations=0
storage_missing=1
call_kills=1695703
call_kills_per_call=2.51
```

Payload state:

```text
payload field=f[1] class=read_only_entry_seed
dynamic_reads=3477646 helper_share=50.10% child_share=49.90%
fallbacks=0 status=possible_but_not_a_standalone_speed_patch

payload field=fpscr class=dirty_cfg_carrier
dynamic_reads=3640919 dirty_writes=3640919
required_writebacks=1182090 call_kills=2924556
call_flush_pcs=82287ED4:1,82287EDC:1,82287EE4:1,82288220:1
status=blocked_until_cfg_writeback_model
```

## Interpretation

The edge is real and hot, but the current counter patch only proves the missing
storage problem: every eligible call still falls through the normal entry /
guest-address indirection path, with zero payload materializations.

The correct storage direction is now explicit:

- best first: a caller-local variant patchpoint for only
  `82282490:82282598 -> 82287788`;
- second: an edge side table only if lookup is compile-time or outside the hot
  call path;
- rejected: replacing `82287788`'s normal machine-code pointer;
- rejected: changing the global indirection slot.

`f[1]` is still the only plausible first payload because it is read-only in the
callee and has zero fallbacks, but the previous stack-slot fastpath already
showed the narrow form is not enough. `fpscr` remains blocked until a CFG-aware
dirty writeback model proves exact flush points for calls, exits, exceptions,
context barriers that expose `PPCContext`, and external readers.

## Decision

Do not patch generated behavior yet, and do not run a quiet speed A/B from the
current edge-counter patch.

Next useful patch:

- `caller-local_or_side-table_skeleton_counter_only`; or
- `cfg_fpscr_writeback_audit_no_behavior_change`.

Keep `arm64_blue_dragon_edge_variant_audit`,
`arm64_blue_dragon_f1_carrier_fastpath`,
`arm64_blue_dragon_mul_add_v128_fastpath`,
`arm64_blue_dragon_stvewx_stack_lane_fastpath`, broad GPR caches, CR
fusion/elision, broad VMX-dot, local-only `822824F0` peepholes, and
`a64_enable_host_guest_stack_synchronization` changes default-off outside
targeted captures.
