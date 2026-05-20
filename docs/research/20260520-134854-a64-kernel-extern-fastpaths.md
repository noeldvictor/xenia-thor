# A64 Kernel Extern Fastpaths

Date: 2026-05-20 13:48 EDT

## Question

Can Blue Dragon's current Thor speed lane get a real win by avoiding the A64
guest-to-host thunk for very hot Xbox kernel exports, without destabilizing the
route?

## Implementation

Added `a64_inline_kernel_high_frequency_exports`, default `true`, in the A64
emitter.

Current kept fastpaths:

- `KeRaiseIrqlToDpcLevel`: direct atomic exchange on `Processor::irql_`, result
  returned in guest `r3`.
- `RtlEnterCriticalSection`: recursive-owner fast path and uncontended
  `lock_count -1 -> 0` acquire; contested cases fall back to the original host
  export.
- `RtlTryEnterCriticalSection`: uncontended acquire, recursive-owner acquire,
  or guest `r3=0` on normal failure; null/unsafe setup falls back.
- `RtlLeaveCriticalSection`: recursive release only. Final unlock, waiter wake,
  assert/error cases, and event signaling stay on the original host export.

The Thor debug launcher now exposes the rollback switch:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 `
  -DeviceSerial c3ca0370 `
  -Mode LaunchBlueDragonSpeedCapture `
  -A64InlineKernelHighFrequencyExports false
```

## Evidence

Baseline for comparison: `scratch/thor-debug/20260520-133239-*`

- Kept only the IRQL inline after the first spinlock attempt proved suspicious.
- No searched fatal markers.
- Guest execution kept moving through 75 seconds.

Stable kept run: `scratch/thor-debug/20260520-134646-*`

- APK SHA-256:
  `5D60BD98548642CEE488FB2AB9C3C7478619CD700687A8C2A28C17C381E31100`.
- No searched fatal markers.
- Metadata stayed in `EmulatorActivity`, PID `20858`.
- Screenshot was still black/loading-route state, not title or gameplay proof.
- Guest execution stayed active through the 75 second interval.

Speed-profile guest-to-host deltas:

| Interval | IRQL-only baseline `133239` | Kept fastpaths `134646` | Change |
| --- | ---: | ---: | ---: |
| 15s | 213,067 | 141,842 | -33.4% |
| 30s | 326,440 | 280,461 | -14.1% |
| 45s | 379,179 | 322,098 | -15.0% |
| 60s | 416,413 | 356,126 | -14.5% |
| 75s | 424,232 | 332,505 | -21.6% |

Guest entry deltas remained healthy in the kept run:

- 15s: `3,879,661`
- 30s: `1,701,093`
- 45s: `1,929,334`
- 60s: `2,037,705`
- 75s: `5,328,455`

## Rejected Spinlock Inline

Rejected capture: `scratch/thor-debug/20260520-134410-*`

- A guarded spinlock experiment inlined `KeAcquireSpinLockAtRaisedIrql`,
  `KeTryToAcquireSpinLockAtRaisedIrql`, and
  `KeReleaseSpinLockFromRaisedIrql`.
- It used a one-CAS acquire path with host fallback on busy locks, but still
  reproduced the bad route signature.
- The speed profile went from `entry_delta=155080` at the second interval to
  `entry_delta=0` from 45 seconds onward, with no fatal marker.
- The spinlock inline was removed before the final APK was redeployed.

Earlier rejected capture: `scratch/thor-debug/20260520-132805-*`

- A more eager spinlock inline also black-idled the route.
- Treat spinlock export inlines as correctness-sensitive until there is a
  separate proof of guest lock ownership, waiter behavior, and release ordering.

## Interpretation

This is a useful transition-cost reduction, not a 30 FPS fix. Rtl and IRQL
exports still appear high in guest entry counts because the guest still calls
those functions; the win is that many of those entries no longer enter the
expensive guest-to-host thunk.

`KfLowerIrql` was not inlined because the host export calls
`XThread::CheckApcs()`. Skipping APC delivery would be a correctness risk.

The next speed work should use body-time or simpleperf evidence again. Current
remaining walls are likely:

- actual generated guest hot loops such as `826C5620`, `827294CC`,
  `8272A3A4`, and `826BF770`;
- remaining kernel externs that cannot be safely inlined without thread/APC or
  waiter semantics;
- XMA/audio cost where `xma_fast_silence` is useful only as an opt-in probe.

## Decision

Keep the Rtl/IRQL fastpaths and the scripted rollback switch. Do not keep
spinlock export inlines. Continue toward 30-60 FPS with measured A64 body-time
work and semantics-safe generic lowering, not broad lock shortcuts.
