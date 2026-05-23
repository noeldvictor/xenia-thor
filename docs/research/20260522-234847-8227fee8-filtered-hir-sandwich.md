# 8227FEE8 Filtered HIR Sandwich

Date: 2026-05-22 23:48 EDT

## Goal

Use the patched warning-level filtered dump lane to capture usable `8227FEE8`
HIR in the same log-level-1 speed lane that can still reach the Blue Dragon
opening route on AYN Thor.

## Setup

- Branch: `master`
- HEAD: `b0b11bb00`
- APK SHA256: `E92DAC2CB4E7080C196DB9656305F372DC20C189E7697A2FCCD47D1E12DA3FA3`
- Device: `c3ca0370`
- Game path:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`
- Baseline toggles:
  - `arm64_context_promotion_gpr_livein_r1=false`
  - `arm64_speed_profile_body_time_filter=82282490,8227FEE8`
  - `arm64_speed_profile_body_time_after_ms=120000`
  - `arm64_speed_profile_thread_snapshot_on_idle=true`
  - `log_level=1`

## Captures

Control before this slice was the patched no-filter route recheck:
`scratch/thor-debug/20260522-232133-*`. It reached the visible opening sky/wing
route with no searched fatal markers and measured `82282490` as dominant with
`8227FEE8` secondary.

Filtered middle capture:
`scratch/thor-debug/20260522-233545-*`.

- Command used `-DisassembleFunctionFilter 8227FEE8` plus delayed body-time for
  `82282490,8227FEE8`.
- The route reached the visible opening sky/wing scene.
- Fatal-marker search was clean.
- The filtered warning-level HIR dump appeared in the normal speed lane.
- Body-time activated after `120001ms`.
- Final body-time rows:
  - `82282490`: `body_ticks_total=26728115`,
    `body_ticks_delta=7697964`, `entries_delta=63812`,
    `ticks_per_entry=120`, `code_size=87168`
  - `82281D28`: `body_ticks_total=7075712`,
    `entries_delta=30444`, `ticks_per_entry=92`, `code_size=96908`
  - `8227FEE8`: `body_ticks_total=4125095`,
    `body_ticks_delta=520659`, `entries_delta=1965`,
    `ticks_per_entry=264`, `code_size=49804`
- Final perf sample still showed a CPU wall, not a GPU wall:
  Main Thread about `92.3%`, XMA about `7.6%`, GPU Commands about `7.6%`.

Post-control no-filter capture:
`scratch/thor-debug/20260522-234038-*`.

- Same APK and delayed body-time, but no disassembly filter.
- It black-idled before opening route progress.
- `entry_delta=0` from the first reported samples onward.
- Body-time activated after `120001ms`, but no `82282490` or `8227FEE8`
  body-time rows appeared.
- Fatal-marker search was clean.
- Final screenshot was black.
- The new idle owner attribution fired but missed the native owner cache:
  `last_global_owner_sys_tid=7347 owner_hint=miss owner_hint_tid=00000000
  owner_hint_handle=00000000 owner_hint_state=zombie`.

## HIR Findings For 8227FEE8

`tools/thor/thor_hir_hotpath_report.ps1` on the route-clean filtered capture:

- `hir_lines=6806`
- `store_context=1426`
- `load_context=896`
- `context_barrier=332`
- `branches=284`
- `calls=78`
- `permute=48`
- `byte_swaps=30`
- no `dot_product_3`, `dot_product_4`, `extract`, `splat`, `stvewx`, or
  `mul_add`

Context traffic shape:

- Load classes: `GPR 817`, `VMX 29`, `FPR 27`, `FP/VMX_STATUS 16`, `CR 5`,
  `LR/CTR 2`
- Store classes: `GPR 633`, `CR 592`, `LR/CTR 83`, `FPR 62`, `VMX 31`,
  `FP/VMX_STATUS 16`, `XER 9`
- Top loads: `r[1]=198`, `r[11]=84`, `r[31]=63`, `r[24]=59`, `r[19]=48`,
  `r[29]=48`, `r[23]=44`, `r[30]=38`, `r[28]=33`, `r[10]=26`
- Top stores: `r[11]=185`, CR6 triplet each `169`, `r[10]=84`, LR `77`,
  `r[3]=74`, `r[4]=59`, `r[5]=39`, `r[9]=37`

State-span report:

- `spans=485`
- `context_barriers=332`
- Hottest context-access spans:
  - `loc_82280A68#after_barrier_105`: `loads=68`, `stores=39`,
    `branches=13`, total `107`
  - `loc_82280E1C`: `loads=56`, `stores=40`, `branches=12`, total `96`
  - `loc_82281B7C#after_barrier_318`: `loads=18`, `stores=18`,
    `branches=5`, total `36`
- Candidate GPR cache plan scores: `r[1]=392`, `r[11]=232`, `r[31]=127`,
  `r[24]=119`, `r[29]=113`, `r[19]=98`, `r[23]=89`, `r[30]=83`

Vector shape report:

- `blocks=152`
- `instructions=6805`
- `permute=48`
- `load_vector_shl=16`
- `load_vector_shr=32`
- No dynamic extract/splat/stvewx surface.
- Static vector-heavy blocks are:
  - `82280A68`: `perm=24`, `load_vector_shl=8`,
    `load_vector_shr=16`, PPC shape includes `lvlx:4`, `lvrx:4`,
    `stvlx:4`, `stvrx:4`, `vor:4`
  - `82280E1C`: `perm=24`, `load_vector_shl=8`,
    `load_vector_shr=16`, PPC shape includes `lvlx:4`, `lvrx:4`,
    `stvlx:4`, `stvrx:4`, `vor:4`

## Interpretation

The filtered middle run proves the patched warning-level dump lane can produce
usable `8227FEE8` HIR without forcing `-LogLevel 0`. The post-control black-idle
means route nondeterminism is still present and is not solely caused by the
disassembly filter. Do not blame the filtered HIR capture for the post-control
idle without more evidence.

`8227FEE8` is not a dot-product or `stvewx` target. Its first obvious cost
shape is generic GPR/CR state traffic with many barriers and branches. The only
vector-specific static target is paired `lvlx/lvrx` and `stvlx/stvrx` lowering
in `82280A68` and `82280E1C`, but those blocks still need body-time proof before
we write a vector peephole.

The r1 state-cache signal is strong in `8227FEE8`, but previous `82282490` r1
live-in work got replacement counts without a quiet speed win. Do not start a
broad GPR cache patch from this report alone. If state caching is attempted
again, make it function/block-gated and prove block body-time first.

## Next Action

Run a delayed route-stabilized block body-time capture for `8227FEE8`, ideally
without a new HIR dump unless needed:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLiveInR1 false -Arm64SpeedProfileBodyTimeFilter "8227FEE8" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileBlockFilter "8227FEE8" -Arm64SpeedProfileBlockBodyTime true -Arm64SpeedProfileThreadSnapshotOnIdle true -StopAppAfterCapture true
```

Use that capture to decide whether the next patch should target:

- `82280A68` / `82280E1C` vector load/store-pair lowering;
- a function/block-gated `8227FEE8` GPR state traffic reduction;
- route-stability instrumentation if the run black-idles again.

If it black-idles, inspect `owner_hint` first. The `owner_hint=miss` result from
`20260522-234038` suggests the blocking native owner can be outside the current
XThread hint cache, so the next idle-attribution patch should map host or
global-critical owners, not guest codegen behavior.
