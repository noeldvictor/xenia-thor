# A64 Context Cache And CR Branch Negative Results

Date: 2026-05-21 15:33 EDT

## Goal

Speed up Blue Dragon on AYN Thor by turning the `8272A3A4` context-traffic
evidence into larger A64 backend wins instead of isolated peepholes.

## Captures

- `scratch/thor-debug/20260521-151017-*`: fallthrough context-value cache
  experiment, `arm64_context_value_cache=true`,
  `arm64_context_value_cache_fallthrough=true`,
  `arm64_context_traffic_audit_function=0x8272A3A4`.
- `scratch/thor-debug/20260521-151433-*`: filtered PPC/OptHIR dump for
  `8272A3A4` with explicit `-LogLevel 2`.
- `scratch/thor-debug/20260521-151717-*`: initial CR compare/branch barrier
  fusion proof before the broader unsafe branch/store experiments.
- `scratch/thor-debug/20260521-152204-*`: `8272A3A4` CR-store elide probe.
- `scratch/thor-debug/20260521-153020-*`: final clean proof after restoring
  safe defaults and interleaved CR `cset`/`strb` order.

## Findings

The fallthrough context cache did not hit the hot function. `8272A3A4` reported
`loads/hits=255/0`, `stores/cached=252/240`,
`resets safety/block/barrier=0/17/32`, and `fallthrough_preserves=0`. This
means the easy "carry a GPR context value into the next unlabeled HIR block"
shape is not present in this function.

The HIR dump showed the real repeated shape:

```text
compare_ult/compare_ugt/compare_eq
store_context CR6 LT/GT/EQ
context_barrier
branch_true/branch_false on one compare result
```

Trying to fuse LT/GT/EQ branches across the `context_barrier`, or skipping CR
stores when the branch immediately consumes the compare, is not safe as a quick
patch. The scoped CR-store elide run crashed the guest at PC `826A2498`, and
the broadened branch fusion also crashed when enabled by default. These paths
are now default-off and the Blue Dragon presets force them off unless explicitly
overridden.

One implementation bug was caught and fixed: the safe CR triplet path must keep
the old interleaved `cset, strb, cset, strb, cset, strb` order. Some optimized
HIR compare values reuse the same host register, so batching all `cset`s before
the stores corrupts CR bytes.

Final clean proof `scratch/thor-debug/20260521-153020-*` reached the Voice
Language screen with:

- `arm64_context_value_cache=false`
- `arm64_context_value_cache_fallthrough=false`
- `arm64_cr_compare_branch_across_context_barrier=false`
- `arm64_cr_store_elide_for_fused_branch=false`
- APK SHA-256
  `4C19B2920B5291ACD389798B8B654E2709136E944CB54417916D56E298914D42`

## Decision

Keep both new shortcuts as diagnostic, default-off cvars only:

- `arm64_context_value_cache_fallthrough`
- `arm64_cr_compare_branch_across_context_barrier`
- `arm64_cr_store_elide_for_fused_branch`

Do not enable them in the Blue Dragon speed pack.

The next viable version of this idea is not "skip CR stores"; it is a real PPC
CR/GPR state cache with correctness-aware flushes at helpers, exits,
exceptions, branch joins, and exact context aliases.
