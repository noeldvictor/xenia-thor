# 2026-05-18 19:23 EDT - Blue Dragon Vulkan Presenter Split

This note is for the experimental xenia-thor fork only. It is not an upstream
Xenia compatibility claim and should not be used to request support from the
official project.

## Question

Blue Dragon reaches `VdSwap`, `PM4_XE_SWAP`, `Vulkan IssueSwap`, and
`RefreshGuestOutput`, but Android screenshots stay black. Is the black screen
caused by Android/Vulkan presentation, or by the guest image data being black or
wrong?

## Evidence

- `scratch/thor-debug/20260518-190743-*`: after the
  `VK_ACCESS_SHADER_WRITE_BIT` shared-memory barrier fix, Blue Dragon still
  swaps all-zero frontbuffers `1CA1C000` and `1CDB4000`.
- `scratch/thor-debug/20260518-191850-*`: with
  `vulkan_present_recent_resolve_on_swap=true`, `IssueSwap` repeatedly presents
  candidate `1DC14000+00398000` at `1280x720`, pitch `1280`, format `7`, but
  the screenshot is still black behind the Java OSD.
- `scratch/thor-debug/20260518-192246-*`: with
  `vulkan_debug_solid_guest_output=true`, the same Blue Dragon guest-output
  callback renders a bright magenta full-screen image. The capture proves the
  Android Surface, Vulkan swapchain, and Xenia presenter path can display.

## Conclusion

The current black screen is not an Android UI or swapchain display failure. The
presenter can show pixels when the guest-output image is filled directly.

The active wall is earlier in the GPU data path:

- The VdSwap frontbuffers are real and are submitted, but they are zero.
- A recent full-size resolve can be routed into the presenter, but the chosen
  candidate is not useful visible output.
- The next useful debug system needs to classify resolve candidates by actual
  visible color data and explain why the game's VdSwap frontbuffer addresses are
  not receiving that data.

## Next Experiment

Add a resolve-candidate scorer that logs and prefers candidates with nonzero
visible color entropy instead of just "full-size and preferred format." Keep the
solid-output probe for presenter sanity checks, but keep it default-off.
