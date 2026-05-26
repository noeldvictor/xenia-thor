# Continual Rerank After A64 Micro-Lane Closures

Date: 2026-05-26 12:30 EDT

## Status

This is a harness/refiner slice. It changes no emulator behavior, runs no Thor
route capture, and makes no speed claim.

## Trigger

The previous source-only audit,
`docs/research/20260526-121500-a64-static-superblock-feasibility.md`, closed
the immediate static-superblock / alternate-entry behavior lane for the mapped
Blue Dragon wall blocks. `tools/thor/thor_a64_route_structural_ranking_audit.ps1`
now reports:

```text
decision=body_dominant_a64_micro_lanes_closed_need_refiner_or_new_evidence
safe_next_patch=run_continual_harness_rerank_or_switch_structural_class
```

## Window

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_continual_harness_review.ps1 -Mode Window
```

Scratch output:

`scratch/thor-debug/continual-harness-window.md`

The window confirms:

- no full-speed proof marker exists;
- `master` was clean at `0d1edba22`;
- the active prompt was current enough to trigger the refiner;
- the recent trajectory is dominated by A64 micro-lane closures, not a new
  route proof.

## Tool Update

`tools/thor/thor_a64_mature_backend_gap_report.ps1` was stale: it defaulted to
the 2026-05-25 reset/worklog and still ranked structural register cache,
VMX128, helper ABI, and fastmem lanes without applying the 2026-05-26 closures.

The tool now defaults to the current 2026-05-26 worklog and closure notes:

- offset fastpath A/B;
- no-wrap fastmem closure;
- nonclosed GPR cache closure;
- VMX128 route-stabilized closure;
- fast-entry emitter-planning closure;
- scalar context load/store closure;
- static-superblock feasibility closure.

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_mature_backend_gap_report.ps1
```

Report:

`scratch/thor-debug/20260526-123000-a64-mature-backend-rerank.txt`

Key output:

```text
body_dominant_micro_lanes_closed=True
closure_flags fastmem=True nonclosed_cache=True vmx=True fast_entry=True scalar_context=True static_superblock=True
decision=body_dominant_a64_micro_lanes_closed_need_fresh_route_proof_or_non_a64_lane
safe_next_patch=best_current_quiet_route_proof_refresh_or_android_ux
```

## Decision

The next default slice should not be another A64 speed micro-audit. The two
safe next lanes are:

1. **Best-current quiet route proof refresh.** FullDeploy current `master`,
   launch the known Blue Dragon route with `-Arm64OffsetMemoryAddressFastpath
   true`, audit/disassembly/block tracing off, delayed body-time comparators
   `82282490,82281D28,82287788`, and explicit FPS/proof parsing. This is fresh
   evidence, not a behavior patch or A64 micro-audit.
2. **Android controller/settings UX.** Separate playability work if the user
   prioritizes handheld usability. Do not count this as FPS progress.

Reopen A64/VMX/fastmem/helper/GPU behavior only if a new clean route capture
shows a different body-dominant wall outside the closed
`82282490` / `82281D28` / `82287788` micro-lanes.

Keep `-Arm64OffsetMemoryAddressFastpath true` explicit in future Blue Dragon
speed captures, while leaving the cvar globally default-off.

## Validation

- PowerShell parser check passed for
  `tools/thor/thor_a64_mature_backend_gap_report.ps1`.
- Updated mature backend gap report passed and wrote
  `scratch/thor-debug/20260526-123000-a64-mature-backend-rerank.txt`.
- No Thor route capture was run.
