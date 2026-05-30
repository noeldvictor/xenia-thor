# Black-Frame Class: Present Pipeline Ruled Out (autonomous iter 9) — DEVICE

## Result (high-leverage narrowing)

For the shared "boots-but-black" class (Blue Dragon, Lost Odyssey, MagnaCarta 2,
Sylpheed, Banjo — all swap but render black), the **present/swap/composition
pipeline is NOT at fault**. The black frame is the guest **frontbuffer content
being empty/black**, i.e. a render-target -> frontbuffer resolve / source issue.

## Probe (Lost Odyssey, deterministic)

`game-pass-lo-blackprobe-20260529-212502`, APK C5F75B10, launched with
`-InputSequence` (hid_nop) and `-ExtraIntent "--ez vulkan_debug_solid_guest_output
true --ez present_trace_guest_output_geometry true ..."`.

- With solid guest output forced, the screen shows **solid magenta full-screen at
  165.8 FPS** (`near_black=0`; previously this title was `near_black=1.0`). So the
  swapchain + present + guest-output composition path all work.
- Geometry trace is valid every frame:
  `host=1920x1080 surface=1920x1080 front=1280x720 aspect=1280x720
  output=1920x1080@0,0 effects=1 final_effect=1920x1080`. The guest frontbuffer
  is a normal 1280x720, the output region is the full screen at 0,0 — NOT
  degenerate, NOT offscreen, NOT zero-sized.
- Swapchain: `Created 1920x1080 swapchain format 37 ... presentation mode 1`.

So neither the present pipeline nor the output geometry explains the black frame.
What remains: the 1280x720 guest frontbuffer that the presenter samples is empty
(black), meaning the guest's rendered render target is not being resolved/copied
into the presented frontbuffer (or the swap samples the wrong RT/address).

## Next experiment (pinpoint)

Re-run with `--ez gpu_trace_swap_frontbuffer_checksum true` and
`--ez gpu_trace_swap_render_targets true` (both launch-wired; see
thor_xenia_debug.ps1). Determine:
- Is the presented frontbuffer checksum all-zero/constant (resolve not landing
  there) while the guest has a non-trivial render target elsewhere? -> RT-resolve
  / frontbuffer-source mismatch.
- vs frontbuffer has content but a later effect/clear blanks it.

That isolates whether the guest RT exists-but-isn't-resolved-to-frontbuffer vs
the guest never produces color. Then target the resolve/frontbuffer-source path
in vulkan_render_target_cache.cc / the swap source selection. A single fix there
could unblock all the boots-but-black titles.
