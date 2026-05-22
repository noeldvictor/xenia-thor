# 2026-05-22 15:49 EDT - r[1] Live-In Branch Preserve Fix

## Goal

Explain why the guarded pre-RA `r[1]` live-in probe for Blue Dragon hot
function `82282490` replaced only `16/107` loads even though the offline
barrier-preserving report predicted `56` replaceable first loads.

## Finding

The live-in probe treated all HIR opcodes with `OPCODE_FLAG_VOLATILE` as state
kills. In HIR, `branch_true` and `branch_false` are marked
`OPCODE_FLAG_BRANCH | OPCODE_FLAG_VOLATILE`. That flag is correct for generic
single-block value promotion, but it is too broad for the explicit r1 edge
carrier.

The effect was severe: the probe killed clean `r[1]` state at conditional
branches before successor edges could benefit from it. That matches the
observed mismatch:

- offline barrier-preserving report: `56` replaceable first loads;
- runtime probe before this fix: `loads_replaced=16`,
  `loads_seeded=91`, `call_resets=91`.

## Patch

`IsContextStateKillingInstr` now preserves clean `r[1]` across
`OPCODE_BRANCH_TRUE` and `OPCODE_BRANCH_FALSE` in the live-in probe. Calls,
returns, traps, debug breaks, aliases, optional `context_barrier` kills, and
other volatile operations still kill state.

The audit counters also now carry a historical dirty reason through the rewrite
state. The next capture can distinguish loads skipped after calls, barriers,
aliases, exits, or dirty entry instead of collapsing most misses into
`dirty_entry`.

New audit field:

```text
branch_preserves=<conditional branch opcodes seen by the rewrite pass>
```

## Validation

```powershell
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
```

`NativeCore` completed successfully in `15s`. `git diff --check` reported only
the repository's normal LF-to-CRLF warnings.

## Decision

Keep `arm64_context_promotion_gpr_livein_r1` default-off. This is still a
guarded experiment, not a Blue Dragon speed preset.

Next action: deploy and run an audited route capture with:

```powershell
-Arm64ContextPromotionGprLiveInR1 true
-Arm64ContextPromotionGprLiveInR1Function 0x82282490
-Arm64ContextPromotionGprLiveInR1PreserveBarrier true
-Arm64ContextPromotionGprLiveInR1Audit true
```

Success for the next slice is not FPS yet. It is proving the runtime audit now
gets close to the offline `56` first-load opportunity, with no searched fatal
markers. Only after that should a quiet speed capture judge whether the larger
replacement set improves Blue Dragon.
