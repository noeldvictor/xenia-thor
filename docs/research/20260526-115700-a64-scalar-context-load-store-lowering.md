# A64 Scalar Context Load/Store Lowering Audit

Date: 2026-05-26 11:57 EDT

## Status

This closes the current scalar context load/store behavior lane for the mapped
Blue Dragon wall blocks. It is a source/tooling audit only; no generated
behavior changed and no Thor speed capture was run.

## Inputs

New deterministic tool:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_scalar_context_load_store_lowering_audit.ps1
```

Output:

`scratch/thor-debug/20260526-115700-a64-scalar-context-load-store-lowering-audit.txt`

Joined mapped codegen-floor reports:

- `scratch/thor-debug/20260526-080921-82282490-822825e0-codegen-audit.txt`
- `scratch/thor-debug/20260526-054200-82281d28-hot-block-codegen-audit.txt`
- `scratch/thor-debug/20260526-080921-82287788-822877bc-codegen-audit.txt`

Source and closure inputs:

- `src/xenia/cpu/backend/a64/a64_sequences.cc`
- `src/xenia/cpu/backend/a64/a64_emitter.cc`
- `src/xenia/cpu/backend/a64/a64_emitter.h`
- `docs/research/20260526-075500-a64-offset-address-fastpath-quiet-ab.md`
- `docs/research/20260526-083000-a64-no-wrap-memory-eligibility-audit.md`
- `docs/research/20260526-092500-a64-nonclosed-gpr-cache-capture.md`
- `docs/research/20260526-032000-82281d28-call-setup-audit.md`
- `docs/research/20260526-114200-a64-context-cr-branch-lowering-audit.md`

## Source Checks

All source checks passed:

- `context_reg_is_pinned_x20=true`
- `context_barrier_noop_on_a64=true`
- `load_context_direct_ldr=true`
- `store_context_direct_str=true`
- `zero_store_uses_zero_register=true`
- `context_traffic_audit_present=true`
- `offset_fastpath_route_positive=true`
- `no_wrap_host_pointer_fastmem_closed=true`
- `nonclosed_gpr_cache_closed=true`
- `call_setup_local_elision_closed=true`
- `cr_branch_behavior_closed=true`

The key finding is that `LOAD_CONTEXT` / `STORE_CONTEXT` are already lowered to
direct `x20 + offset` A64 loads/stores, and `CONTEXT_BARRIER` is a no-op on
A64. The visible hot cost in `82281D28:8228233C-82282374` is not a missing
scalar context addressing peephole.

## Hot Rows

`82281D28:8228233C-82282374`:

- `body_ticks_total=44572243`
- `load_context=5`
- `store_context=13`
- `gpr_loads=5`
- `gpr_stores=11`
- `lr_stores=2`
- `load_offset=6`
- `store_offset=1`
- `calls=2`
- `context_barriers=2`
- status:
  `blocked_by_guest_call_abi_and_existing_direct_lowering`

The store side is the same guest-call ABI traffic already proven live into
`0x826BF770` and the recursive `0x82281D28` call. The guest-stack
`LOAD_OFFSET` side is already the path improved by
`arm64_offset_memory_address_fastpath`, which stays route-clean and locally
positive.

The CR-heavy rows remain closed by the prior CR branch audit:

- `82282490:822825E0-822825F0`: CR compare/store/branch shape blocked by the
  known default-off barrier/store-elide crash paths.
- `82287788:822877BC-82287864`: same CR branch closure, with much smaller
  body-time weight in this route sample.

Weighted totals across the three mapped rows:

- `load_context=366663699`
- `store_context=952863691`
- `gpr_stores=630301251`
- `cr_stores=223295538`
- `load_offset=333008190`

## Decision

Tool decision:

```text
decision=close_scalar_context_load_store_behavior_for_current_route
safe_next_patch=lane_switch_or_broader_cfg_static_superblock_design_only
closed_reason=context_load_store_lowering_is_already_direct;hot_82281d28_stores_are_guest_call_live_ins;offset_fastpath_already_handles_guest_stack_loads;no_wrap_and_nonclosed_cache_lanes_are_closed
```

`tools/thor/thor_a64_context_cr_branch_lowering_audit.ps1` was also tightened
to detect this closure and stop advertising scalar context load/store as the
next safe patch. Its safe next patch now becomes
`lane_switch_or_broader_cfg_static_superblock_design_only` when this note is
present.

`tools/thor/thor_a64_route_structural_ranking_audit.ps1` was similarly
tightened to treat CR branch and scalar context load/store as closed once this
note is present. Its decision becomes
`ranked_backend_lanes_closed_need_broader_design_or_lane_switch`.

Do not patch:

- `STORE_CONTEXT` elision;
- `LOAD_CONTEXT` replacement;
- context-barrier fusion;
- CR store elision;
- branch-across-barrier;
- guest-stack handoff;
- fast-entry generated behavior;
- host-pointer immediate fastmem without no-wrap proof;
- speed A/B from this source-only lane.

## Next

Do not continue drilling this same mapped block. The remaining speed work needs
either a broader generated-code contract, such as a true CFG/static-superblock
or alternate-entry design with visibility solved, or a lane switch to a
different structural class backed by fresh route evidence. Keep
`-Arm64OffsetMemoryAddressFastpath true` explicitly enabled for future Blue
Dragon speed captures, while leaving the cvar globally default-off.
