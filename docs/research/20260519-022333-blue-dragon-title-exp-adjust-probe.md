# Blue Dragon Title Exp-Adjust Probe

Date: 2026-05-19 02:23 EDT

Repo: `xenia-thor`

Branch: `master`

Base commit during captures: `b27292e4a`

Device: AYN Thor Max, ADB serial `c3ca0370`

Target:
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

The previous proof isolated the visible black frame to the unforced
`B02CC5F55AD0D140` shader pass. This note asks whether the texture sample is
actually black, or whether the sampled value is being darkened after sampling.

## Tooling Added

- Secondary shader-output filter/mode so one shader can be forced while another
  is probed in the same run.
- Pixel shader output modes:
  - `10`: last post-processed texture fetch, with a yellow missing-var sentinel.
  - `11-14`: individual post-processed fetch components.
  - `15`: post-processed fetch nonzero predicate.
  - `20`: final fetch coordinates.
  - `30`: raw unsigned sample.
  - `31`: raw signed sample.
- `vulkan_force_2101010_rgba8_fallback` for a raw RGBA8 host-format probe.
- `vulkan_debug_texture_fetch_disable_exp_adjust` to force texture fetch result
  exponent adjustment to `1.0` for research only.

## Captures

### `scratch/thor-debug/20260519-021201-*`

APK SHA-256:
`6BEB77105169D10B803AA8DE150AAC90D7BF5F045097A4617EFF9E1C1EE05635`

Key flags:

- `-VulkanForceSigned2101010UnormFallback true`
- `-VulkanDebugPixelShaderOutputFilter 57B736C8B5D4E953`
- `-VulkanDebugPixelShaderOutputMode 1`
- `-VulkanDebugPixelShaderOutputSecondaryFilter B02CC5F55AD0D140`
- `-VulkanDebugPixelShaderOutputSecondaryMode 10`

Result:

- Screenshot remained black.
- The yellow sentinel did not appear, proving the B02 debug capture variable
  existed.
- Therefore the last B02 post-processed texture fetch result was effectively
  zero.

### `scratch/thor-debug/20260519-021355-*`

APK SHA-256:
`6BEB77105169D10B803AA8DE150AAC90D7BF5F045097A4617EFF9E1C1EE05635`

Key flags:

- Same as above, but `-VulkanDebugPixelShaderOutputSecondaryMode 30`.

Result:

- Screenshot turned red/nonzero.
- B02 output fed the final blit:
  `9567C79307ACC6F5` sampled `1DC14000` with nonzero `first_sample=FF03F0FF`,
  and the final shared-memory swap checksum became nonzero.
- The raw unsigned texture sample is nonzero. The black result is introduced by
  Xenos post-processing after sampling.

### `scratch/thor-debug/20260519-022127-*`

APK SHA-256:
`6B283A738F3BAE475965BF48FAB63366ED630D2604048EB4F2837D28BD6251B7`

Key flags:

- `-VulkanForceSigned2101010UnormFallback true`
- `-VulkanDebugTextureFetchDisableExpAdjust true`
- No shader-output force modes.

Result:

- Blue Dragon reached the title prompt on the AYN Thor.
- Screenshot:
  `scratch/thor-debug/20260519-022127-screenshot.png`
- Visible text included `press START` and copyright text.
- The official swap shared-memory checksum was nonzero:
  `first_sample=FF000000`, `nonzero=1780`.
- No fatal signal, AndroidRuntime crash, GPU hang, or ARM64 fallback was found
  in the capture.

## Findings

For B02, the texture-source trace shows nonzero format-7 source data:

- `1DC14000`, `fmt=7`, `first_sample=000000C0` in the title-reaching run.
- Earlier forced/debug runs also observed nonzero `FF03F0FF` after the upstream
  pass was forced.

The relevant B02 fetch constant word has result exponent adjustment bits that
decode to `-16` (`0x003E0003 >> 13 & 0x3F == 48`, sign-extended to `-16`).
With the temporary Adreno `2_10_10_10` fallback, the raw Vulkan sample is
already normalized around `0..1`; multiplying by `2^-16` makes the result
quantize back to black in the `2_10_10_10_FLOAT` render chain.

Disabling texture result exponent adjustment globally is not correct emulation,
but it proves the immediate title-screen wall is the `2_10_10_10_FLOAT`
resolve/fetch decode path, not Android presentation or raw Adreno texture
sampling.

## Decision

Keep `vulkan_debug_texture_fetch_disable_exp_adjust` as a default-off research
knob only. The proper next fix is to implement or route a correct Xenos
`2_10_10_10_FLOAT` / 7e3 decode path for resolved render-target textures on
Android/Adreno, then remove the need for global exponent-adjust suppression.

## Next Actions

- Replace the coarse exponent-adjust bypass with a targeted 7e3-aware texture
  decode path for `2_10_10_10_FLOAT` render-chain data.
- Add a title smoke script preset that launches Blue Dragon with the temporary
  knob and captures a screenshot/log proof.
- Test pressing START after title with HID nop buttons once the title prompt is
  reproducible.
