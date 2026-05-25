# Blue Dragon Edge Payload Storage Design

Date: 2026-05-25 19:05 EDT

Branch: `master`

## Goal

Convert the route-clean `f[1]` kill taxonomy for the exact hot edge
`82282490:82282598 -> 82287788` into a broader payload-storage implementation
contract.

This is an offline design/tooling slice only. It does not change generated
behavior, does not run a Thor speed A/B, and does not materialize payload state.

## New Tool

Added:

```text
tools/thor/thor_a64_edge_payload_storage_design.ps1
```

The tool wraps the existing edge-variant storage plan, reuses the latest
route-clean taxonomy log, and joins it with the older state-carrier and fpscr
writeback evidence. It then prints the payload bundle cut and the exact next
counter-only patch contract.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_edge_payload_storage_design.ps1 -EdgeCounterLogPath scratch\thor-debug\20260525-184539-speed-logcat.txt
```

## Result

Source checks:

```text
source_check name=normal_entry_is_singleton status=ok
source_check name=hot_edge_call_seam_exists status=ok
source_check name=current_edge_path_is_normal_fallback_only status=ok
source_check name=f1_scope_and_taxonomy_counters_exist status=ok
source_check name=fpscr_cfg_writeback_lane_exists status=ok
source_check name=edge_payload_storage_audit_skeleton_exists status=missing
```

The missing skeleton is expected. It means the next code slice should add a
default-off counter-only `arm64_blue_dragon_edge_payload_storage_audit` path
before any behavior patch.

Latest edge snapshot:

```text
eligible_calls=710511
normal_fallback_share=100.00%
payload_materializations=0
storage_missing=1
active_f1_reads=2121019
active_f1_reads_per_call=2.99
unknown_call_kills=0
unknown_kills_per_call=0.00
```

Payload bundle cut:

```text
f[1]  = include in counter skeleton, not standalone behavior
fpscr = keep in separate CFG writeback lane before behavior
r[3]  = count-only later after the f[1]/fpscr shape is known
lr    = blocked; keep normal PPC call/return path
```

Storage shape:

```text
caller_local_patchpoint = preferred
edge_side_table         = second, only if lookup is compile-time or outside hot path
normal_entry_replacement = rejected
global_indirection_slot   = rejected
```

## Counter-Only Patch Contract

Next cvar:

```text
arm64_blue_dragon_edge_payload_storage_audit=false
```

Required behavior for the first code slice:

- `payload_materializations_allowed=0`
- `behavior_changed=0`
- normal-entry fallback remains required
- no replacement of `A64Function::machine_code()`
- no change to the global guest-address indirection slot
- no quiet speed A/B until route-clean counter rows exist

Required counters:

- compile: `eligible_edge_compiles`, `variant_codegen_skipped`,
  `storage_missing`, `normal_entry_owned`
- runtime: `eligible_edge_calls`, `normal_entry_fallbacks`,
  `indirection_fallbacks`, `variant_misses`, `payload_materializations`
- payload: `f1_seed_candidates`, `f1_active_reads_covered`,
  `f1_unknown_kills`, `fpscr_seed_candidates`, `fpscr_dirty_writes`,
  `fpscr_required_writebacks`, `r3_seed_candidates`, `r3_mutable_writes`
- flush: `helper_preserved`, `child_preserved`, `return_exit`,
  `unknown_call`, `context_barrier`, `exception_or_trap`,
  `external_visibility`

Likely files:

```text
src/xenia/cpu/backend/a64/a64_backend.h
src/xenia/cpu/backend/a64/a64_backend.cc
src/xenia/cpu/backend/a64/a64_emitter.cc
src/xenia/cpu/backend/a64/a64_sequences.cc
```

Validation before any later behavior patch:

```text
PowerShell parser
NativeCore
FullDeploy
route-clean Thor capture with payload_materializations=0 and behavior_changed=0
```

## Decision

Do not patch generated behavior yet. `f[1]` is clean enough to count as a
payload candidate, but `f[1]` alone already missed speed proof. The likely
speed upside requires a measured multi-field bundle shape while `fpscr` remains
CFG-writeback gated and `r[3]` remains count-only.

The next useful slice is the default-off counter-only
`arm64_blue_dragon_edge_payload_storage_audit` skeleton.
