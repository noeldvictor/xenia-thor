# Blue Dragon VMX Dot F32 Fastpath Negative

Date: 2026-05-21 17:28 EDT

Branch: `master`

## Question

Can Blue Dragon's current opening-scene hot function `82282490` be sped up by
lowering VMX `dot_product_3/4` as single-precision NEON `fmul/fadd`, closer to
the x64 DPPS-style path, instead of the existing A64 double-widened sequence?

## Evidence

The filtered HIR/disassembly capture
`scratch/thor-debug/20260521-170941-*` reached the opening scene and showed
`82282490` is not a tiny helper. It is a large VMX-heavy routine. The visible
slice contained repeated `vmsum4fp128` / `dot_product_4`, `vmrghw` /
`permute.2`, `lvx`, `stvx`, `byte_swap`, and many vector context loads and
stores.

Visible RawHIR pattern counts from the useful slice:

- `load_context`: 393
- `store_context`: 264
- `byte_swap`: 96
- `branch`: 60
- `permute.2`: 36
- `dot_product_4`: 31
- `vmrghw`: 29
- `context_barrier`: 26
- `stvx`: 17
- `store_offset`: 14
- `lvx`: 8
- `call`: 8

Visible OptHIR counts were even heavier on state traffic:

- `store_context`: 1415
- `context_barrier`: 213
- `branch`: 203
- `load.1`: 186
- `store.1`: 184
- `vmrghw`: 117
- `store_offset`: 86
- `stvx`: 79
- `beq`: 79
- `stw`: 67
- `call`: 60
- `cmpl`: 56
- `lvx`: 53

## Change Tested

Added `arm64_vmx_dot_f32_fastpath` as an Android-forwarded A64 diagnostic cvar.
When enabled, `DOT_PRODUCT_3` and `DOT_PRODUCT_4` use NEON single-precision
`fmul/fadd`, then preserve the existing infinity-to-QNaN fixup. When disabled,
the old double-widened A64 sequence remains in use.

Validation before device capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both passed. APK SHA for the A/B was
`7E844BB478C6450D64BF35F686AA06FE8C9C4A75A3DACDC6746F65B69008D7FB`.

## Result

Fastpath on:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64VmxDotF32Fastpath true -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Capture `scratch/thor-debug/20260521-171859-*` black-idled. It logged
`entry_delta=0` from 17:19:46 onward, body-time activated after 120 seconds but
had no `82282490` entries, and the final screenshot was black.

Fastpath off:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64VmxDotF32Fastpath false -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Capture `scratch/thor-debug/20260521-172247-*` recovered the route and reached
the opening sky/dragon-wing scene. Body-time for `82282490` was:

- 17:25:07: `body_ticks_delta=3560133`, `entries_delta=15003`,
  `ticks_per_entry=237`
- 17:25:22: `body_ticks_delta=3435237`, `entries_delta=18337`,
  `ticks_per_entry=187`
- 17:25:37: `body_ticks_delta=8608906`, `entries_delta=59544`,
  `ticks_per_entry=144`
- 17:25:52: `body_ticks_delta=8051460`, `entries_delta=65296`,
  `ticks_per_entry=123`

The overlapping `82281D28` cluster also remained hot at 17:25:52:
`body_ticks_delta=2944779`, `entries_delta=31152`, `ticks_per_entry=94`.

## Decision

Leave `arm64_vmx_dot_f32_fastpath=false` by default and keep it only as an
explicit diagnostic knob. The naive single-precision dot lowering is not
route-safe for Blue Dragon.

The next speed work should target more semantics-preserving waste in `82282490`:

1. direct A64 lowering for common `vmrghw` / constant `PERMUTE_I32` shapes,
2. `stvx` byte-swap plus store fusion where the HIR pattern proves exact,
3. vector/GPR context traffic reduction across VMX-heavy blocks,
4. a corrected dot-product experiment only after a unit/audit path proves PPC
   VMX, x64 DPPS, and A64 FPCR/NaN/rounding behavior match for the relevant
   Blue Dragon values.

