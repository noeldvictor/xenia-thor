# A64 Non-Closed Guest-State Cache Audit

## Scope

The no-wrap fastmem lane and guarded-stub fast-entry lane are closed for
behavior. This slice checks whether the remaining guest-state/cache lane has a
non-closed target that avoids the already-rejected `r1`, `r11`, `f[1]`, fpscr,
and barrier paths.

This is offline/source analysis only. It changes no generated behavior and is
not speed proof.

## Tooling

Added:

```powershell
tools/thor/thor_a64_nonclosed_guest_state_cache_audit.ps1
```

Ran:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_nonclosed_guest_state_cache_audit.ps1
```

Output:

```text
scratch/thor-debug/20260526-085500-a64-nonclosed-guest-state-cache-audit.txt
```

The tool joins current route-backed HIR state-span reports for:

- `82281D28`: `scratch/thor-debug/20260526-025401-speed-logcat.txt`
- `82282490`: `scratch/thor-debug/20260526-080921-speed-logcat.txt`
- `82287788`: `scratch/thor-debug/20260526-080921-speed-logcat.txt`

It excludes `r1` / `r11` as explicitly closed core slots and reports
`r3-r10` separately as call-argument overlap.

## Source Checks

The source gate is unchanged:

```text
same_block_context_promotion_exists=True
context_promotion_per_block=True
register_allocation_per_block=True
spills_to_locals=True
emit_time_context_cache_resets_per_block=True
host_gpr_pressure=True
existing_pre_promotion_audit_hook=True
```

Meaning:

- same-block context loads are already handled by `ContextPromotionPass`;
- the current A64 emit-time cache resets per block;
- register allocation is per-block and spill-prone;
- behavior work needs a post-promotion CFG/lifetime audit, not another
  same-block or emit-time cache attempt.

## Ranking

Closed slots still dominate raw score:

```text
r1 score=567 class=closed_core
r11 score=446 class=closed_core
```

Call-argument slots also show volume, but overlap the closed fast-entry /
guest-stack handoff lanes:

```text
r10 score=251
r9 score=186
r3 score=155
r8 score=135
r7 score=125
r6 score=115
```

The strict non-closed candidate set is:

```text
r31 score=237 functions=82281D28:129,82282490:92,82287788:16
r30 score=175 functions=82281D28:89,82282490:71,82287788:15
r29 score=168 functions=82281D28:84,82282490:78,82287788:6
r28 score=152 functions=82281D28:81,82282490:69,82287788:2
r27 score=94 functions=82281D28:50,82282490:44
r26 score=58 functions=82281D28:32,82282490:26
```

These are mostly pointer/state GPRs with cross-span load-after-load and
load-after-store shape, not a same-block replacement shape.

## Decision

There is a real non-closed guest-state/cache target:

```text
decision=nonclosed_guest_state_cache_target_exists top_slots=r31,r30,r29,r28,r27
behavior_patch_now=0
```

Do not patch generated behavior yet. Same-block loads are already promoted, and
the current emit-time cache resets per block. The remaining target needs CFG
lifetime and boundary accounting before store elision or load replacement.

## Next Slice

Implement only a default-off, counter-only post-promotion non-closed GPR
state-cache audit for `r31,r30,r29,r28,r27`.

Required contract:

- no store elision;
- no context-load replacement;
- no payload materialization;
- count candidate loads/stores after normal promotion;
- count clean/dirty hits possible by slot;
- count misses for no entry, multi-pred/label, overlap, volatile, call/helper,
  branch, return, trap/debug, and external visibility;
- estimate host GPR spill pressure;
- include function filtering for the wall cluster.

Only consider behavior if the audit is route-clean and proves material
post-promotion traffic with manageable flush and spill pressure.
