# 20260526-012000 - VMX128 Route-Stabilized Counters

## Question

After the call-edge capture showed `82282490` and `82281D28` dominated by
self-recursive child body time, should the sprint patch broad VMX128
`PERMUTE` / `LOAD_VECTOR_SHL` / `LOAD_VECTOR_SHR` behavior next?

## Capture

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -DisassembleFunctionFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBlockFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBlockBodyTime true -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 30 -Arm64SpeedProfileMinDelta 1 -StopAppAfterCapture true
```

- Capture prefix: `scratch/thor-debug/20260526-011255-*`.
- Capture commit: `7aa4b90d4`.
- APK SHA256:
  `4B3B48CD3DB69B093CF3D47FA84BBC211DD09D23063F08DDD0064496FF6AC356`.
- Route evidence: visible opening sky / dragon-wing screenshot.
- Fatal-marker search:

```powershell
rg -n -i "AndroidRuntime|FATAL EXCEPTION|tombstone|assert|backtrace|SIGSEGV|SIGABRT|\bcrash\b|\bANR\b|fatal exception" scratch\thor-debug\20260526-011255-speed-logcat.txt
```

The search returned no matches.

This was a trace-heavy counter/disassembly capture, not a quiet speed proof.
The capture does not prove sustained 30 FPS.

## Perf And Body-Time

Final perf remained CPU/JIT-heavy:

- Main Thread: `92.3%`
- GPU Commands: `11.5%`
- Draw Thread: `7.6%`
- XMA Decoder: `53.8%` in the final top sample

Final delayed function body-time rows:

| Function | body_ticks_total | entries_delta | ticks_per_entry | code_size |
| --- | ---: | ---: | ---: | ---: |
| `82282490` | `98058145` | `62328` | `525` | `107192` |
| `82281D28` | `67536752` | `29736` | `810` | `119908` |
| `82287788` | `14590679` | `247053` | `20` | `47044` |

Final hot block rows were dominated by scalar/control or call-heavy blocks:

- `82282490:822825E0`: `body_ticks_total=70762934`,
  `entries_delta=289212`, `ticks_per_entry=81`.
- `82281D28:8228233C`: `body_ticks_total=48139529`,
  `entries_delta=24066`, `ticks_per_entry=713`.
- `82282490:8228252C`: `body_ticks_total=16310678`,
  `entries_delta=246624`, `ticks_per_entry=23`.
- `82287788:822877BC`: `body_ticks_total=1318925`.
- `82287788:82287D10`: `body_ticks_total=531338`.

## Vector Shape Reports

The all-function HIR dump overflowed logcat. The current capture only retained
partial `82282490`, no usable `82281D28` HIR, and a usable `82287788` HIR. To
avoid mixing partial static counts with current dynamic route data:

- `82282490` uses the full older HIR dump from
  `scratch/thor-debug/20260521-170941-speed-logcat.txt` joined to the current
  block-profile log.
- `82281D28` uses the full older HIR dump from
  `scratch/thor-debug/20260524-050427-speed-logcat.txt` joined to the current
  block-profile log.
- `82287788` uses the current capture HIR and current block-profile log.

Commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_vector_shape_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -BlockProfileLog scratch\thor-debug\20260526-011255-speed-logcat.txt -Top 30
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_vector_shape_report.ps1 -LogPath scratch\thor-debug\20260524-050427-speed-logcat.txt -Function 82281D28 -Phase OptHIR -BlockProfileLog scratch\thor-debug\20260526-011255-speed-logcat.txt -Top 20
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_vector_shape_report.ps1 -LogPath scratch\thor-debug\20260526-011255-speed-logcat.txt -Function 82287788 -Phase OptHIR -BlockProfileLog scratch\thor-debug\20260526-011255-speed-logcat.txt -Top 30
```

Weighted route counters:

| Function | weighted PERMUTE | weighted LOAD_VECTOR_SHL | weighted LOAD_VECTOR_SHR | weighted PACK | weighted UNPACK |
| --- | ---: | ---: | ---: | ---: | ---: |
| `82282490` | `2524413` | `1682942` | `0` | `0` | `0` |
| `82281D28` | `1317189` | `878126` | `0` | `0` | `0` |
| `82287788` | `7353192` | `700304` | `2801216` | `0` | `0` |

