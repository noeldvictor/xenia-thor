# Gears 3 IN-LEVEL CAMPAIGN GAMEPLAY (device-verified) — PLAYABLE

## Result (screenshot-verified this turn)

Gears 3, deterministic launch driven through menu -> Campaign -> ACT I into the
level. Packet `game-pass-gears3-inlevel-20260530-013902`:
- Sweep classifier: fps~14.6, vdswap=6230, near_black=0.12 (bright, content-rich
  frame), crash markers=3 (RtlRaiseException-class noise; no CRASH DUMP).
- SCREENSHOT (viewed): Gears 3 **in-level third-person gameplay** — Marcus Fenix
  (COG soldier) seen over-the-shoulder on the ACT I "Anchored" naval-vessel deck,
  daytime sky, with the GAMEPLAY HUD present (ammo/weapon indicator bottom-right,
  objective text top). This is live gameplay, not a menu/cutscene/loading screen.

## What this means

Gears of War 3 is **playable** on the AYN Thor build to the extent of: it boots,
navigates its full menu, loads ACT I, and renders interactive in-level campaign
gameplay with HUD. Running ~14.6 fps in this scene (heavy combat-arena rendering;
slower than the 30fps menu). This is the first device-verified in-level-gameplay
screenshot of a title in this session.

## Honest caveats

- ~14.6 fps is well below smooth; playable-but-slow. A speed pass (not done here)
  would be the follow-up to make it comfortable.
- Reached via scripted hid_nop input; the exact menu path is timing-dependent, so
  the sequence may need adjustment across builds. Sequence used:
  `start@26000:400;a@31000:400;a@36000:400;a@42000:400;a@50000:400;a@60000:400;
  a@72000:400;a@86000:400;a@100000:400;a@115000:400;a@130000:400`
  -BootWaitSec 30 -SkipWindowSec 120 -SettleSec 12.
- crash=3 markers are RtlRaiseException noise (consistent across Gears runs); the
  frame is live, no native abort claimed.

## Lineage

Built on this session's Gears fixes: zero-stride vfetch (boot crash fix, commit
76464bae3) + XamAlloc unk assert soften (227f28f7f) got Gears past boot to title;
deterministic input navigation reached menu -> ACT I -> in-level here.

## Status

DEVICE-VERIFIED: Gears 3 reaches in-level campaign gameplay (ACT I, HUD, ~14.6fps).
A playable title. No code change this step.
