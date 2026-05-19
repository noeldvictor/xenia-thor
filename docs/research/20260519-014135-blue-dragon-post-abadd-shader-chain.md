# Blue Dragon Post-0ABADD Shader Chain

Access date: 2026-05-19 01:41 EDT

## Question

After the `0ABADD9DA4373CBA` shader can be forced to write nonzero color into
`1DC14000`, which later draw actually turns the chain back to zero, and is the
final Android/Vulkan present path still suspect?

## Evidence

Build under test:

- Branch: `master`
- Metadata head: `1e10cfb19`, dirty with copy/draw trace tags
- APK SHA-256:
  `44B87E30B1141C339D5CC18C9BDBDCFE866D85F1B1E2EB2DA1250D2343708A19`
- Device: AYN Thor Max, ADB serial `c3ca0370`
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

New instrumentation:

- `IssueDraw` records an incrementing draw sequence and the most recent vertex
  and pixel shader hashes.
- `IssueCopy` records an incrementing copy sequence and prints the most recent
  draw sequence and shader hashes in both the begin and resolve-result log
  lines.

Captures:

| Capture | Forced pixel shader | Key result |
| --- | --- | --- |
| `scratch/thor-debug/20260519-013208-*` | `0ABADD9DA4373CBA` | `copy_seq=20` / `0ABADD` writes `1DC14000` nonzero (`FF03F0FF`), then `copy_seq=21` / `57B736C8B5D4E953` writes `1DC14000` back to zero. |
| `scratch/thor-debug/20260519-013500-*` | `57B736C8B5D4E953` | Forced `57B` writes `1DC14000` nonzero and the screenshot shows a visible dark-blue debug frame. The next unforced `B02CC5F55AD0D140` write returns `1DC14000` to zero. |
| `scratch/thor-debug/20260519-013719-*` | `B02CC5F55AD0D140` | Forced `B02` writes `1DC14000` nonzero, final `9567C79307ACC6F5` samples it, resolves `1D14C000` nonzero, then writes the official frontbuffer nonzero. Screenshot shows a bright debug frame. |

No capture above logged a fatal signal, `AndroidRuntime` crash, GPU hang, or
ARM64 execution fallback.

## Trace Chain

The first frame chain in the forced-B02 run is:

1. `0ABADD9DA4373CBA` copy: `copy_seq=20`, writes `1DC14000`.
2. `57B736C8B5D4E953` copy: `copy_seq=21`, writes `1DC14000`.
3. `B02CC5F55AD0D140` copy: `copy_seq=22`, writes `1DC14000`.
4. `9567C79307ACC6F5` copy: `copy_seq=23`, samples `1DC14000` and writes
   `1D14C000`.
5. `9567C79307ACC6F5` copy: `copy_seq=24`, samples `1D14C000` and writes
   `1CA1C000`.

With `B02CC5F55AD0D140` forced to debug color:

- `copy_seq=22` writes `1DC14000` as `FF03F0FF`.
- The final `9567C79307ACC6F5` pass samples that nonzero `1DC14000`.
- `copy_seq=23` writes `1D14C000` as `FFFF00FF`.
- `copy_seq=24` writes frontbuffer `1CA1C000` as `D8800100`.

Therefore the final fullscreen pass, resolve-to-frontbuffer path, and Android
presentation path can carry nonzero data on Thor.

## Frontbuffer Checksum Caveat

The older `GPU swap trace: frontbuffer checksum` line is not a reliable Vulkan
truth source by itself. The generic command-processor checksum reads CPU guest
memory. Vulkan resolve and texture-source traces read the GPU shared-memory
buffer via Vulkan readback paths. For Vulkan present debugging, prefer:

- `-VulkanTraceResolveChecksum true`
- `-VulkanTraceTextureSourceChecksum true`
- `-VulkanTraceCopyState true`
- `-VulkanTraceSwapSharedMemoryChecksum true` when available

Use CPU frontbuffer checksums only as a secondary signal.

## Current Conclusion

The immediate Blue Dragon black-frame wall is now the unforced
`B02CC5F55AD0D140` pixel shader pass, or the exact texture/constant/boolean
state feeding it. This is no longer a broad Android GUI, Vulkan swapchain, final
blit vertex, or final `9567C79307ACC6F5` presenter problem.

Known B02 facts from the captures:

- `tf0`: `1DC14000`, 1280x720, format 7, tiled, the post chain source.
- `tf1`: DXT source around `1F510000` or `1F520000`, 320x180, format 18,
  nonzero and varying.
- Bool constants for B02 appear as `b128=false`, `b129=false`, `b130=true`
  (`values=05500004` in the bool trace).
- The dumped ucode branches through a log/exp/tone-map style path and exports
  with `max oC0`.

Next lane: inspect B02 translation and source-format semantics, especially
format 7 (`2_10_10_10`), texture fetch signedness/normalization, boolean
branching, log/exp/NaN handling, and export clamping.
