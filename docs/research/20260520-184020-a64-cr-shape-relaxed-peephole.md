# A64 CR Shape Audit And Relaxed Peephole

Date: 2026-05-20 18:40 EDT

Branch: `master`

Device: AYN Thor Max / `c3ca0370`

Target: Blue Dragon Disc 1,
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

Why did the previous `LT/GT/EQ` PPC CR compare-store peephole not shrink
`8272A3A4`, even though the context audit showed 183 CR stores and repeated
stores to `0xA3C..0xA3E`?

## Changes

- Extended the A64 context-traffic audit to separate CR store source opcodes
  from generic context stores.
- Added exact CR shape counters:
  - `LT/GT/EQ` triplets, including strict old-guard matches.
  - `UGT/EQ` adjacent pairs, including strict old-guard matches.
  - CR6 `all_equal/none_equal` `IS_FALSE` shapes.
- Relaxed the A64 `LT/GT/EQ` triplet and `UGT/EQ` pair peepholes so compare
  results may have later users. The emitted code still materializes each compare
  result with `cset` into the original HIR value register before skipping only
  the redundant compare/store instructions.

## Commands

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 50 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "8272A3A4" -Arm64ContextTrafficAudit true -Arm64ContextTrafficAuditFunction 0x8272A3A4 -Arm64ContextTrafficAuditBudget 4
```

## Evidence

Pre-relax audit capture: `scratch/thor-debug/20260520-183455-*`

- `cr_store_src cmp/const/select/is_false=183/0/0/0`
- `top=0xA3E:cmp_eq:61,0xA3C:cmp_ult:51,0xA3D:cmp_ugt:51,0xA3C:cmp_slt:10,0xA3D:cmp_sgt:10`
- `cr_shapes triplet=60/0 signed/unsigned=10/50 gt_eq=50/9 cr6=0/0`
- `8272A3A4 code_size=12544`
- No searched fatal markers, but the route later black-idled.

Post-relax proof capture: `scratch/thor-debug/20260520-183741-*`

- Same HIR audit shape:
  `cr_shapes triplet=60/0 signed/unsigned=10/50 gt_eq=50/9 cr6=0/0`
- `8272A3A4 code_size=12196` (`-348` bytes from `12544`)
- `8272A8E8 code_size=5356` (`-152` bytes from `5508`)
- Other hot functions also shrank, for example:
  - `820DBFB0`: `1896 -> 1848`
  - `82395BA0`: `1920 -> 1884`
  - `821CB918`: `1892 -> 1836`
  - `826C72D8`: `4520 -> 4224`
- No searched fatal markers.
- The 40-second screenshot was black and the route idled in the final interval,
  so this is a backend code-size win, not a solved route or FPS win.

## Interpretation

The hot `0xA3C..0xA3E` stores are CR field 6 offsets, but in this function they
come from normal scalar compare triplets:

- `0xA3C`: `COMPARE_ULT` / `COMPARE_SLT`
- `0xA3D`: `COMPARE_UGT` / `COMPARE_SGT`
- `0xA3E`: `COMPARE_EQ`

The previous peephole matched only when those compare values were used solely by
their adjacent `STORE_CONTEXT` instructions. Blue Dragon's HIR keeps later uses
alive, so the strict guard discarded all 60 triplets. That guard was overly
conservative for codegen because the fused path still emits the compare values
into their assigned A64 registers.

## Decision

Keep the relaxed CR compare-store lowering. It is route-clean enough for the
current Thor evidence and gives a measurable hot-function shrink.

Do not treat it as a performance breakthrough yet. Body ticks for `8272A3A4`
remain noisy and the route still black-idles, so the next larger win probably
comes from the remaining 255 context loads, 442 context stores, 85 context
barriers, and repeated hot GPR slots rather than another single compare
peephole.

## Next

- Add a state-cache experiment for the hottest GPR slots in `8272A3A4`, with
  explicit context-barrier flush rules.
- Add a cheap counter for how many relaxed CR triplet/pair peepholes fire at
  compile time, so future route captures can separate HIR shape counts from
  actual emitted-code hits.
- Keep final speed captures trace-light; use the CR-shape audit only for
  translation evidence.
