# Black-Frame Draw Probe — Guest Draws Are SKIPPED Pre-Raster (autonomous iter 20)

## Result (concrete lead)

On Lost Odyssey (deterministic launch, `--ez vulkan_trace_draw_state true
--ei vulkan_trace_draw_state_budget 64`), the guest IS issuing draw calls, but
Xenia is **skipping them before rasterization**. The draw trace shows, repeatedly:

```
* fullDrawIndexUint32
GPU draw trace: skipped no rasterization/no memexport prim=1 index_count=1 edram_mode=4 vs_hash=C049A8C9E556F129
```

Packet: `scratch/thor-debug/game-pass-lo-drawprobe-20260529-230110` (still black,
near_black=1, ~170 fps, no crash). `vulkan_trace_draw_state` confirmed forwarded
(EmulatorActivity allow-list line 184, launcher line 628).

## Interpretation

This refines the black-frame model significantly:
- NOT "no geometry submitted" (draws exist) and NOT a pure pixel-shader bug.
- The draws are being dropped by the command processor's "skip" path:
  `skipped no rasterization/no memexport` means the draw was classified as
  producing neither rasterized output nor a memexport, so it is not executed.
- The captured draws have `index_count=1` (degenerate / 1 index) and
  `edram_mode=4`. A 1-index draw is suspicious — either the guest is issuing
  trivial/setup draws here, OR the index count / vfetch is being misdecoded so
  real draws look empty and get skipped.

Two hypotheses for the next iteration to separate:
1. These index_count=1 rows are just early setup draws and the real scene draws
   (higher index_count) come later / were rolled out of the captured tail by the
   170 fps flood -> need a wider/targeted capture to see if ANY draw rasterizes.
2. The "no rasterization" classification is wrong for this title (e.g. render-
   target / edram_mode=4 handling, or an index/vfetch decode making draws look
   empty) -> the real scene draws are being skipped, which IS the black-frame bug.

## Next experiment

1. Re-capture with a larger draw-state budget and grep for ANY draw row WITHOUT
   "skipped" (a rasterized draw) and for the max index_count seen. If every draw
   is "skipped" / index_count<=1, the skip-classification or index decode is the
   bug. If some draws rasterize, the black frame is downstream (RT/clear) after
   all and this is a red herring.
2. Run the SAME draw trace on Gears 3 (renders) as control: confirm Gears shows
   non-skipped, higher-index draws -> proves the trace distinguishes good vs bad.
3. If confirmed that all Lost Odyssey draws are skipped pre-raster, read the
   command-processor draw-skip decision ("no rasterization/no memexport") in
   vulkan_command_processor.cc to find why these draws are classified
   non-rasterizing (edram_mode=4? color mask? RT bind?). Characterize; do not
   guess-edit.

## Status

Genuine progress: black-frame narrowed from "guest renders black" to "guest draws
are skipped pre-rasterization (no rasterization/no memexport, index_count=1,
edram_mode=4)". Needs one more capture to decide skip-classification-bug vs
red-herring-setup-draws. No code change.
