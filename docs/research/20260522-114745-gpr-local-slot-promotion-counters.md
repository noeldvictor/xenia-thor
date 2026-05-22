# GPR Local-Slot Promotion Counters

Date: 2026-05-22 11:47 EDT
Branch: `master`
Starting HEAD: `463fb9426`

## Goal

Instrument the default-off `arm64_context_promotion_gpr_local_slots` probe so
the next speed decision is based on exact promotion counts instead of just
route timing and code size.

## Change

Added `arm64_context_promotion_gpr_local_slots_audit`, also default-off.

When both the local-slot experiment and audit are enabled, the
`ContextPromotionPass` logs one summary for the filtered function:

- block and dominated-predecessor counts;
- predecessor state hits and seeded values;
- promoted GPR load/store counts;
- context loads seeded from memory;
- context loads replaced;
- HIR `LOAD_LOCAL` and `STORE_LOCAL` insertions;
- volatile/overlap resets and no-tail skipped stores;
- per-slot counts for `r[1]` and `r[11]`.

Android launch plumbing and `tools/thor/thor_xenia_debug.ps1` can now route the
audit flag.

## Validation

Builds:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

All three passed. `FullDeploy` installed successfully on `c3ca0370`.

Audit capture:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 150 -PerfSampleSeconds "120,145" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64PermuteI32ZipFastpath true -Arm64ContextPromotionGprLocalSlots true -Arm64ContextPromotionGprLocalSlotsFunction 0x82282490 -Arm64ContextPromotionGprLocalSlotsAudit true -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Artifacts:

- Logcat: `scratch/thor-debug/20260522-114838-speed-logcat.txt`
- Filtered logcat: `scratch/thor-debug/20260522-114838-speed-logcat-filtered.txt`
- Metadata: `scratch/thor-debug/20260522-114838-meta.txt`
- Screenshot: `scratch/thor-debug/20260522-114838-screenshot.png`
- APK SHA-256:
  `51A2A306AF8909BD0E2A6945AE91655BA40C9FE4B53FA17753F1C715B17CFF80`

The screenshot reached the visible opening sky/dragon-wing route again. The
fatal-marker search found no matches for Android fatal crashes, guest crash
markers, GPU hangs, or Vulkan device loss.

## Counter Result

For `82282490`:

```text
blocks=165
dominated_blocks=47
pred_state_hits=40
pred_values_seeded=17
loads_seen=174
loads_seeded=171
loads_replaced=3
local_loads=3
stores_seen=285
stores_tracked=285
local_stores=103
volatile_resets=142
overlap_resets=0
stores_skipped_no_tail=0
```

Per slot:

```text
r1  loads/seeded/replaced/local_loads stores/tracked/local_stores/overlap_resets = 107/104/3/3 11/11/35/0
r11 loads/seeded/replaced/local_loads stores/tracked/local_stores/overlap_resets = 67/67/0/0 274/274/68/0
```

Body-time samples still showed `code_size=87660`:

```text
11:50:54 body_ticks_delta=4099690 entries_delta=18337 ticks_per_entry=223
11:51:09 body_ticks_delta=5115492 entries_delta=38016 ticks_per_entry=134
```

## Read

The local-slot bridge is doing the wrong kind of work for this hot path. It
emits 103 local stores and only replaces 3 loads. `r[11]` is especially bad:
68 local stores produce zero replaced loads. `r[1]` is the only slot with any
replacement signal, but 35 stores for 3 loads is still a losing shape.

The likely reason is visible in the counters: `82282490` has lots of volatile
reset points and limited dominated-predecessor reuse. Local HIR memory is too
expensive as a bridge unless the promotion can replace a much larger number of
context loads.

## Decision

Keep `arm64_context_promotion_gpr_local_slots` default-off. Do not spend the
next slice tuning this local-slot shape. The next state-cache experiment should
try a lower-overhead pinned-GPR path, starting with `r[1]` only, with explicit
flush/reset behavior for helpers, exits, exceptions, conditional branches,
returns, traps, multi-predecessor joins, volatile ops, and overlapping context
writes.
