# GPR State-Cache Candidate Plan

Date: 2026-05-21 21:18 EDT

## Goal

Use the new working-fast autonomy loop to turn the `82282490` state-span
evidence into a sharper first implementation plan for the next A64 speed patch.

## Evidence

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_state_span_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Top 15
```

The report still shows `82282490` as GPR-heavy: `GPR=1108` context accesses,
with `213` `context_barrier` spans. The new candidate scoring section ranks
the likely first clean-value cache targets:

| GPR | Score | Main signal |
| --- | ---: | --- |
| `r[1]` | 220 | `81` cross-span load-after-load and `14` same-span reloads |
| `r[11]` | 169 | `25` cross-span load-after-store and `26` cross-span load-after-load |
| `r[10]` | 98 | `14` cross-span load-after-store and `10` same-span load-after-store |
| `r[31]` | 92 | `16` cross-span load-after-store and `17` cross-span load-after-load |
| `r[29]` | 78 | `10` cross-span load-after-store and `24` cross-span load-after-load |
| `r[30]` | 71 | `9` cross-span load-after-store and `21` cross-span load-after-load |
| `r[28]` | 69 | `7` cross-span load-after-store and `24` cross-span load-after-load |

## Decision

Extend `tools/thor/thor_hir_state_span_report.ps1` with a `Candidate GPR State
Cache Plan` section. The score intentionally weights cross-span
load-after-store higher than repeated loads because it is the strongest signal
that a store was emitted and then reloaded from `PPCContext` after a barrier or
label split.

The first runtime patch should not be a dirty register cache yet. It should be
a guarded clean-value cache:

- keep only INT64 GPR knowledge;
- do not skip or delay `STORE_CONTEXT`;
- preserve clean knowledge across no-op `context_barrier`;
- reset on calls, branches, labels, helper-expanded instruction ranges,
  volatile ops, and overlapping context writes;
- expose a rollback cvar before using it in the Blue Dragon speed lane.

This is deliberately narrower than the old block-local/fallthrough cache that
found zero useful hits. The new target is barrier preservation for clean GPRs,
with `r[1]`, `r[11]`, `r[10]`, `r[31]`, `r[29]`, `r[30]`, and `r[28]` as the
first audit set.

## Validation

The updated report ran successfully against the existing
`scratch/thor-debug/20260521-170941-speed-logcat.txt` filtered dump. No native
build or Thor deployment was required because this slice only changes offline
analysis tooling and docs.
