# 82282490 Exclusive Callee Attribution

Date: 2026-05-24 03:10 EDT

## Goal

Answer the next question after the entry/exit profiler: is the Blue Dragon
opening-route wall in `82282490` caused by generated prolog/epilog/stackpoint
cost, local block work, or inclusive recursive/child-call work?

## Capture

- Branch: `master`
- HEAD: `079795cb8`
- APK SHA-256:
  `E730F463807B0E60B6F8B67C2D0C9FF4DCA63CBC5C5D4BF187FFA2B6BD505F4F`
- Capture: `scratch/thor-debug/20260524-030450-*`
- Log: `scratch/thor-debug/20260524-030450-speed-logcat.txt`
- Screenshot: `scratch/thor-debug/20260524-030450-screenshot.png`
- Route evidence: visible opening sky/wing scene with `Microsoft Game Studios Presents`.
- Fatal-marker search: `Fatal signal=0`, `AndroidRuntime=0`,
  `VK_ERROR_DEVICE_LOST=0`, `GPU is hung=0`.

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 30 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64BlueDragonVmxCopyLoopFastpath false -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,8227FEE8" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileBlockFilter "82282490" -Arm64SpeedProfileBlockBodyTime true -Arm64SpeedProfileCallEdgeFilter "82282490" -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

## Final Function Body-Time Rows

| Function | Body ticks total | Last interval entries | Ticks per entry | Code size |
| --- | ---: | ---: | ---: | ---: |
| `82282490` | 101054711 | 48096 | 393 | 112080 |
| `82281D28` | 14250972 | 20532 | 96 | 96908 |
| `8227FEE8` | 5572007 | 18256 | 104 | 49804 |

The `82282490` code size is inflated by block/call-edge profiling and should
not be compared directly to quiet speed captures.

## 82282490 Block/Edge Split

Approximate exclusive ticks subtract same-run direct-call edge body time from
the final block body total. This is attribution guidance, not an exact
cycle-accounting model.

| Block guest | Block ticks total | Dynamic edge target | Edge ticks total | Approx exclusive | Exclusive share |
| --- | ---: | --- | ---: | ---: | ---: |
| `822825E0` | 73427065 | `82282490` | 72194274 | 1232791 | 1.68% |
| `822825C8` | 5698304 | `8227FEE8` | 5640616 | 57688 | 1.01% |
| `8228252C` | 8530471 | `82287788` | 5653971 | 2876500 | 33.72% |
| `822824F0` | 3501617 | `82274DB0` | 1480443 | 2021174 | 57.72% |
| `822824B8` | 1099164 | - | 0 | 1099164 | 100% |
| `822825F4` | 1033964 | - | 0 | 1033964 | 100% |
| `00000000` | 618491 | - | 0 | 618491 | 100% |
| `82282490` | 249802 | - | 0 | 249802 | 100% |
| `82282600` | 248103 | - | 0 | 248103 | 100% |
| `822824EC` | 189195 | - | 0 | 189195 | 100% |

Dynamic direct-call edges sum to `84969304` ticks, or `84.08%` of final
`82282490` body ticks. The self-recursive `822825E0 -> 82282490` edge alone is
`72194274` ticks, about `71.44%` of final `82282490` body ticks and `98.32%`
of the `822825E0` block body.

Joining the new capture with the older filtered OptHIR dump
`scratch/thor-debug/20260521-170941-speed-logcat.txt` shows `822825E0` is a
small recursive child traversal:

```text
822825E0 lwz r3, 0x38(r31)
822825E4 cmpli crf0, r3, 0
822825E8 beq 0x822825F4
822825EC or r4, r29, r29
822825F0 bl 0x82282490
822825F4 lwz r31, 0x3C(r31)
822825FC bne 0x822824B8
```

The measured entry/exit profiler already proved generated prolog/epilog and
stackpoint overhead is tiny. This capture reinforces that the recursive edge is
mostly child body work, not caller-side call overhead.

## Decision

- Do not rerun the same block/call-edge capture unchanged.
- Do not chase stackpoint/prolog/epilog as the main wall.
- Do not start a stale `822824F0` vector peephole from old entry counts.
- Treat `8228252C` and `822824F0` as the next local-work candidates only after
  mapping dynamic generated block starts back to exact HIR/PPC spans. The
  dynamic `8228252C` block is inside the older `loc_822824F0` HIR region rather
  than a separate HIR label, so the current static report is too coarse for a
  codegen patch.

## Next Action

Add or run a dynamic-block slice report that maps block-body guests such as
`8228252C` back to the filtered `82282490` OptHIR/PPC span, then choose a
default-off, function/block-gated state-traffic or lowering experiment from the
body-backed local spans. If the next idea is a self-recursion special path, it
needs a separate correctness proof because this capture says the call edge is
mostly real callee work.
