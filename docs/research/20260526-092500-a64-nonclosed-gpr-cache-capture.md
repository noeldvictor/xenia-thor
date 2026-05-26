# A64 Non-Closed GPR Cache Audit Capture

Date: 2026-05-26 09:25 EDT

## Capture

- Commit: `a3bb4442e`
- APK SHA256:
  `75E498D1038C5E1CFD45FC4036182C51EF4506E35FC8DC91EF1DFFFBCBB8D109`
- Capture prefix: `scratch/thor-debug/20260526-091832-*`
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`
- Cvars of interest:
  - `arm64_offset_memory_address_fastpath=true`
  - `arm64_guest_state_nonclosed_cache_audit=true`
  - `arm64_guest_state_nonclosed_cache_audit_function=0x82281D28`
  - delayed body-time filter `82282490,82281D28,82287788`

The screenshot reached the visible opening sky / dragon-wing frame. Focused
fatal-marker search found no AndroidRuntime, tombstone, assert, backtrace,
fatal exception, SIGSEGV, SIGABRT, crash, or ANR rows.

## Result

The route stayed CPU/JIT-bound:

- Main Thread: `100%`
- GPU Commands: `7.6%`
- XMA Decoder: `7.6%`
- Draw Thread: `0.0%`

Final delayed body-time rows:

- `82282490`: `28864797`, code size `86452`
- `82281D28`: `7161895`, code size `95724`
- `82287788`: `3187095`, code size `35712`

## Audit Rows

`0x82281D28` post-promotion non-closed GPR cache audit:

- `blocks=188`
- `labeled_blocks=186`
- `multi_pred_blocks=134`
- `candidate_loads=181`
- `candidate_stores=75`
- `clean_hits_possible=0`
- `dirty_hits_possible=0`
- `normal_fallback=256`
- `peak_live_slots=5`
- `estimated_spill_pressure=3`

Miss / flush shape:

- `miss_multi_pred=65`
- `miss_after_label=10`
- `miss_after_call=34`
- `miss_after_branch=72`
- `flush_multi_pred=134`
- `flush_label=186`
- `flush_branch=39`
- `flush_external_visibility=205`

Per-slot totals:

- `r31 loads/stores/clean/dirty/fallback=51/23/0/0/74`
- `r30 loads/stores/clean/dirty/fallback=38/19/0/0/57`
- `r29 loads/stores/clean/dirty/fallback=36/14/0/0/50`
- `r28 loads/stores/clean/dirty/fallback=36/10/0/0/46`
- `r27 loads/stores/clean/dirty/fallback=20/9/0/0/29`

## Decision

Close the current non-closed post-promotion GPR state-cache behavior lane for
now. The capture has material fallback traffic, but it has zero clean or dirty
hits possible and high boundary pressure. That fails the required threshold for
behavior work.

Do not run a quiet speed A/B. Do not patch store elision, load replacement, or
payload materialization from this lane. Do not repeat unchanged
`arm64_guest_state_nonclosed_cache_audit` captures for `0x82282490` or
`0x82287788`; the first wall-function capture did not show the required
post-promotion hit volume with manageable flush/spill pressure.

Reopen guest-state caching only with a broader CFG/dataflow model that can
handle multi-pred, branch, and external visibility boundaries without hiding
guest-visible state.

## Next

Before another risky Thor run, rerank the remaining structural lanes from the
current closure set. The immediate default should not be another guest-state
cache audit.
