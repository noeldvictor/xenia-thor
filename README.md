# xenia-thor

**An AI experiment, not an emulator for users.**

`xenia-thor` is a personal fork of Xenia. An agentic AI coding assistant does almost all of the
work. The target is one device: the AYN Thor and Thor Max (Snapdragon 8 Gen 2, Adreno 740, native
Android, `arm64-v8a`). The repository exists for the experiment: can an AI agent do sustained,
measured, low-level emulator engineering. The binary is a by-product.

It is unstable. It breaks from commit to commit, by design.

## No support

- Do not ask the Xenia project, the Xenia Discord, or any emulator community for help with this
  fork. They did not write it and cannot fix it.
- Do not report anything from this fork upstream. A crash here is almost always this fork's bug.
- Do not cite this fork as evidence that Xenia supports Android or the AYN Thor. It does not.
- Do not treat anything here as a compatibility claim. A game that works in one commit worked once,
  on one device, in one scene.
- There are no releases, no prebuilt APKs, and no install instructions for users.

## Thanks to the Xenia developers

Everything of value under this fork is theirs: years of research, engineering, and open-source
work. This fork is not their problem.

- [xenia-project/xenia](https://github.com/xenia-project/xenia)
- [xenia.jp](https://xenia.jp/)
- [xenia-canary/xenia-canary](https://github.com/xenia-canary/xenia-canary), the base of this fork
- [has207/xenia-edge](https://github.com/has207/xenia-edge) and
  [rfandango/XenDroid](https://github.com/rfandango/XenDroid), the compatibility references

## What the work is

One device, one architecture, measured changes.

- **CPU.** PowerPC guest code runs through the ARM64 (`a64`) JIT backend and an LLVM
  whole-function recompiler with an ahead-of-time precompile at launch. No x86 code is in the APK.
  Much of the work removes structure the a64 backend inherited from the x64 backend it was derived
  from (two-operand destructive forms, a 7-register budget, TSO assumptions).
- **GPU.** Vulkan on Mesa Turnip, for the Adreno 740's tiled renderer. The Qualcomm driver is not
  used; the driver ships inside the APK.
- **Method.** A performance claim survives a device measurement or it is recorded as unproven.
  Run-to-run drift on this device is about 2.8 %, larger than most effects, so measurements are
  matched pairs in one session with equal thermal starts, frame generation off, and a captured
  gameplay route. Menus and videos are not tests. Negative results are recorded with the same care
  as wins in `docs/research/experiments.db` (`python tools/exp_ledger.py check "<keyword>"`).
- **Control surface.** Every behavior lever is a toggle in the app menu, or a game patch in the
  Game Patches screen. No cvar extras: a launch the user cannot repeat from the play button is not
  a result.

## State on 2026-09-20

- Blue Dragon: 9.9 fps in the village field on the play-button path; 15.8 fps with the bundled
  "No anti-aliasing, single pass" game patch (the game's own 1x path, which also ends its
  predicated tiling). Blue Dragon is low priority now: [re:Blue](https://github.com/zolaware/reblue)
  is a native recompilation of it.
- Banjo-Kazooie: Nuts & Bolts: compiles (38,104 functions) and plays the intro, then stalls on the
  loading screen after a guest null read; under investigation.
- The AOT precompile no longer dies on large titles: JIT code lives in 64 MB slabs instead of two
  VMAs per function (`vm.max_map_count`).
- The object cache works on the play-button path (Blue Dragon: 414 s cold, 15 s warm). Settings
  shows its size and can delete it.
- Game patches apply before the precompile; before this date every patch was inert on Android.

## Building

No prebuilt APKs. Windows host, Android NDK r25, ARM64 only.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShellDeploy -DeviceSerial c3ca0370
```

`NativeCore` builds `libxenia-app.so` with the NDK (about 3 to 7 minutes). `ApkShell` packages the
APK in seconds. The build script generates `build/version.h` and the object-cache stamp, and refuses
to build if the MCP server does not compile. Desktop builds (PC trace replay, PC kernel checks) go
through premake and MSBuild; see `AGENTS.md` section 10.

## Device work

`tools/mcp/xenia_thor_mcp.py` is a stdio MCP server over adb, registered in `.mcp.json`. Every
device action goes through it: preflight (heat, battery, screen awake, another emulator running),
launch through the play button's path, force-stop, logs, screenshots, fps, toggles, config, game
patches, button presses and routes, crash picture, guest memory dump and disassembly, and a
profiler (system sampling plus simpleperf). The rules it enforces are in `AGENTS.md` sections 5, 6,
and 9. Experiment scripts with their abort conditions live in `tools/thor/`.

## Layout

- `AGENTS.md`: the rules, the sources, the current state, and the debug loop. The single
  instruction file for every agent. `CLAUDE.md` points to it.
- `docs/research/`: dated findings, audits, and corrections. `docs/worklogs/`: one file per day.
- `docs/research/experiments.db` and `experiment-ledger.md`: the experiment ledger, dead ends
  included.
- `.agents/skills/`: task procedures.
- `tools/mcp/`, `tools/thor/`, `tools/pc/`, `tools/edram_bench/`: device control, device
  experiments, PC trace replay, GPU harness.
- `android/android_studio_project/app/src/main/assets/`: the bundled Turnip driver and the bundled
  game patches.

## Legal

Emulator research on legally owned content only. Not for piracy, bypassing access controls,
redistributing game content, or posting copyrighted assets. The repository holds no game files,
keys, dumps, or screenshots of games.
