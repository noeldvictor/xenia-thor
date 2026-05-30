# "Boots-but-Black" Set Is Heterogeneous (autonomous iter 23) — corrects iter 22

## Result

Applying the ps_writes>0 color-draw test + screenshots across the set shows the
"boots-but-black" titles are NOT one class. The iter-22 generalization ("black
titles issue no color geometry -> guest-progress stall") holds for Lost Odyssey
but NOT for the others.

| title | this run | color draws (ps_writes>0) | actual screen |
|-------|----------|---------------------------|----------------|
| Lost Odyssey | black, ~170 fps | 0 | black (no color geometry) -> pre-scene/guest-progress stall |
| Blue Dragon | NOT black, ~2 fps | 74 | **in-game HUD renders** (char "Shu", HP 40/MP 30, weapon icon, blue field) - just very slow |
| Banjo N&B | NOT black | 20 | **"Disc Read Error" dialog** (bad/unimplemented file IO) - a file-IO blocker, renders its error UI |
| Gears 3 (control) | renders | 243 | menu, renders |

Packets: game-pass-bluedragon-draw512-20260529-232348,
game-pass-banjo-draw512-20260529-232211,
game-pass-lo-draw512-20260529-230919, game-pass-gears3-draw512-20260529-231512.

## Corrected model

The earlier RPG sweep snapshotted these at one moment and lumped them as
"boots-but-black", but per-title they are distinct:

- **Lost Odyssey** = genuine black, zero color draws -> guest never reaches its
  scene-render code (pre-scene / loading / guest-progress stall). This is the only
  one that fits the "no color geometry" finding.
- **Blue Dragon** = actually IN-GAME and rendering (HUD + field visible), bottle-
  necked at ~2 fps. Its problem is SPEED, not black-frame. 74 color draws confirm
  real rendering. (The earlier sweep's near_black=1 was a momentary black frame /
  load transition, not a persistent black screen.)
- **Banjo** = a file-IO / disc-read blocker (renders its own error dialog), a
  different lane entirely (matches the prior dirty-disc-adjacent file-IO notes).

So there is no single "boots-but-black" bug. Lesson: classify each title by a
live draw-trace + screenshot, not by a one-shot sweep label.

## Next experiments (per title)

- Lost Odyssey: the one true black/no-color-draw case. Probe guest progress -
  xboxkrnl_thread_wait_trace + A64 thread snapshots to see where it's stuck before
  scene render. (Rejoins the guest-execution lane.)
- Blue Dragon: it RENDERS in-game at ~2 fps -> this is the speed lane, not black.
  A64 speed profile / hot-function capture would show the bottleneck. (Note: Blue
  Dragon speed work is paused per AGENTS.md unless the user restarts it - do NOT
  reopen without the user.)
- Banjo: file-IO error path (NtQueryFullAttributesFile etc., per prior notes).

## Status

Corrects the iter-22 over-generalization. Only Lost Odyssey is "black / no color
draws". Blue Dragon renders in-game (slow); Banjo hits a file-IO error. No code
change. Per-title classification table is the durable result.
