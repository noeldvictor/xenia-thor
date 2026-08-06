# xenia-thor

**An AI experiment, not an emulator you should use.**

`xenia-thor` is a personal fork of Xenia where essentially all of the work is
done by an agentic AI coding assistant, aimed at one device: the AYN Thor /
Thor Max (Snapdragon 8 Gen 2, Adreno 740, Android ARM64). The point of the
repository is the experiment — can an AI agent do sustained, measured, low-level
emulator engineering — not the resulting binary.

It is **extremely unstable**. It breaks from commit to commit, on purpose.

---

## Read this before doing anything else

**There is no support. None. Not from me, and absolutely not from upstream.**

- **Do not ask the Xenia project, the Xenia Discord, or any emulator community
  for help with this fork.** They did not write it, they cannot fix it, and it
  is not their responsibility.
- **Do not report anything from this fork upstream.** Crashes here are almost
  always mine, not theirs.
- **Do not cite this as evidence that Xenia supports Android or the AYN Thor.**
  It does not.
- **Do not treat anything here as a compatibility claim.** A game "working" in
  one commit means it worked once, on one device, on one scene.
- **There are no releases, no builds, and no install instructions for users.**
  If you want to run it: build it yourself, debug it yourself, keep the pieces.

If it breaks, that is the expected outcome. You are on your own — by design.

## Thanks to the actual Xenia developers

Everything of value underneath this fork is theirs. Years of research,
engineering, and open-source work made it possible for an AI agent to have
something worth experimenting on at all.

- [xenia-project/xenia](https://github.com/xenia-project/xenia)
- [xenia.jp](https://xenia.jp/)

Please respect their time. **This fork is not their problem.**

---

## What the experiment is actually about

One device, one architecture, measured changes:

- **CPU:** the ARM64 (`a64`) JIT backend and an LLVM whole-function recompiler.
  Most of the current work is finding places where the backend inherited **x86's
  two-operand destructive model** from the x64 backend it was derived from, and
  rethinking them for AArch64's three-operand, non-destructive ISA.
- **GPU:** Vulkan on Mesa Turnip, targeting the Adreno 740's TBDR behaviour.
- **Method:** every performance claim has to survive a real measurement, or it
  gets recorded as unproven and dropped.

The measurement discipline matters more than any individual change, because
run-to-run drift on this device (~2.8%) is larger than most effects being
tested. Comparing two builds cannot resolve them. So: one session, both arms
behind a cvar, equal thermal starts, a drift-control arm, frame cap **off**, and
guest-throughput as the metric rather than fps — a frame cap hides exactly the
CPU headroom you are trying to measure.

Negative results are recorded as carefully as wins, in
`docs/research/experiments.db` (`python tools/exp_ledger.py check "<keyword>"`).
Several plausible optimisations in this repo are marked DEAD **because they were
measured**, not because they were untried.

## Building

There are no prebuilt APKs. The Android build:

```bash
# The path must not contain spaces; a junction avoids that.
cmd /c mklink /J C:\xt "<repo>"
cmd /c "C:\xt\android\android_studio_project\gradlew.bat -p C:\xt\android\android_studio_project :app:assembleGithubDebug"
```

Output lands in `android/android_studio_project/app/build/outputs/apk/`.

Desktop builds (used for GPU structure work, not for the device) go through
premake + MSBuild; see `CLAUDE.md`.

## Device work

Helper scripts for the Thor live in `tools/thor/` and `tools/thor_launch.sh`.
Two traps worth knowing if you ignore the advice above and try anyway:

- A bare `adb shell am start` runs the **Qualcomm** driver, not Turnip, and
  silently invalidates every GPU measurement. `tools/thor_launch.sh` passes the
  driver extras explicitly and verifies the device prints as
  `Turnip Adreno (TM) 740`.
- If the panel is asleep the activity never gets a surface and every frame is
  dropped, while a screenshot returns pure black and the emulator looks broken.
  Check `dumpsys power | grep mWakefulness` first.

## Layout

- `CLAUDE.md` — the working agreement, device facts, and hard-won gotchas.
- `AGENTS.md` — agent instructions.
- `docs/research/` — findings, audits, and post-mortems.
- `docs/research/experiments.db` — the experiment ledger, including dead ends.
- `.agents/skills/` — task-specific procedures.

## Legal

Emulator research on legally owned content only. Not for piracy, bypassing
access controls, redistributing game content, or posting copyrighted assets.

**Again: an AI experiment. No support, no releases, no promises. Do it
yourself.**
