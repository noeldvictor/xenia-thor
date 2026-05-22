# A64 GPR Cache Barrier-Preserve Negative

Date: 2026-05-21 21:27 EDT

## Goal

Test the narrowest runtime version of the `82282490` GPR state-cache plan:
preserve clean INT64 GPR cache entries across no-op HIR `context_barrier`
instructions without delaying or eliding any `STORE_CONTEXT`.

## Patch

Added default-off cvar:

```text
arm64_context_value_cache_preserve_barrier
```

When `arm64_context_value_cache=true` and the new cvar is true, the existing
A64 emit-time context-value cache keeps clean GPR entries across
`OPCODE_CONTEXT_BARRIER`. It still resets on block entry, calls, branches,
volatile ops, helper-expanded instruction ranges, and register overwrite
invalidation. It also now logs GPR slot-level cache hits and store caches when
`arm64_context_traffic_audit` is enabled.

## Validation

Build/deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both passed.

Thor capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextValueCache true -Arm64ContextValueCachePreserveBarrier true -Arm64ContextTrafficAudit true -Arm64ContextTrafficAuditFunction 0x82282490 -Arm64ContextTrafficAuditBudget 4 -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Evidence:

- Capture: `scratch/thor-debug/20260521-212305-*`
- APK SHA-256:
  `532DB72552DA7A3BD258567A52CB8CAE069827FD8A8743ECD709B1490EA2AFDD`
- Screenshot: `scratch/thor-debug/20260521-212305-screenshot.png`
- Route: visible opening sky/dragon-wing scene
- Fatal marker search: no matches for Android fatal/crash/GPU-hung markers
- `82282490` code size stayed `87168`

Cache result:

```text
A64 context value cache: fn 82282490 loads/hits=546/0 stores/cached=562/463 invalid offset/reg=1/768 resets safety/block/barrier=119/30/0 barrier_preserves=213 fallthrough_preserves=0
A64 context value cache slots: fn 82282490 load_hits=- stores=r[1]=11 r[3]=44 r[4]=42 r[5]=19 r[6]=17 r[7]=13 r[8]=30 r[9]=44 r[10]=64 r[11]=110 r[12]=7 r[16]=1 r[22]=3 r[23]=3 r[24]=1 r[26]=1 r[27]=5 r[28]=8 r[29]=10 r[30]=11 r[31]=19
```

The patch preserved across all `213` barriers and saw many candidate stores,
but still produced zero load hits.

## Conclusion

This proves the current emit-time cache is the wrong layer for the real speed
win. It has no durable host register: the normal HIR register allocator reuses
the same small `x22..x28` pool, and `register_invalidations=768` kills every
candidate before the next `LOAD_CONTEXT`.

Keep `arm64_context_value_cache_preserve_barrier` default-off. It is useful as
an audit/result path, not a speed preset.

## Next Move

The next state-cache attempt needs one of these, in order of safety:

1. a compile-time HIR promotion that removes redundant clean GPR
   `LOAD_CONTEXT` instructions before A64 register allocation;
2. a pinned-register experiment for one or two hot PPC GPRs, likely `r[1]` and
   `r[11]`, with explicit helper/exit/branch flushes;
3. a larger register allocator change that can reserve durable state registers
   for chosen PPC GPRs.

Do not spend more time on the current emit-time cache by merely preserving
across more barriers; the Thor audit shows that is not where the hits are lost.
