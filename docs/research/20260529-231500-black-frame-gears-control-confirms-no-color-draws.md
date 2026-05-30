# Black-Frame: Gears 3 Control Confirms Lost Odyssey Issues NO Color Draws (iter 22)

## Result (control nails the class)

Same draw-state trace (budget 512), Lost Odyssey (black) vs Gears 3 (renders):

| metric | Lost Odyssey (black) | Gears 3 (renders) |
|--------|----------------------|-------------------|
| GPU draw trace rows | 1488 | 1488 |
| skipped | 24 | 24 |
| rasterizing | 1464 | 1464 |
| **color-writing draws (ps_writes>0 / nonzero ps_hash)** | **0** | **243** |
| max index_count | 4 | 4 |

Gears 3 sample color draw:
`prim=6 index_count=4 raster=true pixel=true ps_writes=1 ps_kills=false
normalized_color_mask=000F used_textures=00000007 ps_textures=00000007
vs_hash=1456901233B6564C ps_hash=F6AFAEAD7D170F51 viewport=1280x720
surface=14000500 pitch=1280 color_mask=000F`.

Lost Odyssey: every captured draw is `pixel=false ps_writes=0 ps_hash=0`
(depth-only), zero color-writing draws.

Packets: `game-pass-gears3-draw512-20260529-231512` (control),
`game-pass-lo-draw512-20260529-230919` (subject).

## Conclusion

The trace clearly distinguishes a rendering title (Gears: 243 color draws with
real pixel shaders + textures) from a black title (Lost Odyssey: 0 color draws).
So the boots-but-black class is now firmly: **the guest never issues color
(pixel-writing) geometry during the captured window — only depth-only draws.**
This is upstream of the GPU emulation: the guest itself isn't getting to its
color-rendering code.

Note both show max index_count=4 in their captured windows, but Gears clearly DOES
render real content on screen with those draws (+ its 243 ps_writes draws), so the
small index_count is an artifact of WHICH draws filled the 512 budget (early
per-frame draws), not the whole frame. The discriminating signal is ps_writes>0
presence, and only Gears has it.

## Interpretation / most likely cause

Lost Odyssey at this point is almost certainly **stuck before its scene-render
code** — a loading / guest-progress stall where it submits only depth/setup draws
(and keeps VdSwap-ing a black frontbuffer at ~170 fps). That matches: uncapped
high fps (little real GPU work), color RT bound but never shaded, no crash. So the
black frame is a GUEST-PROGRESS / loading stall for this title, NOT a GPU
pixel-shader-translation bug. (Different titles in the boots-but-black set may
still differ; Banjo/Sylpheed should be checked the same way.)

## Next experiment

1. Check whether Lost Odyssey EVER issues a ps_writes>0 draw across a much longer
   window / later time (longer capture, or after more guest progress). If never,
   confirms pre-scene stall.
2. Apply the same ps_writes>0 test to Banjo and Sylpheed to see if the whole
   boots-but-black set is "no color draws" (loading stalls) or if some do issue
   color draws that don't reach the frontbuffer (a real GPU bug).
3. If it's a guest-progress stall, the lane rejoins the guest-execution/wait
   investigation (A64 thread snapshots / XboxkrnlThreadWaitTrace), not the GPU.

## Status

Black-frame class confirmed via control: black titles issue no color draws while
a rendering title issues hundreds. Likely guest-progress/loading stall, not a GPU
pixel bug. No code change (characterization).
