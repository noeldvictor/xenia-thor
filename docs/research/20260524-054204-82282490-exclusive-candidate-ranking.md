# 2026-05-24 05:42 EDT - 82282490 Exclusive Candidate Ranking

## Question

After confirming that `82281D28` and the largest `82282490` block totals are
mostly recursive child work, which `82282490` spans are still good local
codegen candidates?

## Tool Change

`tools/thor/thor_hir_dynamic_block_slice_report.ps1` now emits a
`Local Exclusive Candidate Ranking` after the detailed dynamic block slices.
The ranking sorts mapped slices by approximate exclusive body ticks after
subtracting same-run direct child edge body ticks.

This is still attribution guidance, not exact cycle accounting. Its purpose is
to keep the next patch focused on local generated-code work.

## Validation

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_dynamic_block_slice_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 12
```

Inputs:

- Filtered `82282490` OptHIR dump:
  `scratch/thor-debug/20260521-170941-speed-logcat.txt`
- Route-clean same-run block/call-edge profile:
  `scratch/thor-debug/20260524-030450-speed-logcat.txt`

Top ranked local/exclusive candidates:

| Rank | Guest | Span | Approx exclusive | Exclusive share | Note |
| --- | --- | --- | ---: | ---: | --- |
| 1 | `8228252C` | `8228252C-822825C4` | `2876500` | `33.72%` | vector/FPR/state-heavy; still child-call-heavy |
| 2 | `822824F0` | `822824F0-82282528` | `2021174` | `57.72%` | setup/vector-load/state-heavy |
| 3 | `822825E0` | `822825E0-822825F0` | `1232791` | `1.68%` | mostly recursive child body; not first patch |
| 4 | `822824B8` | `822824B8-822824E8` | `1099164` | `100%` | branch/CR classification |
| 5 | `822825F4` | `822825F4-822825FC` | `1033964` | `100%` | linked-list advance/branch |

The ranking keeps `8228252C-822825C4` first, but the previous
`stvewx` lane-fold probe for `82282580/82282584` was route-clean without a
quiet speed win. That means the next experiment should not repeat the same
lane-fold A/B unchanged.

## Decision

Use the ranking before the next `82282490` patch. The best next patch lane is
still `8228252C-822825C4`, but it should target broader local state/vector/FPR
traffic rather than the already-tested narrow stack-lane `stvewx` fold.

If that lane is not ready for a safe default-off patch, the next offline audit
should compare the top two candidates:

- `8228252C-822825C4`: store/load context, constant splat/extract, VMX FP
  multiply-add, and the child edge to `82287788`;
- `822824F0-82282528`: state-heavy setup around `lvlx/lvrx` and the
  `82274DB0` child edge.
