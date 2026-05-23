---
name: xenia-continual-harness-refiner
description: Use when adapting Continual Harness-style online refinement to xenia-thor, especially after repeated Blue Dragon stalls, rejected experiments, stale prompts, missing skills, or when Codex should refine AGENTS.md, repo-local skills, worklogs, research memory, and goal-loop prompts from a recent trajectory window.
---

# Xenia Continual Harness Refiner

Use this skill as a lightweight adaptation of Continual Harness for xenia-thor.
It refines our repo-local harness from evidence; it does not replace the
Stop-hook, heartbeat, or one-variable experiment gate.

## Trigger

Run a refiner pass when any of these are true:

- two recent slices in a row were inconclusive, black-idled, or repeated old
  lanes;
- the current heartbeat/Stop-hook prompt points at stale evidence;
- a capture reveals a new target and the local skills still point elsewhere;
- the same manual analysis command is being rewritten repeatedly;
- a new subsystem needs a local skill, parser, or durable memory note.

## Trajectory Window

First build or inspect the recent window:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_continual_harness_review.ps1 -Mode Window
```

Then read `scratch/thor-debug/continual-harness-window.md`.

The window is the xenia-thor equivalent of Continual Harness trajectory
history: recent worklogs, research notes, goal-loop prompt, git state, proof
marker state, and capture metadata. Use it to identify failure signatures and
stale harness instructions.

## Refiner Pass

Make conservative CRUD edits:

- Prompt/contract: update `AGENTS.md`, `tools/thor/thor_codex_goal_loop.ps1`,
  and the heartbeat prompt when the next action changes.
- Skills: create or edit repo-local `.agents/skills/*/SKILL.md` when a
  workflow is repeated or stale.
- Memory: write a dated `docs/research/YYYYMMDD-HHMMSS-topic.md` when new
  evidence changes strategy.
- Tools: add or update `tools/thor/*.ps1` only when deterministic analysis is
  being repeated manually.
- Experiments: never mark a codegen idea as accepted without route proof.

Do not import Pokémon-specific code or assumptions. The portable idea is the
online refinement loop: recent evidence -> failure signatures -> CRUD over the
agent harness -> persisted bootstrap for the next slice.

## Guardrails

- Keep all project skills repo-local.
- Keep experiment cvars default-off until route proof justifies enabling them.
- Do not overwrite user edits or unrelated worktree changes.
- Do not commit scratch windows, raw captures, screenshots, game files, keys,
  or extracted copyrighted content.
- Commit/push only validated docs, tooling, or code changes on `master`.

## Output

Every refiner pass should leave one of:

- a committed prompt/skill/tooling update;
- a dated research note explaining why no harness change is needed;
- a concrete blocker naming the stale or missing evidence.
