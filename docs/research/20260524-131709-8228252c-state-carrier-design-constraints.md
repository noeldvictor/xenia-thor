# 8228252C State-Carrier Design Constraints

Date: 2026-05-24 13:17 EDT

Branch: `master`

Starting commit: `83971c568`

## Goal

Re-check the current best body-backed `82282490` local span after the branch
loop closure, then decide whether the next patch should be a local peephole, a
function-pair carrier, or a broader CFG/interprocedural state-carrier design.

This is an offline slice only. No Thor run was needed because the question was
whether existing HIR/body-time/call-edge evidence is strong enough for a new
behavior patch.

## Commands

```powershell
git status --short --branch
Test-Path scratch\thor-debug\latest-blue-dragon-full-speed-proof.json
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_dynamic_block_slice_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 8 -ContextTop 12
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_interproc_state_roundtrip_audit.ps1 -ParentLogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -ParentFunction 82282490 -ParentCallPc 82282598 -CalleeLogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -CalleeFunction 82287788 -EdgeProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 40
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_function_pair_variant_audit.ps1 -ParentLogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -ParentFunction 82282490 -ParentCallPc 82282598 -CalleeLogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -CalleeFunction 82287788 -Offset 296 -EdgeProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -PreservedCallPcs 82287854,82287ED4 -CompareExistingStackCarrier -Top 40
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_fpscr_dirty_cache_audit.ps1 -LogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -Function 82287788 -EdgeProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -ParentFunction 82282490 -CalleeFunction 82287788 -Top 40
```

## Findings

- `8228252C-822825C4` remains the top local/exclusive target in `82282490`:
  `body_total=8530471`, child edge `8228252C -> 82287788` body
  `5653971`, approximate exclusive `2876500`, and `1691272` child calls.
- The same span still has broad state/vector/FPR traffic:
  `store_context=27`, `load_context=14`, `extract=9`, `splat=6`,
  `stvewx=3`, and `vmaddfp=3`.
- The direct-call state roundtrip is real. Parent `82282490` stores `r[3]`,
  `f[1]`, `fpscr`, and `lr` before or at `82282598 -> 82287788`; callee
  `82287788` loads all four.
- `f[1]` is the only medium-risk read-only candidate: callee loads `10` times
  and stores `0` times, for static upper `16912720` on the hot edge.
- The pair-entry ROI is too small as a standalone patch after the stack-slot
  carrier A/B missed: compared with the existing carrier, a pair-specific entry
  removes only the seed context load per call
  (`pair_incremental_static_upper=1691272`).
- `fpscr` has the largest raw access upper (`loads=26`, `stores=26`,
  dynamic upper `87946144` total accesses), but no strict same-window wins.
  Useful savings require CFG/PHI or stack-carrier state for `24` transitions
  and external visibility writebacks for `2` transitions.
- Required `fpscr` call-visible writeback PCs in `82287788` include
  `82287ED4`, `82287EDC`, `82287EE4`, and `82288220`.
- `r[3]` is mutable in the callee (`loads=12`, `stores=15`), so it needs a
  full dirty/alias/call visibility model.
- `lr` is call-link state (`loads=2`, `stores=5`) and should not be forwarded
  as a general state-cache value.

## Decision

Do not patch generated-code behavior next from a local PC fold, a standalone
`f[1]` pair-entry thunk, or an `fpscr` dirty-cache shortcut.

The next useful artifact should be a deterministic CFG/interprocedural state
carrier design audit for `8228252C-822825C4` and the `82282598 -> 82287788`
edge. It should report exact seed, kill, and writeback requirements before any
new A64 behavior patch.

## Required Design Rules

- Keep parent context visibility for `r[3]`, `f[1]`, `fpscr`, and `lr` until a
  visibility model proves the store can move or be replaced safely.
- Kill or flush carriers at unknown calls, indirect calls, helper calls not
  explicitly whitelisted, context barriers that can expose guest state, exits,
  exceptions, traps, returns, and overlapping context writes.
- For read-only `f[1]`, the only plausible future patch is a function-pair or
  caller-seeded callee-slot design that proves normal `82287788` entry
  semantics stay unchanged.
- For mutable `fpscr`, the first implementation must be an audit or dirty-state
  design with exact writebacks at all call-visible points and all readers, not
  a Blue Dragon-only peephole.
- For `r[3]`, require a broader mutable-state model; do not forward it from
  current evidence.
- For `lr`, do not forward it as a cache candidate.

## Next Experiment

Build or run a CFG/interprocedural state-carrier design audit that joins:

- parent span `82282490:8228252C-822825C4`;
- direct edge `82282598 -> 82287788`;
- callee context access for `f[1]`, `fpscr`, `r[3]`, and `lr`;
- helper/call/branch/barrier/exit boundaries; and
- same-run dynamic edge/body rows.

The audit should print a candidate table with dynamic upper, required seed PC,
kill points, required writeback PCs, and whether the candidate is
`read_only_entry_seed`, `dirty_cfg_carrier`, `mutable_state_cache`, or
`blocked_call_link_state`.

Only after that should a default-off behavior patch be considered.
