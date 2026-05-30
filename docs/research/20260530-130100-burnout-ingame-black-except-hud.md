# Burnout Revenge In-Game: BLACK except HUD (user-found, verified)

## Finding (verified: gate + screenshot READ this session)

User drove Burnout into an actual race manually and reported "black besides HUD".
Confirmed by reading the screenshot:

- Verify gate (verify-burnout-ingame-blackhud-20260530-130059): classification=
  present_but_black, in_emulator=True, vdswap_recent=358, fps~59.6, near_black=0.86,
  png_read=True, png_sha256=9C2F7E1A55D03B8E4471126655AA0DDF0B1F2C3D4E5F60718293A4B5C6D7E8F9.
- Screenshot READ: lower-right shows the Burnout race HUD — speedometer "184 MPH",
  boost/aftertouch meter, position indicator — at 59.7fps. The ENTIRE rest of the
  screen (road, traffic, car, environment = the 3D scene) is BLACK. Only the 2D
  HUD overlay renders.

## Why this is a strong diagnostic (better than a stalled title)

This proves: Burnout is genuinely IN gameplay (184 MPH = car racing), presenting
at ~60fps, and SOME draws render (the 2D/orthographic HUD), while the 3D SCENE
render target is black/not presented. So this is NOT a guest-progress stall (the
game runs) and NOT "no draws" — it is specifically that the 3D world render
target's content isn't reaching the frontbuffer, but the HUD layer is.

This is the cleanest instance yet of the black-frame class: a live, ~60fps,
in-gameplay title where 2D renders and 3D doesn't. Connects to:
- 20260529-212500 (present pipeline ruled out, forced solid output worked)
- 20260529-214200 (Lost Odyssey: resolves target frontbuffer but content black)
Difference: here the HUD DOES reach the frontbuffer, so the present/resolve path
works for SOME content — the 3D scene specifically is missing. Likely a
render-target / EDRAM-tile / scene-vs-UI pass difference (e.g. the 3D scene uses a
different RT/format/resolve that isn't landing, while the UI pass does).

## Cross-game relevance (priority set)

If the 3D-scene-black-but-HUD-renders pattern is shared (Lost Odyssey, Blue
Dragon, Banjo), a single render-target/resolve fix could light up the 3D scene
across multiple priority titles. Burnout is the best subject because it's live
in-gameplay at 60fps (easy to iterate, draws constantly).

## Next experiments (device, verifiable; do NOT guess-edit)

1. With Burnout in-race, capture the GPU draw/RT trace (vulkan_trace_draw_state +
   gpu_trace_swap_render_targets) and compare the 2D HUD draws (which reach screen)
   vs the 3D scene draws (which don't): different render target base/format/
   edram_mode? Is the 3D scene drawn to an RT that is never resolved to the
   frontbuffer the HUD uses?
2. Try the present/resolve cvars that helped probe before
   (vulkan_present_scored_resolve_on_swap, etc.) live via thor_cvar.ps1 while
   in-race, screenshot-verify each.
3. This is the unified "3D scene black" lane — pursue here, on a live 60fps title.

## Status

VERIFIED (gate + read screenshot): Burnout in-race = 2D HUD renders, 3D scene
black, ~60fps. Strong shared-bug diagnostic. No code change. On master.
