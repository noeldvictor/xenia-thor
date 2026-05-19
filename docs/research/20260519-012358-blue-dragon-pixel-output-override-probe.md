# Blue Dragon Pixel Output Override Probe

Date: 2026-05-19 01:23 EDT

Fork status: experimental xenia-thor research fork only. This is not official
Xenia compatibility data and should not be sent upstream as a support request.

## Question

The previous targeted shader-filter trace showed `0ABADD9DA4373CBA` sampling
nonzero or clear-like inputs and resolving `1DC14000` to zero. The next question
was whether the pixel shader / color export path itself was incapable of writing
nonzero data, or whether a later pass was erasing the target.

## Change

Added a research-only Vulkan pixel shader output override:

- `vulkan_debug_pixel_shader_output_filter`: comma-separated pixel shader ucode
  hashes.
- `vulkan_debug_pixel_shader_output_mode`:
  - `0`: disabled.
  - `1`: force matched pixel shader color exports to magenta RGBA.
  - `2`: keep RGB and force alpha to 1.

The flags are routed through Android `EmulatorActivity` launch extras and
`tools/thor/thor_xenia_debug.ps1`, and the swap trace logs the active filter and
mode.

## Evidence

Build and device validation:

- Branch: `master`, dirty head during capture: `7f66f5207`.
- Device: AYN Thor Max, serial `c3ca0370`.
- APK SHA-256:
  `B92175A0BC538E8284C2C033B595AD9FBF1F9A959D217CDB82EEF98C7796E633`.
- Capture:
  - `scratch/thor-debug/20260519-011906-live-logcat.txt`
  - `scratch/thor-debug/20260519-011906-live-logcat-filtered.txt`
  - `scratch/thor-debug/20260519-011906-meta.txt`
  - `scratch/thor-debug/20260519-011906-screenshot.png`
- Launch included:
  `-VulkanDebugPixelShaderOutputFilter 0ABADD9DA4373CBA`
  `-VulkanDebugPixelShaderOutputMode 1`
  `-VulkanForceSigned2101010UnormFallback true`
  plus focused draw, texture-source, copy, resolve, and frontbuffer tracing.
- Log proof that the cvars reached the native side:
  `debug_ps_output_filter=0ABADD9DA4373CBA debug_ps_output_mode=1`.
- No fatal signal, AndroidRuntime crash, GPU hang, or ARM64 execution fallback
  appeared in the capture.

The Adreno signed `2_10_10_10` fallback was active:

- `VulkanTextureCache: forcing signed 2_10_10_10 textures through UNORM fallback`

That fallback alone did not fix the black output in the preceding
`20260519-011132` run.

## Result

The forced output worked.

Immediately after the targeted `0ABADD9DA4373CBA` draw, the resolve of
`1DC14000` produced nonzero data:

- `first_sample=FF03F0FF`
- `first_sample_matches=1800`
- `first_nonzero_value=FF03F0FF`

The exact packed value is not the 8-bit `FF00FFFF` magenta expected for a simple
RGBA8 target, but that is reasonable for this render target format path. The
important fact is that the resolved target became nonzero after forcing the
shader export.

Then a later copy / resolve sequence to the same `raw_dest_base=1DC14000` erased
it again:

- `first_sample=00000000`
- `first_nonzero=-1`
- scored source for `1DC14000+00398000`: `score=0 nonzero=0 varying=0`

The final `9567C79307ACC6F5` blit then sampled `1DC14000` after it had already
been zeroed, and the official frontbuffers stayed zero.

## Conclusion

The `0ABADD9DA4373CBA` shader/export path is not the immediate black-frame wall.
It can write nonzero color into the expected render target when overridden.

The current wall moved one step later: a post-`0ABADD9DA4373CBA` copy/draw/resolve
sequence is clearing or overwriting `1DC14000` before the final `9567C79307ACC6F5`
fullscreen blit. The next GPU lane should identify the exact writer that turns
`1DC14000` from `FF03F0FF` back to zero, preferably by tagging copy/resolve
trace lines with recent draw sequence and shader hashes.

## Next Actions

- Add a narrow "recent draw/copy causal trace" around writes to `1DC14000`.
- Run a quick control probe forcing `9567C79307ACC6F5` output to prove the final
  blit/frontbuffer chain can still show a targeted shader override.
- Keep the signed `2_10_10_10` fallback available as a comparison knob, but do
  not treat it as a fix.
