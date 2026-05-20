# A64 Context Cache Negative And Kernel Spinlock Fastpaths

Date: 2026-05-20 19:29 EDT

Branch: `master`

Goal: reduce Blue Dragon's Thor CPU overhead after the `8272A3A4` context
traffic audit showed heavy PPC state churn and the speed profiles showed tiny
kernel lock exports in the hot list.

## Summary

Two lanes were tested:

- A block-local A64 context value cache for PPC GPR `LOAD_CONTEXT` /
  `STORE_CONTEXT`.
- Inline A64 bodies for raised-IRQL spin-lock exports:
  `KeAcquireSpinLockAtRaisedIrql`, `KeTryToAcquireSpinLockAtRaisedIrql`, and
  `KeReleaseSpinLockFromRaisedIrql`.

The context value cache is not the answer for `8272A3A4`: both probes reported
`255/0` load hits. Keep it default-off as a diagnostic. The real state-cache
work needs a cross-block/PPC-state cache with explicit flush rules.

The spin-lock fastpaths are useful. With `KfLowerIrql` still native, Blue Dragon
stayed active and reached the Voice Language menu. Guest-to-host calls dropped
versus the previous healthy route, while the top entry counters stayed high
because the guest still calls the same exports often.

## Captures

Context cache probes:

- `scratch/thor-debug/20260520-191256-*`
  - `arm64_context_value_cache=true`
  - `A64 context value cache: fn 8272A3A4 loads/hits=255/0 stores/cached=252/240 invalid offset/reg=0 resets=375`
  - `8272A3A4 code_size=12196`, `8272A8E8 code_size=5356`
  - Reached the Blue Dragon Voice Language menu.
- `scratch/thor-debug/20260520-191641-*`
  - `arm64_context_value_cache=true`
  - `A64 context value cache: fn 8272A3A4 loads/hits=255/0 stores/cached=252/240 invalid offset/reg=0 resets=413`
  - `8272A3A4 code_size=12196`, `8272A8E8 code_size=5356`
  - Black-idled later.

Spinlock and `KfLowerIrql` probe:

- `scratch/thor-debug/20260520-192530-*`
  - `a64_inline_kernel_spinlock_exports=true`
  - `a64_inline_kf_lower_irql=true`
  - APK SHA-256 `1FE8D70153ECB2BF6F7EAAF686F872AE9D3303CF8D79ECE5ED85E68F8FAF2CF2`
  - Black-idled after the early burst. This makes the APC-skipping
    `KfLowerIrql` inline unsafe as a default.
- `scratch/thor-debug/20260520-192739-*`
  - `a64_inline_kernel_spinlock_exports=true`
  - `a64_inline_kf_lower_irql=false`
  - Same APK SHA-256 `1FE8D70153ECB2BF6F7EAAF686F872AE9D3303CF8D79ECE5ED85E68F8FAF2CF2`
  - Reached the Blue Dragon Voice Language menu.
  - 40-second perf: Main Thread about `76.9%`, XMA Decoder about `46.1%`,
    GPU Commands about `15.3%`.
- `scratch/thor-debug/20260520-193232-*`
  - Final safe-default proof after rebuilding with `a64_inline_kf_lower_irql`
    default-off.
  - APK SHA-256 `3C89CD3DBEC4FBD9735D29A10178F24BA584D1B9C2BD7EEEBE9C5C1156FB4159`
  - `a64_inline_kernel_spinlock_exports=true`
  - `a64_inline_kf_lower_irql=false`
  - Reached the Blue Dragon Voice Language menu.
  - 40-second perf: Main Thread about `84.6%`, XMA Decoder about `42.3%`,
    GPU Commands about `19.2%`.

## Counter Notes

Previous healthy comparison capture `scratch/thor-debug/20260520-191256-*`:

- 15-30s interval: `g2h=263236`, `extern=338797`
- 30-45s interval: `g2h=322262`, `extern=406078`

Spinlock-on, `KfLowerIrql`-native capture
`scratch/thor-debug/20260520-192739-*`:

- 15-30s interval: `g2h=206432`, `extern=344905`
- 30-45s interval: `g2h=244721`, `extern=403914`
- 45-60s interval: `g2h=271915`, `extern=442616`

Interpretation: extern entry counts remain high because the same guest exports
are still called frequently, but the generated A64 path avoids many native
guest-to-host thunk trips.

## Decision

- Keep `arm64_context_value_cache` default-off.
- Keep `a64_inline_kernel_spinlock_exports` default-on.
- Keep `a64_inline_kf_lower_irql` default-off. It is an opt-in experiment until
  there is an APC-aware fastpath or a helper that can preserve the native
  `CheckApcs()` behavior.

## Next Work

- Add a safe `KfLowerIrql` path only if it preserves APC delivery or proves a
  title-local no-APC condition.
- Continue reducing `RtlEnterCriticalSection` / `RtlLeaveCriticalSection`
  native fallback cost.
- Build a real PPC state cache across HIR blocks; the simple block-local
  context value cache found no same-block reload hits in `8272A3A4`.
