# Blue Dragon r1 Clean-Load Source Gate

## Summary

The planned default-off `r1` clean-load replacement prototype for
`0x82282490` is not ready to implement as stated. Source review shows the
current `ContextPromotionPass` already performs same-block context-load
promotion for all context ranges before the optional local-slot and live-in
passes run.

This means the route-clean `arm64_guest_state_register_cache_audit` row in
`docs/research/20260525-160247-arm64-guest-state-register-cache-audit-capture.md`
is still useful, but its `clean_hits_possible` counter is a pre-promotion upper
bound. It should not be treated as proof that a same-block `r1` replacement
patch would change generated code.

## Source Evidence

In `src/xenia/cpu/compiler/passes/context_promotion_pass.cc`, the
`arm64_guest_state_register_cache_audit` hook runs before the normal per-block
promotion loop:

```text
AuditGuestStateRegisterCache(builder);
...
PromoteBlock(block);
```

Inside `PromoteBlock`, a `LOAD_CONTEXT` is already rewritten to `ASSIGN` when
`TryGetContextValue` finds a prior in-block value with matching offset, size,
and type. `STORE_CONTEXT` updates the same per-block context value table through
`SetContextValueRange`.

The existing behavior is therefore already the safe version of same-block clean
load replacement. A new `r1` clean-load prototype would only be useful if it
proved residual opportunities after `PromoteBlock`, or crossed boundaries that
the existing pass intentionally flushes.

## Prior r1 Lane Evidence

The broader `r1` family has already been explored:

- local-slot promotion was route-clean but had poor hit shape;
- pinned `r1` and live-in `r1` probes did not prove a quiet speed win;
- the current `arm64_context_promotion_gpr_livein_r1` path remains default-off.

Do not rerun those unchanged. The new audit row is valuable because it points to
state traffic, but it is not enough to justify another same-block replacement
patch.

## Decision

Do not implement the planned same-block `r1` clean-load replacement prototype
from the pre-promotion counters alone.

Next useful slice:

- add or run a default-off post-`PromoteBlock` residual audit for
  `0x82282490`;
- report remaining `r1` / `r11` `LOAD_CONTEXT` and `STORE_CONTEXT` traffic,
  residual clean/dirty opportunities, boundary flush reasons, and estimated
  spill pressure after the normal pass has already rewritten same-block loads;
- keep behavior unchanged, no store elision, no `r11` dirty caching, no payload
  materialization;
- only consider a behavior patch if the residual audit is route-clean on Thor
  and shows material post-promotion hit volume.

If residual `r1` volume collapses, return to the caller-local/side-table
edge-variant storage lane for `82282490:82282598 -> 82287788` instead of
spending another slice on same-block GPR caching.
