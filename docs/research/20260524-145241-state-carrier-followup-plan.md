# 82282490 State-Carrier Follow-up Plan

Date: 2026-05-24 14:52 EDT

Branch: `master`

Starting commit: `9a34369a4`

## Goal

Turn the latest route-clean Thor state-carrier counter row into a deterministic
next-step planner, so the sprint does not fall back to a stale stvewx,
standalone `f[1]`, pair-entry seed, `fpscr`, or three-PC `MUL_ADD_V128`
experiment.

This follows:

- `docs/research/20260524-143436-state-carrier-audit-interval-fix.md`
- `docs/research/20260524-133027-8228252c-state-carrier-design-audit.md`
- `docs/research/20260524-113615-82287788-pair-entry-roi.md`
- `docs/research/20260524-114614-82287788-fpscr-dirty-cache-audit.md`
- `docs/research/20260524-110545-blue-dragon-f1-carrier-fastpath-ab.md`

No Thor runtime capture was needed for this slice because it is an offline
planner over already route-clean Thor evidence.

## New Tool

Added:

```text
tools/thor/thor_hir_state_carrier_followup_plan.ps1
```

The tool reads the latest
`A64 Blue Dragon state-carrier design audit` row and combines it with the
existing static audits. It prints dynamic shares and an explicit
`no_generated_behavior_patch_yet` decision when the evidence only supports
design work.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_state_carrier_followup_plan.ps1 -CounterLogPath scratch\thor-debug\20260524-143436-speed-logcat.txt -StaticAuditPath docs\research\20260524-133027-8228252c-state-carrier-design-audit.md -PairEntryRoiPath docs\research\20260524-113615-82287788-pair-entry-roi.md -F1FastpathAbPath docs\research\20260524-110545-blue-dragon-f1-carrier-fastpath-ab.md -FpscrDirtyCacheAuditPath docs\research\20260524-114614-82287788-fpscr-dirty-cache-audit.md
```

## Result

The planner parsed the final counter row from
`scratch/thor-debug/20260524-143436-speed-logcat.txt`:

```text
f1_seed=258389/1162676
f1_read=773031/3477646
f1_helper_read=387415/1742466
f1_child_read=385616/1735180
f1_child_call=128225/576589
f1_fallback=0/0
fpscr_seed=258389/1162676
fpscr_read=807804/3640919
fpscr_dirty_write=807804/3640919
fpscr_required_writeback=262798/1182090
fpscr_call_kill=650213/2924556
fpscr_fallback=0/0
```

Derived dynamic shape:

```text
f1 total_reads=3477646 helper_share=50.10% child_share=49.90%
f1 child_calls=576589 reads_per_child_call=3.01 fallback_total=0
fpscr total_reads=3640919 dirty_per_read=100.00%
fpscr required_writebacks=1182090 writeback_share=32.47%
fpscr call_kills=2924556 call_kill_share=80.32% fallback_total=0
```

## Interpretation

`f[1]` remains the only medium-risk read-only state lane, and the runtime volume
is real: `3,477,646` reads with zero fallbacks. But the helper/child split is
almost exactly even, and previous evidence says both narrow forms are too small
to justify another immediate behavior patch:

- the stack-slot carrier was route-safe but not a speed win;
- pair-entry seed-only mostly removes one seed context load per `82287788`
  call;
- `pair_incremental_static_upper=1691272` is much smaller than the raw
  `f[1]` read upper.

`fpscr` has slightly larger raw volume but remains the unsafe lane:
`3,640,919` dirty writes, `1,182,090` required writebacks, and `2,924,556`
call kills. The previous dirty-cache audit still reports `strict_same_window=0`,
`cfg_phi_or_stack_carrier_required=24`, and
`external_visibility_flush_required=2`.

## Decision

Do not patch generated behavior from the current evidence.

Blocked immediate experiments:

- standalone `f[1]` stack-slot repeat;
- pair-entry seed-only patch;
- `fpscr` shortcut;
- stvewx lane-fold repeat;
- three-PC `MUL_ADD_V128` repeat.

The next useful patch should be tooling or counters for a broader
`82282490 -> 82287788` parent/callee carrier plan that either combines multiple
safe state lanes or proves exact CFG writeback rules. A quiet speed A/B should
wait until that broader plan has compile-time/runtime audit counters and Thor
route-safety proof.
