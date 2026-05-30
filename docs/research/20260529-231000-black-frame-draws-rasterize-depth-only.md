# Black-Frame: Draws Rasterize But Are Depth-Only / No Color (autonomous iter 21)

## Result (corrects iter 20)

With a larger draw-state budget (512), the iter-20 "all draws skipped" read is
WRONG. Lost Odyssey draws DO rasterize; the captured ones just produce no color.

Packet `scratch/thor-debug/game-pass-lo-draw512-20260529-230919` (still black,
~173 fps, vdswap=9028). Draw-state trace:

- **1488** `GPU draw trace:` rows total: **24 skipped**, **1464 rasterizing**
  (`raster=true`). So skips are a small minority, not the story.
- Every captured rasterizing draw is **depth-only / no pixel output**:
  `prim=8 index_count=3 host_vertices=3 raster=true pixel=false ps_writes=0
  ps_hash=0000000000000000 normalized_color_mask=0000 edram_mode=5 rb_mode=5
  color_mask=FFFF depthcontrol=00008777 z_enable=true z_write=true zfunc=7
  viewport=8192x8192+0,0 msaa=2 surface=0A020280 pitch=640`.
- RT row: `rt color0=000002D0 fmt=0 mask=F base=720` (a color target IS bound).
- **max index_count = 4** across all 1488 rows — every draw is a 3-4 index
  (single-triangle) draw.

## Interpretation

The guest is issuing many tiny single-triangle draws that **rasterize depth but
write no color** (`pixel=false`, `ps_writes=0`, no pixel shader hash). A color RT
is bound (color0 base=720) but nothing shades into it -> the frontbuffer stays
black (consistent with the earlier resolve finding: frontbuffer resolved to
opaque black because nothing wrote color).

So the refined black-frame model: it's not present/resolve/geometry/skip — it's
that the **color-writing draws are missing or not producing pixel output**. Two
live hypotheses:
1. The actual scene's color draws (bigger index_count, with a real ps_hash) are
   happening but were NOT in this captured window (budget 512 filled with the
   tiny depth draws; or they come later in the frame and the 170 fps flood aged
   them out). Need a capture that catches a color draw (ps_writes>0).
2. The guest's color draws are being emitted as no-pixel (pixel shader not
   translated / disabled / killed), so only depth is written. If NO draw in a
   full frame ever has ps_writes>0, that's the bug class (pixel shader path).

The all-index_count<=4 observation is notable: if the REAL geometry is also tiny,
the title may be stuck on a loading/setup screen drawing only placeholder/depth
prims, not actual scene content (would link back to a guest-progress/loading
stall rather than a pure GPU bug).

## Next experiment

1. Capture targeting a COLOR draw: grep the full trace for any row with
   `ps_writes=` > 0 or a nonzero `ps_hash`. If present -> color draws exist, black
   is downstream; if absent across a full frame -> pixel-output path is the bug.
2. Gears 3 (renders) CONTROL with the same trace: it MUST show ps_writes>0 /
   nonzero ps_hash / larger index_count draws. Diff the two to pinpoint what Lost
   Odyssey lacks (pixel shader? larger geometry? a specific edram/rb_mode?).
3. Then read the pixel-shader translate/emit path or the loading-state, per which
   hypothesis the data supports. Characterize only; no guess-edit.

## Status

Iter-20 "draws skipped" superseded: draws rasterize but are depth-only/no-color,
all index_count<=4. Concrete next step = find a color (ps_writes>0) draw or prove
none exists, with a Gears 3 control. No code change.
