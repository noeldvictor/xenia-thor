# 82287788 Function-Pair Variant Audit

Date: 2026-05-24 11:29 EDT

## Question

Can the hot `82282490` call at `82282598 -> 82287788` use a specialized
callee entry or thunk to avoid `82287788` `f[1]` context reloads, without
breaking guest-visible PPC state?

## New Tool

Added:

```text
tools/thor/thor_hir_function_pair_variant_audit.ps1
```

The tool combines:

- parent filtered HIR;
- callee filtered HIR;
- optional dynamic call-edge rows;
- a target context offset;
- a whitelist of preserved child call PCs.

It reports the nearest parent seed store, callee target loads/stores, call
classes before loads, static replacement upper bounds, and the compiler shape
required for a function-pair variant.

## Commands

`f[1]` candidate:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_function_pair_variant_audit.ps1 -ParentLogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -ParentFunction 82282490 -ParentCallPc 82282598 -CalleeLogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -CalleeFunction 82287788 -Offset 296 -EdgeProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -PreservedCallPcs "82287854,82287ED4" -Top 24
```

`fpscr` control:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_function_pair_variant_audit.ps1 -ParentLogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -ParentFunction 82282490 -ParentCallPc 82282598 -CalleeLogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -CalleeFunction 82287788 -Offset 2628 -EdgeProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -PreservedCallPcs "82287854,82287ED4" -Top 12
```

## `f[1]` Findings

Dynamic edge:

- parent block `8228252C`
- `calls_total=1691272`
- `body_ticks_total=5653971`
- `ticks_per_call=4`

Parent seed:

- nearest `f[1]` store is present at `82282594`
- text: `store_context +296, v301.f64<vec0>`
- distance to call row: `5`

Callee target access:

- `loads=10`
- `stores=0`
- `replaceable_loads=10`
- `unsafe_loads=0`
- `static_replace_upper=16912720`

Load PCs:

- `82287798`
- `82287828`
- `82287A1C`
- `82287A2C`
- `82287AA4`
- `82287CF8`
- `82287D10`
- `82287D8C`
- `82287EA8`
- `82287F1C`

Callee calls:

- total calls: `6`
- preserved helpers: `4`
- preserved child calls: `2`
- unsafe or unknown calls: `0`

Preserved child call PCs were supplied from earlier `821CE028` proof:

- `82287854`
- `82287ED4`

This makes `f[1]` a static function-pair entry-seed candidate for this edge.

## `fpscr` Control

The same audit rejects `fpscr` as a pair-entry seed:

- `loads=26`
- `stores=26`
- `replaceable_loads=0`
- `variant_shape=blocked_by_callee_writes`

That agrees with the forwarding-risk plan: `fpscr` needs a dirty-status cache
and exact writeback model, not a read-only entry seed.

## Source Constraint

Current A64 direct-call emission uses the generic guest function ABI. In
`src/xenia/cpu/backend/a64/a64_emitter.cc`, `A64Emitter::Call` passes the
next guest return address in `x0` before `blr`:

- `A64Emitter::Call`: line `4268`
- comment: line `4307`
- `ldr(x0, ... GUEST_CALL_RET_ADDR)`: line `4310`

So a function-pair variant is not just a local `load_context` peephole. The
normal call path has no extra `f[1]` argument slot today.

## Decision

No generated-code behavior patch in this slice.

`f[1]` is a real pair-specific candidate, but the implementation needs a
deliberate call/entry design. The safest next code experiment is not another
`82287788` local stack-slot carrier. It is one of:

1. A default-off pair-specific direct-call thunk for `82282490:82282598 ->
   82287788` that passes or seeds `f[1]` while preserving the generic guest
   ABI for every other caller.
2. A compile-time audit/probe proving a caller-seeded callee-local slot can be
   used without changing normal `A64Emitter::Call` semantics.

In either path:

- keep the parent `store_context +296` visible unless a later visibility audit
  proves it can move;
- do not alter normal `82287788` entry semantics;
- fall back or kill the carrier on unknown calls, indirect child calls before
  target loads, callee target stores, exits/exceptions, and overlapping context
  writes;
- do not include `fpscr`, `r[3]`, or `lr` in this first pair-variant path.
