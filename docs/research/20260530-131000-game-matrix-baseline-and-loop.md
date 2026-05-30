# Game-Matrix Regression Loop: WORKING + Baseline (4 priority titles)

## The loop is built and produces a real baseline

`tools/thor/thor_game_matrix.ps1` runs the 4 priority titles through the verified
capture gate, assigns RPCS3-style tiers + metrics, and (with -Baseline) reports
BETTER/WORSE/SAME per game. This is the "is it better or worse?" loop the user
asked for, grounded in emulator-test best practice (Dolphin FifoCI = deterministic
replay + reference compare; RPCS3 = status tiers).

Baseline saved: docs/research/game-matrix-baseline.json (this run = the reference
future changes are measured against).

## BASELINE (2026-05-30, device-measured, gate-verified)

| game        | tier          | classification    | near_black | fps  | notes |
|-------------|---------------|-------------------|-----------|------|-------|
| burnout     | 2 Ingame-black| present_but_black | 0.86      | 59.6 | in-race, HUD renders, 3D scene BLACK (verified read) |
| bluedragon  | 3 Ingame      | rendering         | 0.42      | 30.0 | renders 3D field+party+UI (verified read) |
| lostodyssey | 2 Ingame-black| present_but_black | 0.91      | 28.0 | black (HUD/none) |
| banjo       | 0 Nothing     | crash             | 1.0       | 0    | disc-read-error class (fatal=3) |

Spot-checked by READING screenshots: burnout (HUD-only, 3D black, sha 9C2F7E1A)
and bluedragon (real 3D in-game scene). Tiers matched reality.

## KEY CROSS-GAME INSIGHT (the lever)

Blue Dragon's 3D scene REACHES the frontbuffer (tier 3, renders world+characters).
Burnout and Lost Odyssey present at ~30-60fps but their 3D scene is BLACK
(tier 2, HUD-only / black). So the present/resolve path CAN deliver a 3D scene
(Blue Dragon proves it) but does NOT for Burnout/Lost Odyssey. Diffing Blue
Dragon's working render/RT path against Burnout's black 3D path is the most
direct route to the shared black-3D-scene bug -- and the harness will tell us
(via near_black dropping) if a fix lights up the black titles.

## How to use the loop (improve -> measure)

1. Baseline exists (above).
2. Make a change (e.g. a render-target/resolve fix), build, deploy.
3. `thor_game_matrix.ps1 -Baseline docs/research/game-matrix-baseline.json`
4. Read the BETTER/WORSE/SAME report. For the black-3D bug, success = burnout/
   lostodyssey near_black DROPS (3D scene appears) and tier rises 2->3/4, with NO
   regression on bluedragon (must stay tier 3+). Always READ the screenshots to
   confirm scene content.

## Caveats (honest)

- "Reach" is currently launch + dwell + light Start/A mashing (not yet input
  replay). Banjo's tier-0 is its disc-read-error path; transient states possible
  -> re-run to confirm a regression before trusting a single WORSE.
- Tiers/metrics are machine-computed from gate verdicts (screenshot read +
  logcat) = trustworthy for better/worse; scene CONTENT still needs a screenshot
  read to interpret.

## Status

Regression loop operational; baseline committed. Next per user: iterate on the
black-3D-scene bug (Burnout/Lost Odyssey tier 2), using Blue Dragon (tier 3,
working 3D) as the contrast, and the matrix to measure better/worse across all 4.
On master.
