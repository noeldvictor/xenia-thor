# Blue Dragon Watchdog Timing And Present-Source Checkpoint

- Date: 2026-05-18 21:07 EDT
- Device: AYN Thor Max, ADB serial `c3ca0370`
- Branch: `master`
- Base head during captures: `ab21a1f2c`
- APK SHA-256 after the timing-plumbing deploy:
  `A4992FAD0A959CF21C8126CEA940D9CD76DE72141BAE8E6B1B78B4E08B73B39D`
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Captures

- Baseline interrupt trace, forced `1C340000` 640x360:
  `scratch/thor-debug/20260518-204736-*`
- Swap-completion source-1 interrupt experiment:
  `scratch/thor-debug/20260518-205158-*`
- `time_scalar=0.25`, forced `1C340000`:
  `scratch/thor-debug/20260518-205549-*`
- `time_scalar=0.5`, forced `1C340000`:
  `scratch/thor-debug/20260518-205851-*`
- `time_scalar=0.5`, recent full-size resolve:
  `scratch/thor-debug/20260518-210200-*`
- `time_scalar=0.5`, forced `1C304000` 320x180:
  `scratch/thor-debug/20260518-210516-*`

## Findings

- With normal guest time and no extra swap interrupt, Blue Dragon reaches frame
  1 before reporting the D3D watchdog:
  `The GPU is hung! ... frame is 1`.
- The frame-1 dump still shows the ring behind at the watchdog sample:
  `CP_RB_RPTR: 0x00000041`, `CP_RB_WPTR: 0x00000055`.
- The command processor keeps making progress after that watchdog, so this is
  very likely a timing/throughput issue during bring-up, not a hard CP stop.
- PM4 `INTERRUPT` packets are present and source-1 callback dispatch returns
  normally during the good path. The observed Blue Dragon token at `FFCA3000`
  remains `0`, so token movement is not the current completion signal.
- Dispatching source 1 after every `PM4_XE_SWAP` is harmful:
  `scratch/thor-debug/20260518-205158-*` logs the swap-triggered source-1
  callback begin without an end, XMA stops, only one `XE_SWAP` completes, and
  the watchdog repeats at frames 1 and 2.
- `time_scalar=0.25` eliminates the watchdog for a 110 second host run:
  150 `XE_SWAP`s, 8,864 XMA kicks, no fatal signal, no Android runtime crash.
- `time_scalar=0.5` also eliminates the watchdog for a 120 second host run:
  1,088 `XE_SWAP`s, 9,581 XMA kicks, no fatal signal, no Android runtime crash.
- The forced 640x360 source `1C340000` and 320x180 source `1C304000` both stay
  on a flat dark-blue image. This is visible guest output, but not title.
- The existing recent-full-size resolve selector chooses `1DC14000` /
  `1DC27000` style 1280x720 candidates under the timing fix and presents black.

## Decision

Keep `-TimeScalar 0.5` as a temporary Thor bring-up knob for Blue Dragon. It is
not a compatibility fix, but it cleanly distinguishes the false D3D watchdog
from the remaining present/content problem.

Do not use `-GpuInterruptOnSwap true` for Blue Dragon. It is narrower than the
old ring-idle interrupt hack, but it still stalls inside the guest callback.

The next useful graphics step is a scored render-content selector. Recency and
format preference are not enough: full-size candidates are often black or clear,
and the visible small candidates are flat blue.

