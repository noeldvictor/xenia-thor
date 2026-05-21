# V128 Swapped Store Address-Spill Negative

Date: 2026-05-21 17:50 EDT

Branch: `master`

## Question

Can the A64 `STORE_V128` swapped-store path save one instruction by avoiding
the address spill to `x17` when the source vector is not constant?

## Rationale

Blue Dragon's `82282490` HIR has `stvx` traffic. The PPC frontend emits
`stvx` as a big-endian vector store, and the compiler memory-combination pass
can fold `byte_swap -> store` into a store with `LOAD_STORE_BYTE_SWAP`. The A64
`STORE_V128` implementation then emits:

1. compute guest address into `x0`,
2. save `x0` to `x17` for swapped stores,
3. `rev32` source vector into `v0`,
4. `str q0, [membase + x17]`.

For non-constant vector sources, `SrcVReg` only returns the existing vector
register index and does not clobber `x0`, so the `mov x17, x0` looks
unnecessary. The experiment changed `need_src_load` to save the address only
when `i.src2.is_constant`.

## Validation

Build and deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both passed. APK SHA:
`07FFA8311A4998B9701CA7C2D936E68E11CC40D9BE4D9232379E94874E30FF10`.

Thor capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Capture `scratch/thor-debug/20260521-174627-*` black-idled before delayed
body-time activated. It logged `entry_delta=0` from 17:47:15 onward and the
final screenshot was black. No useful `82282490` body-time or code-size proof
was produced.

## Decision

Reverted the local code change and did not commit it. The idea may be
semantically valid, but it is too small to carry without a route-clean proof.
Do not reintroduce this store-spill cleanup unless the test plan includes at
least one clean Blue Dragon opening-scene capture and ideally a disassembly or
code-size audit proving it actually hits `82282490`.

The better next target is still larger VMX state traffic, especially context
load/store reduction around `82282490` and exact patterns that produce a
visible code-size/body-time win.

