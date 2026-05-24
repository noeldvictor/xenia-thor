# 8228252C State-Carrier Design Audit

Date: 2026-05-24 13:30 EDT

Branch: `master`

Starting commit: `7c6a2525a`

## Goal

Turn the previous state-carrier design constraints into a deterministic audit
tool that prints the seed, kill, and writeback requirements for the hot
`82282490:8228252C-822825C4` span and direct edge
`82282598 -> 82287788`.

This is still an offline lane. It is deliberately not a behavior patch and did
not need a Thor capture.

## New Tool

`tools/thor/thor_hir_state_carrier_design_audit.ps1`

Inputs:

- parent filtered HIR log and function/span;
- parent call PC;
- callee filtered HIR log and function;
- optional same-run dynamic call-edge log;
- candidate context offsets; and
- preserved helper/child-call hints.

The output joins parent span context traffic, callee boundary/call kinds, callee
context accesses, dynamic edge calls, and per-candidate requirements.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_state_carrier_design_audit.ps1 -ParentLogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -ParentFunction 82282490 -ParentStartGuest 8228252C -ParentEndGuest 822825C4 -ParentCallPc 82282598 -CalleeLogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -CalleeFunction 82287788 -EdgeProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -PreservedCallPcs 82287854,82287ED4 -Top 20
```

## Findings

- The parent span still shows the expected wide state/vector/FPR mix:
  `store_context=27`, `load_context=14`, `mul_add=3`, `extract=9`,
  `splat=6`, and `store.1=3`.
- The dynamic edge remains hot: `8228252C -> 82287788` has
  `calls_total=1691272`, `body_ticks_total=5653971`, and `ticks_per_call=4`.
- The callee has six calls in this filtered path: four GPR/LR helper calls and
  two preserved child calls at `82287854` and `82287ED4`. The path also has
  `labels=93`, `branches=112`, and `context_barriers=52`, so any carrier that
  crosses the callee needs CFG-aware handling.

Candidate table:

- `r[3]`: `mutable_state_cache`, high risk. Parent seed is at `82282550`.
  Callee has `loads=12`, `stores=15`, dynamic upper `20295264` loads and
  `25369080` stores, and writeback pressure before
  `82287854`, `82287ED4`, `82287EDC`, `82287EE4`, and `82288220`.
- `f[1]`: `read_only_entry_seed`, medium risk. Parent seed is at `82282594`.
  Callee has `loads=10`, `stores=0`, dynamic upper `16912720` loads, and no
  writebacks. This remains possible only as a function-pair/caller-seeded
  design, and the previous stack-slot carrier A/B means it is too small as a
  standalone patch.
- `fpscr`: `dirty_cfg_carrier`, very high risk. Parent seed is at `82282594`.
  Callee has `loads=26`, `stores=26`, dynamic upper `43973072` loads and
  `43973072` stores. Required writeback PCs are `82287ED4`, `82287EDC`,
  `82287EE4`, and `82288220`.
- `lr`: `blocked_call_link_state`, high risk. It has loads/stores, but should
  stay on the normal PPC call/return state path and not be forwarded as cache
  state.

## Decision

Do not patch generated behavior yet. The new audit gives the requirements table
we were missing, and it points to a safer next slice:

- add default-off compile-time or runtime counters for the candidate carrier
  plan, not a speed A/B;
- if the first candidate is `f[1]`, count the real dynamic benefit of a
  caller-seeded callee slot while keeping the parent context store visible;
- if the first candidate is `fpscr`, count dirty writeback events at the exact
  call-visible PCs before any caching shortcut.

## Next Experiment

Implement a default-off carrier-design counter/probe for the audited path. It
should report dynamic seed hits, candidate reads, dirty writes, required
writebacks, kills, and fallback reasons for `f[1]` and/or `fpscr` without
changing normal generated-code behavior. Only after that route-clean counter
proof should a one-variable behavior patch be considered.
