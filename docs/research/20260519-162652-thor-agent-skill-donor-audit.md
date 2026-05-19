# Thor Agent Skill Donor Audit

Date: 2026-05-19 16:26 EDT

## Question

Can the Thor skill patterns from the Vita3K-Thor and RPCSX UI Android Thor forks
speed up the xenia-thor Blue Dragon / AYN Thor ARM64 loop?

## Sources Checked

- Vita3K-Thor `.agents/skills`:
  https://github.com/noeldvictor/Vita3K-Thor/tree/master/.agents/skills
  - Accessed: 2026-05-19
  - Local donor commit: `4059339e7434f9f3866696ebc14ac024febdb4b8`
  - Relevant skill ideas: debug RAG, Ghidra escalation, input automation,
    performance profiler, regression ledger, render debug, experiment gate,
    Android loop, Windows render loop.
- RPCSX UI Android Thor `.agents/skills`:
  https://github.com/noeldvictor/rpcsx-ui-android-thor/tree/master/.agents/skills
  - Accessed: 2026-05-19
  - Local donor commit: `71e6a963fed5267cc263215521462420cfefba00`
  - Relevant skill ideas: ADB operator, experiment ledger, controller/route
    automation, Ghidra static lane, Vulkan audit, screenshot burst, codegen
    hotpath, Windows/Android A/B, PowerShell operator.

## License And Copying Decision

The donor repos are useful because they are adjacent Thor experiments, but
xenia-thor should not blindly copy their skill bodies or scripts. The RPCSX
checkout includes GPLv2 licensing, and the Vita3K tree includes a COPYING file.
This audit adapted concepts and workflow shapes only. New xenia-thor skills were
written specifically for Xenia, Blue Dragon, Xbox 360 PPC, A64 backend profiling,
and AYN Thor ARM64 evidence.

## Transferable Ideas

- Experiment gate: before risky toggles or code changes, search prior notes and
  close the loop with a one-variable hypothesis.
- Durable ledger: capture paths, commands, commit hashes, and outcomes must land
  in dated worklogs/research, not chat memory.
- Route proof: screenshots and route labels make progress visible and reduce
  manual "did it reach title?" ambiguity.
- Hotpath lane: speed work needs a dedicated skill tied to profiler counters,
  not mixed with renderer traces.
- Static analysis lane: Ghidra should answer a specific runtime-address
  question, not become broad game decompilation.
- ADB safety: collect device/app state and screenshots without destructive
  device cleanup.

## Xenia-Specific Differences

- Priority is AYN Thor Max ARM64, not Windows-first. Windows/x64 remains useful
  as a control microscope when a semantic gap needs it, but it should not steer
  the default loop.
- The current wall is Blue Dragon on an imported aX360e/Edge-style A64 backend,
  so the first speed skill must focus on A64 counters, guest PCs, helper
  prologue/epilogue churn, direct/indirect calls, guest-to-host transitions, and
  XMA/audio cost.
- GPU/RenderDoc skills stay important, but the latest profiler says CPU/A64 is
  the immediate speed wall unless new evidence says otherwise.

## Changes Made

- Added `.agents/skills/xenia-a64-speed-hotpath/SKILL.md`.
- Added `.agents/skills/xenia-thor-experiment-gate/SKILL.md`.
- Added `.agents/skills/xenia-blue-dragon-route-capture/SKILL.md`.
- Updated `.agents/skills/xenia-thor-war-room/SKILL.md` to route work through
  the new skills and make the profiled Blue Dragon speed run canonical.
- Updated `.agents/skills/aarch64-snapdragon-jit-port/SKILL.md`,
  `.agents/skills/xbox360-xenon-ppc-architect/SKILL.md`, and
  `.agents/skills/xenia-vulkan-adreno-renderdoc/SKILL.md` with Thor-first
  guidance and speed-profile handoffs.
- Updated `AGENTS.md` with the donor audit, new skill inventory, experiment-gate
  rule, and the AYN Thor ARM64 over Windows priority.

## Next Use

For the next Blue Dragon speed slice:

1. Read `.agents/skills/xenia-thor-war-room/SKILL.md`.
2. Run `.agents/skills/xenia-thor-experiment-gate/SKILL.md` for the planned
   hypothesis.
3. Use `.agents/skills/xenia-a64-speed-hotpath/SKILL.md` to gather a clean A64
   speed profile.
4. If the top PC/helper is unclear, use
   `.agents/skills/xbox360-xenon-ppc-architect/SKILL.md` and
   `$xenia-ghidra-android-debug` for the smallest guest static question.
5. Record result in `docs/worklogs/20260519.md` and commit/push the validated
   slice.
