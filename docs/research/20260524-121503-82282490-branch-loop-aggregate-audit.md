# 82282490 Branch Loop Aggregate Audit

Date: 2026-05-24 12:15 EDT

Starting HEAD: `ddfef0ce6`

## Goal

Broaden the fresh `822824B8-822824E8` branch-state audit across the nearby
`82282490` entry and `822824B8 <-> 822825F4` loop tail before changing codegen.

This is an offline-only slice. No Thor capture was run and no full-speed proof
marker exists.

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_dynamic_block_slice_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 8 -ContextTop 12
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_branch_state_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 82282490 -EndGuest 822824B8 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 16
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_branch_state_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 822824B8 -EndGuest 822824E8 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 20
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_branch_state_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 822825F4 -EndGuest 82282600 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 16
```

## Dynamic Context

The corrected local-exclusive ranking is still led by higher-traffic spans:

| Rank | Span | Approx exclusive | Notes |
| --- | ---: | ---: | --- |
| 1 | `8228252C-822825C4` | `2876500` | Call-heavy parent span, but still largest local/state/vector/FPR surface. |
| 2 | `822824F0-82282528` | `2021174` | Cleaner vector-load span, but its local-only vector peepholes are closed. |
| 3 | `822825E0-822825F0` | `1232791` | Mostly recursive `82282490` child work. |
| 4 | `822824B8-822824E8` | `1099164` | Branch/CR/GPR loop head. |
| 5 | `822825F4-822825FC` | `1033964` | Loop tail back to `822824B8`. |

The branch loop (`822824B8` plus `822825F4`) totals `2133128` local-exclusive
body ticks, but the details below make it a poor standalone patch target.

## Branch-State Results

| Slice | Body ticks | CR predicate stores | GPR reload opportunities | Decision |
| --- | ---: | ---: | ---: | --- |
| `82282490-822824B8` | `249802` | `1` | `0` | Entry/setup is too small and has no carrier win. |
| `822824B8-822824E8` | `1099164` | `3` | `3` | Only `r[11]`, `r[11]`, `r[31]`, all fallthrough-only. |
| `822825F4-82282600` | `1033964` | `1` | `0` | Loop tail has the same CR predicate-store shape and no reload win. |

The aggregate branch-loop shape is therefore:

- `2382930` ticks if the nearby entry/setup is included.
- `2133128` ticks for just the loop head plus loop tail.
- Four local branch predicates are also written to CR context before a
  `context_barrier` / branch.
- Only three GPR reloads are visible, all in `822824B8-822824E8`, all
  fallthrough-only, and all crossing a branch boundary.

## Decision

Do not implement a branch-loop behavior patch next.

The CR side is the tempting part of this lane, but broad CR store/compare/barrier
fusion is already a known Blue Dragon negative and the values cross visibility
barriers. The GPR side has too little upside: only three fallthrough-only reloads
in the whole loop and none in the entry or loop-tail slices.

This also argues against another Thor run for this exact branch lane. It would be
another narrow probe after `stvewx`, `MUL_ADD_V128`, dead-store suppression, and
`f[1]` carrier experiments all produced route-clean but non-speed-proven results.

## Next

Return to higher-traffic CFG-aware/interprocedural state-carrier work instead of
patching local branch state. The best next report/design should target the
`8228252C-822825C4` surface and the hot `82282490 -> 82287788` state round-trip,
with explicit call, helper, barrier, exit, exception, and alias writeback rules.

If the next slice stays tool-only, prefer a generic dirty-state/carrier audit
that aggregates candidate state across dynamic top spans, rather than another
Blue Dragon-only PC fold.
