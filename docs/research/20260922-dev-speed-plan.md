# Dev speed plan: a PC-first workflow (2026-09-22 evening)

User, 2026-09-22: "review all work done in last 24 hours it took way too long and we don't have
much to show", "we need a much smarter pc workflow", priority "broad speed and polish all
games, focus on Magna Carta 2, Banjo, Gears of War". No new hardware (no cooler, no second
device). Device use is autonomous; installs are free; each cycle reports its result.

## Where the time goes (measured)

| step | cost | note |
|---|---|---|
| NativeCore build | 95 to 106 s | not the bottleneck |
| install (ApkShellDeploy) | 6 to 8 s | not the bottleneck |
| one device question | 1 to 3 min launch and route, up to 15 min cool-down | x8 launches for an intermittent bug |
| wrong order | 3.5 h (Banjo freeze), 6 h (Blue Dragon regression) | the deciding split was one launch |
| no timebox | 3.5 h, 8 commits, no fix (Banjo dark tile) | |
| notes | 25 of 61 commits | |
| sprawl | 159 files in tools/thor, 40 MCP functions, AGENTS.md 1,012 lines | |
| stale symbols | device build id 9712..., local c699... | the MCP named frames of the wrong build |

## The rule: the device measures, the PC finds

The device is for the scoreboard and for what only the Adreno shows. Finding causes happens on
the PC, where a run is 30 to 75 s, needs no cool-down, and several can run at once.

## The PC labs

1. **Parity run (kernel, HLE, game logic).** The Windows build runs with the device's settings:
   every `XE_ANDROID_DEFAULT` cvar at its Android value, the title's shipped toggles, and the
   patches. A bug from an Android-only default reproduces there (the menu freeze of
   2026-09-22 came from one). The canary oracle (`scratch/oracle/xenia_canary.exe`) is the
   reference when the fork fails. Tool: `tools/pc/pc_parity.py`.
2. **Trace lab (GPU front end and correctness).** A device trace (`.xtr`) replays on the PC.
   The PC image of a device frame is the reference picture. The command processor, the
   render-target cache, and the texture cache are the same code on both machines, so their
   CPU cost per frame (Banjo: IssueDraw 14 to 23 ms) is measured and cut on the PC with a
   trace replay benchmark, seconds per try. Tool: `tools/pc/trace_bench.py`.
3. **Adreno shader lab (GPU cost without the device).** Mesa's Turnip and the ir3 compiler in
   WSL with the freedreno drm-shim (a noop Adreno 7xx) compile the translated shaders of a
   frame and give instruction counts, registers, and spills per shader. A shader-translator
   change is scored on the PC. The same Mesa tree is the base for a custom Turnip build.
4. **Device unit tests.** The PowerPC instruction tests run on the device as a plain binary
   (`ppctests_subset.sh`): seconds, no game, no heat. Backend bugs get a unit repro there.

## The device side

1. **Scoreboard.** One command runs the same routes after each install and appends one row:
   Banjo title fps and the save-panel reach, MagnaCarta 2 gameplay fps and GPU frame time,
   Gears of War boot and fps, Blue Dragon field fps (regression check). It prints the delta
   against the last row. Tool: `tools/thor/scoreboard.py`, rows in `docs/scoreboard.jsonl`.
2. **Triage.** One command splits the axes for a new symptom in about three launches:
   baseline, desktop defaults, the other CPU backend; live GPU levers inside one launch.
   Tool: `tools/thor/triage.py`.
3. **Symbols.** The MCP checks the build id of the installed library against the local one
   before it names frames.

## Process

- The outcome rules in AGENTS.md (split first, own changes first, 90-minute timebox, scoreboard
  per install, one notes commit per cycle, tools only for a second use).
- The stop hook checks for a scoreboard row on an install day.
- AGENTS.md becomes a short core (rules, tools, current state); the history moves to
  `docs/worklog`.

## Order

1. Install the menu-freeze fix; verify it with `menu_walk.py`.
2. Build-id check in the MCP.
3. Scoreboard v1 and its first row.
4. Parity run on the PC for Banjo, MagnaCarta 2, and Gears (the images copy to `scratch/`).
5. Trace bench for the GPU front end.
6. Shader lab in WSL.
7. Stop hook, AGENTS.md core, tools archive.
