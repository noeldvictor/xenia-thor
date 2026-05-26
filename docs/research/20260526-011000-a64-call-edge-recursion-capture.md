# A64 Call-Edge Recursion Capture

Date: 2026-05-26

## Question

The helper/block-link offline audit showed material dispatch volume in the
warmed route but no resolver wall. Does current call-edge profiling point at
direct recursive dispatch overhead, indirect dispatch, extern/helper ABI, or
caller-exclusive generated code?

## Capture

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileCallEdgeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -StopAppAfterCapture true
```

Artifacts:

- Capture prefix: `scratch/thor-debug/20260526-005803-*`
- Log: `scratch/thor-debug/20260526-005803-speed-logcat.txt`
- Filtered log:
  `scratch/thor-debug/20260526-005803-speed-logcat-filtered.txt`
- Metadata: `scratch/thor-debug/20260526-005803-meta.txt`
- Screenshot: `scratch/thor-debug/20260526-005803-screenshot.png`
- Commit: `2131e7922`
- APK SHA256:
  `4B3B48CD3DB69B093CF3D47FA84BBC211DD09D23063F08DDD0064496FF6AC356`
- Route evidence: visible Blue Dragon opening sky / dragon-wing frame.

Fatal-marker search found no Xenia, AndroidRuntime, tombstone, assert,
backtrace, fatal exception, SIGSEGV, SIGABRT, crash, or ANR lines. The only
`Xenia` match was the normal Adreno Vulkan application name.

## Final Perf

Final sample:

- Main Thread: `88.4%`
- GPU Commands: `11.5%`
- XMA Decoder: `7.6%`
- Draw Thread: `0.0%`

This remains CPU/JIT-bound, not broad GPU/Vulkan-bound.

## Final Body-Time Rows

- `82282490`: `body_ticks_total=32874047`, `entries_delta=69748`,
  `ticks_per_entry=185`, `code_size=92056`
- `82281D28`: `body_ticks_total=9434976`, `entries_delta=33276`,
  `ticks_per_entry=128`, `code_size=103876`
- `82287788`: `body_ticks_total=3264491`, `entries_delta=276266`,
  `ticks_per_entry=5`, `code_size=35940`

The code sizes are larger than quiet captures because call-edge profiling was
enabled.

## Call-Edge Parser

Commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_call_path_report.ps1 -LogPath scratch\thor-debug\20260526-005803-speed-logcat.txt -BlockProfileLog scratch\thor-debug\20260526-005803-speed-logcat.txt -Function 82282490 -Top 20
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_call_path_report.ps1 -LogPath scratch\thor-debug\20260526-005803-speed-logcat.txt -BlockProfileLog scratch\thor-debug\20260526-005803-speed-logcat.txt -Function 82281D28 -Top 20
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_call_path_report.ps1 -LogPath scratch\thor-debug\20260526-005803-speed-logcat.txt -BlockProfileLog scratch\thor-debug\20260526-005803-speed-logcat.txt -Function 82287788 -Top 20
```

The run did not include filtered HIR dumps, so these are dynamic-only
call-edge summaries.

## Dynamic Edge Read

### `82282490`

- Function body total: `32874047`
- Dynamic edge body total: `30979388` (`94.24%`)
- Approximate parent-exclusive ticks: `1894659` (`5.76%`)
- Self-recursive edge `822825E0 -> 82282490`:
  `body_ticks_total=22887830`, `calls_total=137601`,
  `body_pct=69.62%`, `ticks_per_call_peak=252`
- Edge `8228252C -> 82287788`:
  `body_ticks_total=4117087`, `calls_total=622431`,
  `body_pct=12.52%`, `ticks_per_call_peak=6`
- Edge `822825C8 -> 8227FEE8`:
  `body_ticks_total=3257689`, `calls_total=10425`,
  `body_pct=9.91%`, `ticks_per_call_peak=437`
- Edge `822824F0 -> 82274DB0`:
  `body_ticks_total=716782`, `calls_total=622431`,
  `body_pct=2.18%`, `ticks_per_call_peak=1`

### `82281D28`

- Function body total: `9434976`
- Dynamic edge body total: `8392330` (`88.95%`)
- Approximate parent-exclusive ticks: `1042646` (`11.05%`)
- Self-recursive edge `8228233C -> 82281D28`:
  `body_ticks_total=6813963`, `calls_total=63715`,
  `body_pct=72.22%`, `ticks_per_call_peak=118`
- Next largest targets:
  `82275C08=740520`, `82274E38=471468`, `822753E0=310781`

### `82287788`

- Function body total: `3264491`
- Dynamic edge body total: `676626` (`20.73%`)
- Approximate parent-exclusive ticks: `2587865` (`79.27%`)
- Edge `822877BC -> 821CE028`:
  `body_ticks_total=676626`, `calls_total=307544`,
  `ticks_per_call_peak=2`

## Decision

This capture does not justify a resolver patch, an unchanged helper ABI
experiment, or another generic recursive-call/stackpoint probe.

The result matches older recursive-edge evidence:

- `82282490` and `82281D28` are dominated by self-recursive child body time.
- The direct recursive edges are inclusive work, not mostly caller-side call
  overhead.
- The useful speed work must target broad local work repeated inside the
  recursive body, or a proven callee-local hot path, not the direct-call edge
  itself.

## Next

Do not rerun this call-edge capture unchanged.

Preferred next lane:

1. Current route-stabilized VMX128 `PERMUTE` and
   `LOAD_VECTOR_SHL` / `LOAD_VECTOR_SHR` counters for the hot cluster before
   any VMX behavior patch.
2. If staying in helper/block-linking, first produce a static HIR/source audit
   showing a reusable direct-call or stackpoint rule beyond the already-rejected
   stack-sync and edge-payload lanes.
3. For `82287788`, only continue if the audit avoids the closed `f1`, fpscr,
   edge-payload, and first-barrier-only lanes.
