# Blue Dragon Full-Speed Autopilot Skill

Date: 2026-05-21 20:51 EDT

## Request

Create a skill that automatically continues the Codex Desktop conversation until
Blue Dragon reaches full speed on AYN Thor.

## Decision

Added a dedicated repo-local skill:

```text
.agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md
```

This skill is the high-level continuation contract. The generic
`xenia-codex-goal-loop` skill still owns the hook mechanics, while the new
autopilot skill owns the Blue Dragon full-speed proof definition, continuation
ritual, current sprint priority, and stop conditions.

## Implementation

Updated:

- `tools/thor/thor_codex_goal_loop.ps1`
- `.agents/skills/xenia-codex-goal-loop/SKILL.md`
- `AGENTS.md`

`EnableBlueDragonFullSpeed` now tells future continuations to read:

```text
.agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md
```

before selecting the war-room, A64 speed, JIT, PPC, Ghidra, or Vulkan lane.

## Autopilot Contract

The loop remains bounded:

- proof marker:
  `scratch/thor-debug/latest-blue-dragon-full-speed-proof.json`;
- active Stop-hook config:
  `scratch/thor-debug/codex-goal-loop.json`;
- default max attempts for the armed full-speed loop: 48;
- heartbeat id: `blue-dragon-full-speed-sprint`.

The skill explicitly rejects unbounded "never stop" behavior. It continues only
until proof exists, a max-attempt/blocker condition is reached, or the next step
requires a user decision.

## Current Sprint Bias

The skill points the next continuation at the measured `82282490` wall:

- hot GPR state: `r[1]`, `r[10]`, `r[11]`, `r[28]..r[31]`;
- hot CR state: `cr6.all_equal`, `cr6.1`, `cr6.none_equal`;
- required flush points: helpers, exits, exceptions, and aliasing context
  barriers.

It also lists rejected experiments that should not be retried without new
evidence.

## Status

Tooling/skill update only. No runtime emulator behavior changed.
