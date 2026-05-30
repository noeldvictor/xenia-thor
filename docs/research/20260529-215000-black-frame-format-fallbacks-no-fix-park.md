# Black-Frame: Format Fallbacks Don't Fix It — Lane Parked (iter 12)

## Test (negative)

On Lost Odyssey (deterministic), tried the format/texture toggles that hint at a
known Adreno black-output cause:

- `vulkan_force_2101010_rgba8_fallback true` -> still black
  (`game-pass-lo-2101010-20260529-214920`, near_black=1).
- `vulkan_force_signed_2101010_unorm_fallback true` +
  `vulkan_debug_texture_fetch_disable_exp_adjust true` -> still black
  (`game-pass-lo-signed2101010-20260529-215026`, near_black=1).

So the black frame is NOT a 2101010 format-fallback issue.

## Black-frame lane status: well-characterized, PARKED (deep)

Across iters 9-12 the boots-but-black class is now fully bounded:
- present / swapchain / composition: OK (forced solid output displays).
- output geometry: OK (front 1280x720, output 1920x1080@0,0).
- EDRAM->frontbuffer resolve: OK (targets 1F4F8000, lands).
- resolve selection: not the issue (no non-clear candidate).
- format fallbacks (2101010 / texture exp-adjust): no effect.
- **Remaining root cause: the guest's rendered EDRAM color is genuinely black**
  (draws/pixel-shader output/EDRAM content) — deep, likely title-variable
  (Gears 3 renders fine). Lost Odyssey also runs uncapped ~170 fps black, so it
  may be stuck pre-content, not merely loading.

This is a hard GPU-emulation lane (the prior Sylpheed presenter work also did not
crack it). Parking it here, well-documented, rather than spend further low-ROI
iterations. Future entry point: `vulkan_trace_draw_state` /
`vulkan_debug_pixel_shader_output_*` on a black title vs Gears 3 to see whether
draws emit color, and whether dumped guest pixel shaders translate to black.

## Pivot

Higher-ROI next: push Gears 3 (which renders to its menu) toward actual campaign
gameplay via a longer deterministic -InputSequence; then the UMA upload-path
perf counter and the A64 stackpoint balance (Infinite Undiscovery).
