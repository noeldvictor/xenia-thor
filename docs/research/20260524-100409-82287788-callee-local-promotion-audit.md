# 82287788 Callee-Local Promotion Audit

Date: 2026-05-24 10:04 EDT

## Goal

The previous interprocedural audit proved that `82282490 -> 82287788` carries
live `f[1]` and `fpscr` state. This audit asks the next question before any
behavior patch: can `82287788` itself reuse those context values inside strict
local windows, or would useful reuse require crossing labels, branches,
barriers, or calls?

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_callee_local_promotion_audit.ps1 -LogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -Function 82287788 -Offsets "296,2628" -Top 20
```

## Tool Added

- `tools/thor/thor_hir_callee_local_promotion_audit.ps1`

The tool parses a filtered HIR dump, splits the function into conservative
promotion regions, and counts strict same-region reuse versus cross-boundary
reloads for selected context offsets. The strict policy breaks a local window on:

- labels;
- `context_barrier`;
- calls;
- branches;
- returns.

This keeps the report conservative enough to guide the next codegen experiment
without silently assuming CFG, ABI, helper, or exception behavior.

## Result

Input:

- log: `scratch\thor-debug\20260524-041413-speed-logcat.txt`
- function: `82287788`
- phase: `OptHIR`
- offsets: `296:f[1]`, `2628:fpscr`

Function shape:

- labels: `93`
- context barriers: `52`
- calls: `6`
- branches: `112`

Target summary:

- `f[1]`: `loads=10`, `stores=0`, `regions=10`
- `f[1]` strict same-region wins: `0`
- `f[1]` cross-boundary reloads: `9`
- `f[1]` risky call crossings: `2`
- `fpscr`: `loads=26`, `stores=26`, `regions=26`
- `fpscr` strict same-region wins: `0`
- `fpscr` cross-boundary reloads after store: `25`
- `fpscr` risky call crossings: `1`

The strict local-cache result is intentionally disappointing: the useful reuse
does not live inside small straight-line windows. It is mostly across branch,
label, barrier, and some call boundaries.

## Decision

Do not implement a blind callee-local context cache for `82287788`.

`f[1]` is read-only inside the callee, so it is still the best candidate, but
the useful replacements require a CFG/function-pair carrier model rather than a
simple same-block peephole. Because some reloads cross calls, the next step must
prove call-clobber behavior before keeping a carried value alive.

`fpscr` is high risk. The callee repeatedly loads and stores it, so any useful
optimization must forward exact updates and preserve guest-visible FP status
semantics. Treat `fpscr` as mutable until a source review proves a precise dirty
flush/update model.

## Next

Prefer a default-off audit/probe for `f[1]` carrier safety across the exact
`82282490 -> 82287788` boundary. It should count or prove:

- whether reloads after guest/helper calls can legally use the entry `f[1]`;
- where carrier state must be flushed on calls, exits, exceptions, barriers, and
  aliasing context writes;
- whether the route produces enough safe replacements to justify a quiet Thor
  speed A/B.

Do not patch `fpscr` or broad callee-local state caching first.
