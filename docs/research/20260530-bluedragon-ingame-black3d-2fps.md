# Blue Dragon — reaches in-game (party HUD) but 3D world missing + ~2fps, device-verified 2026-05-30

Device: AYN Thor c3ca0370. master. UMA OFF (gpu_uma_direct_shared_memory=false).
ISO: Blue Dragon.m3u / Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso

## What was reached (read screenshots, not inferred)
Blue Dragon boots and reaches ACTUAL IN-GAME state: the in-field party HUD is
rendered — character "Shu", HP 40 / MP 30, weapon icon (bottom-left). This is past
title/menu, in the game world. (screenshots bluedragon-uma-off.png and
bluedragon-uma-off-2.png — byte-identical at 94339 B though VdSwap advanced
782->908, i.e. presenting the SAME frame repeatedly.)

## Two problems (both = the user's stated blocker class)
1. 3D WORLD NOT REACHING FRONTBUFFER. The whole screen is flat light-blue where the
   field/environment should be; only the 2D HUD composites through. Same signature
   as Burnout in-race: HUD presents, 3D scene render target does not reach the
   frontbuffer.
2. ~2.0 FPS. OSD reads 2.0 FPS; VdSwap rate measured ~2.5/s (762->782 in 8s). This
   is the pathological framerate collapse (Thor is 10-20x an Xbox 360, so 2fps is a
   bug, not a hardware limit).

## Evidence the guest IS rendering (so the 3D loss is on our present/RT side)
logcat (UMA-off run) shows active rendering + asset streaming, e.g.:
- "Created a 320x8192 1xMSAA color render target ... EDRAM base 0"
- "Created a 320x8192 1xMSAA depth render target ... EDRAM base 48"
- "Created a 160x4096 4xMSAA color render target ... EDRAM base 0"
- "Created a 160x4096 4xMSAA depth render target ... EDRAM base 48"
- streaming \snd_memory\se, \pack\effect (one missing aef002_011.ipk -> C000000F,
  non-fatal)
So the guest issues real draws into MSAA color/depth render targets; the field just
isn't presented. MSAA RTs are notable — the resolve/copy from an MSAA RT to the
frontbuffer/HUD-composite path is a prime suspect for both the black-3D AND the
2fps (MSAA resolve thrash).

## Cross-game linkage
This matches the Burnout in-race observation (2D HUD renders, 3D black) and the
earlier matrix note: Blue Dragon's 3D was previously seen reaching the frontbuffer
at a title screen, but IN-GAME it does not. Likely ONE shared bug: MSAA
render-target resolve / EDRAM-tile -> frontbuffer path on Adreno. Fixing it would
help multiple priority titles (the cross-compat goal).

## NOT claimed
No UMA involvement here (this run is UMA-off). No fix attempted. The ~2fps cause is
hypothesized (MSAA resolve thrash) but NOT yet profiled — next step is to capture
GPU timing / count resolves per frame in this scene. Frame is stable (identical
bytes across swaps), so it is a persistent in-game state, not a transient load.
