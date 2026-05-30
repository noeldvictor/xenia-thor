# Black-Frame: Guest Present-Side Healthy, Gap is Frontbuffer Content (iter 10)

## Result

Continuing the boots-but-black pinpoint on Lost Odyssey
(`game-pass-lo-fbprobe-20260529-213210`, deterministic hid_nop launch,
`gpu_trace_swap=true`). The guest present-side is fully healthy; the black frame
is specifically that the **frontbuffer content is not produced**.

Swap trace (continuous, every frame):

```
XE_SWAP packet magic=53574150 frontbuffer=1F4F8000 size=1280x720 display=1280x720 count=4 ...
Vulkan IssueSwap begin frontbuffer=1F4F8000 guest_size=1280x720 display=1280x720 frame_current=10373 frame_completed=10372 ...
VdSwap ... front_va=FF4F7000 front_pa=1F4F8000 format=6 size=1280x720 fetch_dwords=8A000002,1F4F8006,0059E4FF,00001414,00000000,00000200
Created 1920x1080 swapchain format 37 ... presentation mode 1
```

So: a stable guest frontbuffer at PA `1F4F8000` (1280x720, format 6), a valid
texture-fetch descriptor, `frame_current` advancing, and a healthy swapchain.
Together with iter 9 (forced solid output displays full-screen, geometry valid),
**every stage is correct except the actual pixels at `1F4F8000`.**

The `gpu_trace_swap_frontbuffer_checksum` / `gpu_trace_swap_render_targets` cvars
were set (they are in the allow-list) but produced no usable rows in the captured
tail: the per-swap `gpu_trace_swap` logging at ~190 fps is so dense it rolled the
checksum/startup lines out of the `-t 120000` window, and the frontbuffer checksum
likely also needs a readback to actually sample guest memory.

## Conclusion / hypothesis

The black frame for this class = the guest's rendered color (EDRAM render target)
is **not being resolved into the frontbuffer at `1F4F8000`** that the presenter
samples (or the guest never produces color). The present, swap-descriptor, and
geometry paths are all proven correct.

## Next (pinpoint the resolve)

1. Re-probe with `--ez vulkan_trace_resolve true` (+ small budget) and a SHORTER
   capture (no `gpu_trace_swap` flood) to see whether EDRAM->frontbuffer resolves
   occur at all and whether any resolve targets `1F4F8000`.
2. If no resolve targets the frontbuffer, the fix is in the resolve path
   (`vulkan_render_target_cache.cc`) / swap frontbuffer-source selection. If
   resolves do target it, add `--ez vulkan_readback_resolve true` +
   `gpu_trace_swap_frontbuffer_checksum` to confirm the bytes are non-zero.
3. Whatever fix emerges, device-verify against the boots-but-black titles; forced
   solid output already proves the present side works.
