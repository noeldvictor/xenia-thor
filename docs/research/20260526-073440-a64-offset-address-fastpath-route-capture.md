# A64 Offset Address Fastpath Route Capture

## Result

Default-off `arm64_offset_memory_address_fastpath` is route-clean on Thor for
the Blue Dragon opening route.

This is route-safety and codegen evidence only. It is not a quiet speed A/B and
does not prove sustained 30 FPS.

## Capture

- Commit: `1ed1dec66`
- APK SHA256:
  `3195B62E66ABCE289187F6A0C4A67E621AE2A32931BBD98B790CD32CB0E33AB9`
- Capture prefix: `scratch/thor-debug/20260526-073440-*`
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`
- Cvar: `arm64_offset_memory_address_fastpath=true`
- Route length: 180 seconds
- Delayed body-time comparators: `82282490,82281D28,82287788`

Artifacts:

- Log: `scratch/thor-debug/20260526-073440-speed-logcat.txt`
- Filtered log:
  `scratch/thor-debug/20260526-073440-speed-logcat-filtered.txt`
- Metadata: `scratch/thor-debug/20260526-073440-meta.txt`
- Screenshot: `scratch/thor-debug/20260526-073440-screenshot.png`
- Final perf: `scratch/thor-debug/20260526-073440-perf-final.txt`

## Route Evidence

The screenshot reached the visible opening sky / dragon-wing frame. Focused
fatal-marker search found no AndroidRuntime, tombstone, assert, backtrace,
fatal exception, SIGSEGV, SIGABRT, crash, or ANR rows.

Final perf remained CPU/JIT-heavy:

- Main Thread: `100%`
- XMA Decoder: `11.5%`
- GPU Commands: `7.6%`
- Draw Thread: `3.8%`

## Codegen / Body-Time Notes

The enabled path reduced generated code size in the wall cluster versus the
latest route-clean baseline `scratch/thor-debug/20260526-061637-*`:

- `82281D28`: `119908 -> 95724`
- `82282490`: `87168 -> 86452`
- `82287788`: `35732 -> 35712`

Final delayed body-time rows for the enabled capture:

- `82282490`: `body_ticks_total=29593983`, `entries_delta=57109`,
  `ticks_per_entry=146`, `code_size=86452`
- `82281D28`: `body_ticks_total=7630474`, `entries_delta=26904`,
  `ticks_per_entry=92`, `code_size=95724`
- `82287788`: `body_ticks_total=3430942`, `entries_delta=227085`,
  `ticks_per_entry=4`, `code_size=35712`

Do not compare these body-time totals directly against older captures as a
speed claim. The route position and dynamic entry mix differ enough that this
needs a matched same-APK quiet A/B.

## Decision

Keep `arm64_offset_memory_address_fastpath` default-off globally.

Next useful slice is a same-APK quiet A/B or control sandwich on commit
`1ed1dec66` / APK SHA
`3195B62E66ABCE289187F6A0C4A67E621AE2A32931BBD98B790CD32CB0E33AB9`, comparing:

- control: `-Arm64OffsetMemoryAddressFastpath false`
- fastpath: `-Arm64OffsetMemoryAddressFastpath true`

Use the same Blue Dragon speed route, delayed body-time comparators
`82282490,82281D28,82287788`, audit/disassembly/block tracing off, and clean
fatal-marker searches. Only claim speed if the matched captures show a route-wide
improvement beyond route noise.
