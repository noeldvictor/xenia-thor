# A64 Mature Backend Gap Report

Date: 2026-05-25

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_mature_backend_gap_report.ps1
```

Inputs:

- Route/design evidence:
  `docs/research/20260525-134556-82287788-fpscr-cfg-carrier-design.md`
- Worklog evidence: `docs/worklogs/20260525.md`
- Mature backend pattern note:
  `docs/research/20260525-143937-mature-a64-emulator-backend-patterns.md`
- Source checked:
  `src/xenia/cpu/backend/a64/a64_emitter.*`,
  `src/xenia/cpu/backend/a64/a64_sequences.cc`,
  `src/xenia/cpu/backend/a64/a64_seq_memory.cc`,
  `src/xenia/cpu/backend/a64/a64_code_cache.cc`,
  `src/xenia/cpu/compiler/passes/context_promotion_pass.cc`, and
  `src/xenia/cpu/compiler/passes/register_allocation_pass.cc`.

## Summary

The route is still CPU/JIT-heavy (`route_cpu_wall=True`). The current A64
backend has useful foundations, especially fixed x19/x20/x21 host registers for
backend context, PPC context, and membase. The biggest mature-backend gap is not
Vulkan and not another isolated Blue Dragon PC fastpath. It is guest register /
context state management across hot spans and calls, followed by edge-variant
storage/linking for `82282490:82282598 -> 82287788`.

## Tool Output

```text
A64 Mature Backend Gap Report
repo=C:\Users\leanerdesigner\Documents\New project 8\xenia-thor
route_doc=docs\research\20260525-134556-82287788-fpscr-cfg-carrier-design.md
worklog=docs\worklogs\20260525.md
mature_patterns_doc=docs\research\20260525-143937-mature-a64-emulator-backend-patterns.md
route_cpu_wall=True

gap subsystem=static_host_register_contract status=partial evidence=x19_backend_ctx=True; x20_context=True; x21_membase=True; allocatable_gprs=x22-x28_only=True source=src\xenia\cpu\backend\a64\a64_emitter.h:78 next=Keep x19/x20/x21, but audit whether only seven allocatable GPRs plus scratch-heavy helpers are forcing avoidable context spills in 82282490/82287788.
gap subsystem=guest_register_cache status=weak_negative evidence=emit_time_context_cache=True; livein_r1_pass=True; allocator_spills_supported=True; previous r1/cache probes were not speed wins source=src\xenia\cpu\backend\a64\a64_emitter.cc:276 next=Build a structural register-cache report over hot HIR spans before another local store/load fold; candidate should operate before/with register allocation, not as an emit-time one-slot patch.
gap subsystem=helper_abi_and_stackpoints status=partial_hot evidence=direct_call_site=True; stackpoint_push=True; helper_inlining_sites=6; direct_call_abi_guest_return_only=True source=src\xenia\cpu\backend\a64\a64_emitter.cc:4414 next=Audit call-clobber and save/restore pressure for the 82282490 -> 82287788 edge; prefer caller-local/edge-variant storage only after proving payload lifetime and fallbacks.
gap subsystem=block_linking_and_dispatch status=partial_blocked_by_singleton_entry evidence=direct_machine_code_calls=True; resolve_function_path=True; latest edge-variant audits say normal entry and indirection are guest-address singleton paths source=src\xenia\cpu\backend\a64\a64_emitter.cc:4549 next=Return to caller-local or side-table edge-variant storage for 82282490:82282598 -> 82287788 if the maturity report chooses block-linking over fpscr-local caching.
gap subsystem=fastmem_and_fault_fallback status=partial evidence=direct_membase_load_store=True; inline_mmio_flag=True; code_cache_base_only=True source=src\xenia\cpu\backend\a64\a64_seq_memory.cc:406 next=Do not assume memory is the Blue Dragon wall yet; add a hot-route memory/MMIO/fault fallback counter only if body-time points at memory lowering.
gap subsystem=vector_fp_fpscr_lowering status=hot_partial evidence=fpcr_mode_tracking=True; fpscr_cfg_audit=True; latest fpscr rows show millions of loads/stores but behavior patch is blocked by CFG writebacks source=src\xenia\cpu\backend\a64\a64_sequences.cc next=Keep fpscr behavior unchanged until a carrier design proves normal-entry fallback, required writebacks, and payload materialization; no quiet A/B before nonzero intended payload.
gap subsystem=deterministic_backend_harness status=missing_for_mature_changes evidence=offline Thor reports exist, but no focused A64 backend unit harness is tied to register-cache/block-linking/fpscr carrier behavior yet source=docs\research\20260525-143937-mature-a64-emulator-backend-patterns.md next=For structural backend patches, add a compile/offline deterministic test or verifier before the Thor route capture.

ranked_next_lanes:
1. register_cache_gap_report_for_82282490_82287788: largest maturity gap; current emit-time/local-slot probes were negative, so design before code.
2. edge_variant_storage_design_for_82282490_82282598_to_82287788: directly matches hot edge, but needs caller-local/side-table storage and fallback proof.
3. fpscr_cfg_carrier_skeleton: viable only as default-off counter/payload proof; behavior and quiet A/B remain blocked.
4. fastmem_or_vulkan: not next unless fresh captures move the wall away from Main Thread/A64 generated code.
```

## Decision

Next useful slice: build a structural register-cache gap report for the current
`82282490 -> 82287788` route. The report should join HIR context traffic,
current A64 register allocation pressure, spill risk, helper-call clobbers, and
previous negative context-cache/local-slot/pinned-r1 probes.

Do not implement fpscr behavior or run a quiet speed A/B yet. Do not pivot to
broad Vulkan work unless fresh captures move the wall away from Main Thread/A64
generated code.
