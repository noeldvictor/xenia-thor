# Working-Fast Autonomy Skill

Date: 2026-05-21 21:12 EDT

## Goal

The user asked for Codex to keep working autonomously until Blue Dragon works
and runs fast on the AYN Thor. The existing full-speed autopilot skill defined
the goal and proof marker, but it still allowed future continuations to spend
too much time re-reading context without producing a concrete artifact.

## Decision

Add `.agents/skills/xenia-working-fast-autonomy/SKILL.md` as the concrete
worker loop under the Blue Dragon full-speed autopilot.

The skill requires each continuation to leave at least one durable output:

- a code patch plus build result;
- a Thor capture with metadata and interpretation;
- deterministic analysis tooling or report output;
- a Ghidra/HIR/profiler research note naming the next patch;
- a blocker with the exact failing command, missing input, capture path, and
  next experiment.

This keeps the loop aggressive without making it unbounded. The Stop hook and
heartbeat still end on the full-speed proof marker, max attempts, or a concrete
blocker.

## Wiring

- `xenia-blue-dragon-full-speed-autopilot` now tells continuations to read the
  new worker skill.
- `xenia-codex-goal-loop` now describes the worker output requirement.
- `tools/thor/thor_codex_goal_loop.ps1 -Mode EnableBlueDragonFullSpeed` now
  includes the worker skill in the generated continuation prompt.
- `AGENTS.md` documents the new skill and the full-speed hook sequence.

## Current Priority

The worker skill carries forward the current evidence: Blue Dragon opening
route is still dominated by `82282490`, and the next useful patch should reduce
real GPR state traffic around `r[1]`, `r[10]`, `r[11]`, and `r[28]..r[31]`.
CR6 shortcuts, broad vector rewrites, and old context-cache probes remain
rejected unless new evidence changes their premise.

## Validation Plan

The new worker skill, full-speed autopilot skill, and Codex goal-loop skill all
passed `quick_validate.py`. `git diff --check` reported only existing
line-ending normalization warnings.

The full-speed loop was re-armed with `-Mode EnableBlueDragonFullSpeed
-MaxAttempts 48`. The Stop hook emitted `decision=block` with a prompt naming
`.agents/skills/xenia-working-fast-autonomy/SKILL.md`, then the validation
attempt counter was reset.

The heartbeat automation `blue-dragon-full-speed-sprint` was updated to read
the new worker skill and require a concrete artifact or blocker on each slice.
