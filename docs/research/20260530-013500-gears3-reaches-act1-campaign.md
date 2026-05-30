# Gears 3 Reaches ACT I Campaign Briefing/Loading (iter, device-verified)

## Result (screenshot-verified this turn)

Gears 3, deterministic launch with an extended campaign-navigation hid_nop
sequence, packet `game-pass-gears3-campaign2-20260530-013512`:
- Sweep classifier: ~30.0 fps, vdswap=4860, near_black=0.66 (real rendered
  content, not black), crash markers=2 (RtlRaiseException-class noise; no CRASH
  DUMP claimed).
- SCREENSHOT (viewed): Gears 3 **ACT I "Anchored"** campaign chapter briefing /
  loading screen — Marcus Fenix rendered in COG armor on the left, "ACT I" +
  chapter title text, loading indicator lower-right, 29.6 FPS badge.

So Gears 3 now navigates title -> main menu -> Campaign -> ACT I chapter, and is
on the level briefing/loading screen, rendering at ~30fps. This is past the menu
(prior best) and into the campaign flow.

## Honest scope

NOT yet confirmed as controllable in-level gameplay — this is the act/chapter
briefing/loading screen, not a screenshot of the player controlling a character in
a level. "Playable" is not claimed until a gameplay screen (HUD + controllable
character in the level) is captured.

## Next step

Re-run with a longer settle (let ACT I finish loading into the level) and capture
again; if it shows in-level HUD/gameplay, that is the playable proof. If it stalls
on the loading screen, capture wait-trace/VdSwap to see whether load completes or
hangs (could intersect the thread-join lane).

## Sequence used (reproducible)

`-InputSequence "start@26000:400;a@31000:400;a@36000:400;a@42000:400;a@50000:400;
a@58000:400;a@68000:400;a@80000:400;a@92000:400"` -BootWaitSec 30 -SkipWindowSec
80 -SettleSec 10.

## Status

Device-verified: Gears 3 reaches ACT I campaign briefing/loading @ ~30fps. Not yet
confirmed in-level playable. No code change.
