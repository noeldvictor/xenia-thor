# A64 Offset Address Fastpath Quiet A/B

## Result

The same-APK ON/OFF/ON sandwich for
`arm64_offset_memory_address_fastpath` is route-clean and shows a local
generated-code/body-time improvement in the hot A64 cluster. It does not prove
sustained 30 FPS or a route-wide FPS win.

Keep the cvar globally default-off. It is reasonable to enable it explicitly in
future Blue Dragon speed captures, but do not rerun this same A/B unchanged.

## Captures

All captures used commit `5ffe7d20b` with the same APK SHA256:

`3195B62E66ABCE289187F6A0C4A67E621AE2A32931BBD98B790CD32CB0E33AB9`

Route and settings were matched:

- Target: Blue Dragon Disc 1 from the Thor SD-card path.
- Duration: 180 seconds.
- Delayed body-time comparators: `82282490,82281D28,82287788`.
- Audit, disassembly, block body-time, and call-edge tracing: off.
- Screenshots reached the visible opening sky / dragon-wing frame.
- Focused fatal-marker searches found no AndroidRuntime, tombstone, assert,
  backtrace, fatal exception, SIGSEGV, SIGABRT, crash, or ANR rows.

| Capture | Cvar | Prefix |
| --- | --- | --- |
| ON-1 | true | `scratch/thor-debug/20260526-073440-*` |
| OFF | false | `scratch/thor-debug/20260526-074634-*` |
| ON-2 | true | `scratch/thor-debug/20260526-075015-*` |

## Final Perf

The route remains CPU/JIT-bound.

| Capture | Main Thread | XMA Decoder | GPU Commands | Draw Thread |
| --- | ---: | ---: | ---: | ---: |
| ON-1 | `100%` | `11.5%` | `7.6%` | `3.8%` |
| OFF | `92.3%` | `50.0%` | `11.5%` | `3.8%` |
| ON-2 | `92.3%` | `15.3%` | `11.5%` | `3.8%` |

The XMA thread percentages vary enough that they should be treated as route
noise for this narrow A64 lowering decision.

## Final Body-Time Rows

`82282490`:

| Capture | body_ticks_total | final entries_delta | ticks_per_entry | code_size |
| --- | ---: | ---: | ---: | ---: |
| ON-1 | `29593983` | `57109` | `146` | `86452` |
| OFF | `31252631` | `56392` | `167` | `87168` |
| ON-2 | `27182211` | `57876` | `142` | `86452` |

`82281D28`:

| Capture | body_ticks_total | final entries_delta | ticks_per_entry | code_size |
| --- | ---: | ---: | ---: | ---: |
| ON-1 | `7630474` | `26904` | `92` | `95724` |
| OFF | `8049998` | `26904` | `92` | `96908` |
| ON-2 | `7527649` | `27612` | `93` | `95724` |

`82287788`:

| Capture | body_ticks_total | final entries_delta | ticks_per_entry | code_size |
| --- | ---: | ---: | ---: | ---: |
| ON-1 | `3430942` | `227085` | `4` | `35712` |
| OFF | `3540296` | `223832` | `5` | `35732` |
| ON-2 | `3338470` | `229593` | `5` | `35712` |

## Decision

The local signal is positive:

- `82282490` code size drops `87168 -> 86452`, and both ON captures have lower
  final body ticks than OFF.
- `82281D28` code size drops `96908 -> 95724`, and both ON captures have lower
  final body ticks than OFF.
- `82287788` code size drops `35732 -> 35712`, with small matching body-time
  movement.

The route-wide signal is not enough to claim a speed win because Main Thread
remains near a full core and no FPS proof exists.

Next useful work:

1. Stop repeating unchanged offset-helper A/B captures.
2. Use `-Arm64OffsetMemoryAddressFastpath true` in future Blue Dragon speed
   captures unless a regression appears.
3. Move to the next structural A64 lane: broader normal-memory addressing
   reductions, additional safe offset/immediate forms, or another hot
   codegen-floor audit backed by route counters.
