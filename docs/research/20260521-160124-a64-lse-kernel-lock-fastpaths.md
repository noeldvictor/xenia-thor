# A64 LSE Kernel Lock Fastpaths

Date: 2026-05-21 16:01 EDT

## Goal

Reduce Blue Dragon's Thor CPU overhead in the high-frequency Xbox kernel lock
path without adding another title-specific shortcut.

Recent clean profiles showed the post-boot route repeatedly dominated by:

- `RtlEnterCriticalSection`
- `RtlLeaveCriticalSection`
- `KeRaiseIrqlToDpcLevel`
- `KeAcquireSpinLockAtRaisedIrql`
- `KeReleaseSpinLockFromRaisedIrql`
- `KfLowerIrql`

The A64 backend already detects ARMv8.1 Large System Extensions (LSE) for HIR
atomics, but the hand-emitted kernel fastpaths still used explicit
`ldaxr`/`stlxr` retry loops.

## Change

Added `a64_lse_kernel_lock_fastpaths` (default true) and Android/Thor launch
forwarding. When the host supports `kA64EmitLSE`, the A64 kernel fastpaths now
use:

- `swpal` for `KeRaiseIrqlToDpcLevel` and opt-in inline `KfLowerIrql` IRQL
  exchange.
- `casal` for spinlock acquire and critical-section free-lock claims.
- `ldaddal` for spinlock release and critical-section lock-count increment /
  decrement.

The cvar is still independently disableable with:

```powershell
-A64LseKernelLockFastpaths false
```

## Captures

- `scratch/thor-debug/20260521-155632-*`: first LSE-on 70s run with
  `A64RtlLeaveFastpathAudit=true`.
- `scratch/thor-debug/20260521-155831-*`: LSE-on A/B run, audit off,
  60 seconds.
- `scratch/thor-debug/20260521-155946-*`: LSE-off A/B run, same APK, audit off,
  60 seconds.

APK SHA-256 for the A/B runs:

```text
588A4246FBC7D6E0EC2B383D6F219926F9D81D56D29ADC5D6EE5AE4AFBFC35F9
```

## Results

The LSE-on A/B run reached the visible Blue Dragon Voice Language screen at
60 seconds with no searched fatal markers. The LSE-off run on the same APK
black-idled after the first burst:

| Capture | Cvar | 15s entry_delta | 30s entry_delta | 45s entry_delta | 60s entry_delta | Screenshot |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `20260521-155831` | `true` | 3,820,974 | 1,659,860 | 1,906,059 | 2,016,964 | Voice Language |
| `20260521-155946` | `false` | 3,881,933 | 171,000 | 0 | 0 | black |

The generated hot kernel thunk sizes also shrink with LSE enabled:

| Function | LSE on | LSE off |
| --- | ---: | ---: |
| `RtlEnterCriticalSection` | 492 | 504 |
| `RtlLeaveCriticalSection` | 516 | 528 |
| `KeRaiseIrqlToDpcLevel` | 324 | 332 |
| `KeAcquireSpinLockAtRaisedIrql` | 376 | 388 |
| `KeReleaseSpinLockFromRaisedIrql` | 364 | 372 |

`KfLowerIrql` stays at 336 bytes because the unsafe inline KfLower path remains
default-off.

## Decision

Keep `a64_lse_kernel_lock_fastpaths=true` as the default for Thor. It is guarded
by the existing host LSE feature check and has a one-switch rollback path.

This is not the whole FPS fix. It improves the synchronization hot path and
route behavior, but the next large wall remains the Blue Dragon PPC hot set
(`827294CC`, `8272A3A4`, `8272A8E8`) plus XMA/audio and GPU command work.
