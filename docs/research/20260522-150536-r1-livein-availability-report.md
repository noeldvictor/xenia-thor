# 82282490 r[1] Live-In Availability Report

Date: 2026-05-22 15:05 EDT

Branch: `master`

Starting HEAD: `da1ccdff3`

Source log:
`scratch/thor-debug/20260521-170941-speed-logcat.txt`

Raw report:
`scratch/thor-debug/20260522-150348-r1-livein-availability-report.md`

Tool:
`tools/thor/thor_hir_gpr_livein_report.ps1`

## Question

The `r[1]` load-shape report showed that most loads are first target accesses in
their block and most live in multi-predecessor blocks. This report asks whether
those first loads have all incoming edges leaving `r[1]` clean before the load,
and whether the next runtime patch needs to preserve state across
`context_barrier`.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/thor/thor_hir_gpr_livein_report.ps1 -LogPath scratch/thor-debug/20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Gpr 1 -Top 40
```

## Results

Common totals:

- `blocks=168`
- `edges=267`
- `target_loads=107`
- `target_stores=11`
- `target_alias_stores=0`

Strict helper and barrier flush:

- `replaceable_loads=14`
- `first_target_loads=76`
- `replaceable_first_target_loads=14`
- Target-load block entry status: `14` all-predecessor-clean, `16`
  partial-predecessor-clean, `45` no-clean-predecessor, `1` entry block.
- Not-replaceable reasons: `entry_unknown=32`, `call=31`,
  `context_barrier=30`.

Helper flush with barrier preservation:

- `replaceable_loads=61`
- `first_target_loads=76`
- `replaceable_first_target_loads=56`
- Target-load block entry status: `57` all-predecessor-clean, `5`
  partial-predecessor-clean, `13` no-clean-predecessor, `1` entry block.
- Not-replaceable reasons: `call=34`, `entry_unknown=12`.

## Interpretation

The report says a runtime patch exists, but it is not the old emit-time cache
shape. A strict "flush at every barrier" design only reaches `14` first loads,
which is probably too small to justify a native build/device run. Preserving
clean `r[1]` state across `context_barrier` exposes `56` first loads and `61`
loads total, which is large enough for a guarded experiment.

The patch still needs an explicit value carrier before A64 register allocation.
All-predecessor-clean means the incoming context value is available on every
edge; it does not mean all predecessor values are the same SSA `Value*`. The
implementation must use a safe merge/carrier shape, such as a local slot, edge
materialization, or a real pre-RA state cache, and it must reset on calls,
helpers, exits, exceptions, and exact/aliasing writes.

## Decision

Do not build another post-RA `r[1]` pin/cache.

Next runtime patch should be a default-off, function-filtered, audited
pre-register-allocation `r[1]` live-in probe for `82282490` that:

- preserves clean `r[1]` across `context_barrier` only in this guarded path;
- kills state at calls/helpers, exits, exceptions, volatile context operations,
  and exact/aliasing writes;
- only replaces target loads in blocks where all incoming edges are clean;
- records attempted/replaced/skipped-by-reason counters;
- keeps `arm64_context_promotion_gpr_local_slots`,
  `arm64_context_pinned_gpr_r1`, and
  `arm64_context_pinned_gpr_r1_fallthrough` default-off.
