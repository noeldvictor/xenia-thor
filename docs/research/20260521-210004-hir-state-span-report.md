# HIR State Span Report

Date: 2026-05-21 21:00 EDT

## Goal

Continue the Blue Dragon full-speed sprint with offline evidence before a risky
A64 state-cache change. The previous HIR report named hot context offsets, but
did not distinguish same-span reuse from cross-span reload churn.

## Change

Added:

```text
tools/thor/thor_hir_state_span_report.ps1
```

The tool parses filtered HIR dumps and reports:

- context access classes;
- hot label/barrier spans by context access count;
- same-span repeated loads;
- same-span load-after-store and store-after-store churn;
- cross-span load-after-store reloads;
- cross-span load-after-load repeated reloads.

It uses the same PPC context offset naming as the hotpath report.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_state_span_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Top 20
```

## Findings

For `82282490` OptHIR:

- `hir_lines=6798`
- `spans=331`
- `context_barriers=213`
- context classes: `GPR=1108`, `VMX=528`, `CR=345`, `FPR=90`,
  `LR/CTR=78`, `FP/VMX_STATUS=24`, `XER=5`

Top hot spans by context access:

- `loc_82282CE4`: `178` context accesses
- `loc_82282C2C#after_barrier_84`: `79`
- `loc_822849B8#after_barrier_205`: `52`
- `loc_82283828`: `50`
- `loc_822836C8`: `49`

Top same-span repeated loads:

- `r[1]`: `14`
- `r[11]`: `10`
- `r[6]`: `5`
- `r[9]`, `r[10]`, `v[0]`, `r[3]`, `r[4]`, `v[12]`, `r[7]`: `4` each

Top same-span load-after-store:

- `v[0]`: `11`
- `r[10]`: `10`
- `v[13]`: `10`
- `v[11]`, `v[12]`, `r[6]`: `8` each
- `r[9]`: `7`
- `r[11]`: `6`

Top cross-span load-after-store:

- `r[11]`: `25`
- `r[31]`: `16`
- `r[10]`: `14`
- `v[126]`: `12`
- `fpscr`: `11`
- `r[1]`: `11`
- `r[3]`: `11`
- `r[29]`: `10`
- `v[125]`: `9`
- `r[30]`: `9`

Top cross-span load-after-load:

- `r[1]`: `81`
- `r[11]`: `26`
- `r[28]`: `24`
- `r[29]`: `24`
- `r[30]`: `21`
- `v[127]`: `19`
- `r[31]`: `17`

## Decision

The next implementation target should not be a broad vector peephole. The data
points to a real GPR state-cache design:

- `r[1]` is stack-pointer reload churn across many spans.
- `r[11]`, `r[10]`, and `r[31]` are the most attractive cross-span
  load-after-store GPRs.
- `r[28]..r[31]` look like loop-carried or callee-saved state in the hot
  opening-scene routine.
- CR6 is still important, but prior CR branch/barrier experiments crashed, so
  CR should follow after the first GPR cache audit.

Any implementation must define flushes at helpers, exits, exceptions, and
aliasing context barriers before touching codegen.

## Status

Tooling/analysis only. No emulator runtime behavior changed and no APK rebuild
is required.
