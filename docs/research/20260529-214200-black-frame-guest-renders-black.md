# Black-Frame: Guest Renders Black (resolve+present plumbing all OK) (iter 11)

## Result (definitive narrowing)

For the boots-but-black class, the GPU **plumbing is entirely correct** — the
frontbuffer genuinely contains opaque black because the guest's rendered color is
black. This rules out present, resolve-targeting, and resolve-selection.

## Evidence (Lost Odyssey, deterministic)

`game-pass-lo-resolveprobe-20260529-214009`, `vulkan_trace_resolve` +
`vulkan_trace_resolve_checksum`:

- 205 resolves; every one targets the frontbuffer:
  `vulkan copy dest_base=1F4F8000 extent_start=1F4F8000 raw_rb_dest=1F4F8000
  coord=1280x720 dest_format=6 dest_pitch=1280 shader=Fast32bpp1x2xMSAA`.
- Resolved content checksum: early frames all-zero (`first=00000000...`), later
  frames `nonzero=1800 first_sample=FF000000 first_sample_matches=1797
  clear_like=true low_variation=true` -> the frontbuffer is filled with **opaque
  black `FF000000`** (format 6 = 32bpp, alpha FF + RGB 000000).

So the resolve correctly writes the EDRAM render target into the frontbuffer the
presenter samples — but that render target is black.

`game-pass-lo-scoredresolve-20260529-214207`: enabling
`vulkan_present_scored_resolve_on_swap` + `vulkan_present_scored_resolve_reject_clear_like`
did NOT change it (still `near_black=1`) -> there is no better non-clear-like
resolve to present; all resolves are black.

## Conclusion

Combined with iter 9 (forced solid output works) and iter 10 (healthy XE_SWAP):

- Present / swapchain / composition: OK.
- Output geometry: OK (front 1280x720, output 1920x1080@0,0).
- EDRAM->frontbuffer resolve: OK (targets 1F4F8000, lands).
- Resolve selection: not the issue (no non-black candidate).
- **Root cause: the guest's rendered EDRAM color is black** — upstream in guest
  draws / pixel-shader output / EDRAM render-target content. Deep and likely
  title-variable (Gears 3 renders fine, so it is specific to these titles), NOT a
  cheap present/resolve fix.

## Next options

- Deeper (this lane): trace guest draw + shader output on a black title -
  `vulkan_trace_draw_state`, `vulkan_trace_shader_constants`,
  `vulkan_debug_pixel_shader_output_mode/_filter`,
  `vulkan_force_2101010_rgba8_fallback` / `vulkan_force_signed_2101010_unorm_fallback`
  (format-fallback toggles hint at a known 2101010 format issue) - to see if draws
  produce color or a shader/format bug yields black. Compare against Gears 3.
- Higher-ROI pivot (since black-frame is now a deep lane): push Gears 3 (which
  renders) into actual campaign gameplay; or the UMA upload-counter; or the A64
  stackpoint balance.