Interpretation:

- `PACK` remains absent and `UNPACK` remains zero weighted on the live route.
- In `82282490` and `82281D28`, the weighted `PERMUTE` /
  `LOAD_VECTOR_SHL` volume is concentrated in the already-closed
  stvewx/vmaddfp local shapes (`822824F0` / `82282678`), not a broad new
  opcode-family lane.
- `82287788` has real `PERMUTE` / `LOAD_VECTOR_SHR` volume, but the active
  vector block `82287D10` is a small body-time slice (`531338`) compared with
  scalar/control entry and branch blocks in the same function.

## Codegen Floor Checks

Commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_a64_codegen_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 822825E0 -BlockProfileLog scratch\thor-debug\20260526-011255-speed-logcat.txt -Top 12
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_a64_codegen_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 8228252C -EndGuest 822825C4 -BlockProfileLog scratch\thor-debug\20260526-011255-speed-logcat.txt -Top 12
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_a64_codegen_audit.ps1 -LogPath scratch\thor-debug\20260526-011255-speed-logcat.txt -Function 82287788 -StartGuest 822877BC -EndGuest 82287868 -BlockProfileLog scratch\thor-debug\20260526-011255-speed-logcat.txt -Top 12
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_a64_codegen_audit.ps1 -LogPath scratch\thor-debug\20260526-011255-speed-logcat.txt -Function 82287788 -StartGuest 82287D10 -BlockProfileLog scratch\thor-debug\20260526-011255-speed-logcat.txt -Top 12
```

Selected results:

- `82282490:822825E0-822825F4`: `ppc_count=5`, `hir_count=18`,
  `context_barriers=2`, `calls=1`, `branches=1`,
  `estimated_floor_total=17`. HIR is mainly `store_context`, barriers,
  compares, and a recursive call.
- `82282490:8228252C-822825C4`: `ppc_count=38`, `hir_count=129`,
  `context_barriers=3`, `calls=1`, `estimated_floor_total=192`. This is the
  previously closed stvewx/vmaddfp/state-heavy shape.
- `82287788:822877BC-82287868`: `ppc_count=43`, `hir_count=138`,
  `context_barriers=9`, `calls=1`, `branches=8`,
  `estimated_floor_total=125`. This hot slice is scalar/control/context-heavy,
  not vector-heavy.
- `82287788:82287D10-82287D84`: `ppc_count=29`, `hir_count=100`,
  `permute=4`, `load_vector_shr=4`, `estimated_floor_total=130`,
  but only `body_ticks_total=531338` in the current route.

`82281D28:8228233C` still cannot be mapped with the available OptHIR dump:
`StartGuest 8228233C not found in OptHIR dump for 82281D28`. A future
tooling slice should capture that function alone or add file-backed HIR dumps
instead of dumping all three large functions through logcat.

## Decision

Do not patch broad VMX128 `PERMUTE` / `LOAD_VECTOR_SHL` /
`LOAD_VECTOR_SHR` behavior from this capture.

The vector lane has some route volume, but the volume is either:

- inside closed local stvewx/vmaddfp shapes that already failed to prove speed,
  or
- inside smaller `82287788` vector blocks that are not the dominant body-time
  wall.

The live wall remains scalar/control/context-barrier generated code around
recursive call blocks and entry dispatch blocks, especially `822825E0`,
`8228233C`, `8228252C`, and `822877BC`.

## Next Useful Slice

Prefer a hot-block codegen-floor/source audit before more behavior changes:

1. Make HIR dump capture reliable for one large function at a time, or add a
   file-backed HIR dump path so logcat does not truncate static context.
2. Recover `82281D28:8228233C` OptHIR and PPC span context.
3. Compare current-route hot blocks by instruction mix, context-barrier count,
   call/return cost, load/store context count, and generated A64 floor.
4. Only then consider a reusable backend rule, such as context-barrier
   preservation, scalar branch/CR lowering cleanup, helper ABI cleanup, or
   direct-call stackpoint reduction.

Do not run another VMX128 behavior A/B, unchanged call-edge capture, or
generic recursive-call probe from this evidence.
