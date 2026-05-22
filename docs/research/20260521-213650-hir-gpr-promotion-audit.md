# HIR GPR Promotion Audit

Date: 2026-05-21 21:36 EDT

Branch: `master`

Starting HEAD: `b0a4d4c74`

## Goal

Stop retrying emit-time context-value cache preservation and identify the next
safe pre-register-allocation path for reducing Blue Dragon `82282490` PPC GPR
state traffic on AYN Thor.

## Inputs

- Baseline filtered HIR log:
  `scratch/thor-debug/20260521-170941-speed-logcat.txt`
- Function: `82282490`
- Phase: `OptHIR`
- Previous negative result:
  `docs/research/20260521-212700-a64-gpr-cache-barrier-negative.md`

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_gpr_promotion_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Top 20
```

## Findings

The new audit parses filtered HIR function dumps and ranks first whole-GPR
loads in dominated single-predecessor HIR blocks. It also prints the important
compiler gate: `ContextPromotionPass` currently runs before register allocation,
but the PPC translator does not run `DataFlowAnalysisPass`, so a naive
cross-block `ASSIGN` to a value defined in another block is not a safe patch.
The next implementation must either lower those incoming values through locals
or add a guarded data-flow/local-slot stage before `RegisterAllocationPass`.

`82282490` OptHIR summary from the audit:

- `hir_lines=6798`
- `blocks=118`
- `context_accesses=2178`
- `whole_gpr_loads=546`
- `whole_gpr_stores=562`
- `dominated_single_pred_blocks=29`
- `multi_pred_blocks=84`
- `first_gpr_loads_in_dominated_single_pred_blocks=61`

Top dominated first-GPR-load candidates:

- `r[1]`: `score=32`, `first_loads=14`, `after_store=4`, `after_load=10`
- `r[3]`: `score=14`, `first_loads=5`, `after_store=4`, `after_load=1`
- `r[11]`: `score=13`, `first_loads=5`, `after_store=3`, `after_load=2`
- `r[4]`: `score=12`, `first_loads=4`, `after_store=4`, `after_load=0`
- `r[10]`: `score=11`, `first_loads=4`, `after_store=3`, `after_load=1`

This lines up with the prior state-span report: `r[1]` is still the first
target, and `r[11]`/`r[10]` remain the best load-after-store targets.

## Decision

Do not implement cross-block context promotion by directly wiring predecessor
`Value*` objects into successor HIR blocks. That would skip the local-slot
lowering the current pass pipeline needs.

The next runtime patch should be one of these, in this order:

1. Add a guarded pre-RA GPR promotion experiment that materializes incoming
   whole-GPR values through HIR locals before successor loads are replaced.
2. Restrict the first patch to `r[1]` and `r[11]`.
3. Reset candidate state at volatile ops, calls, conditional branches, returns,
   traps, multi-predecessor joins, and overlapping context writes.
4. Do not elide stores in the first patch; replace loads only.

## Status

`succeeded` as an offline OODA artifact. No Thor capture was needed because no
runtime code changed in this slice.
