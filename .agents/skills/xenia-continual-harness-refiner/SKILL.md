---
name: xenia-continual-harness-refiner
description: Use when adapting Continual Harness-style online refinement to xenia-thor, especially after repeated Blue Dragon stalls, rejected experiments, stale prompts, missing skills, or when Codex should refine AGENTS.md, repo-local skills, worklogs, research memory, tooling, and goal-loop prompts from a recent trajectory window.
---

# Xenia Continual Harness Refiner

Use this skill as the hard reset valve for xenia-thor. It adapts the portable
parts of Continual Harness: recent trajectory window, failure signatures,
separate prompt/skill/memory/tool evolution passes, and conservative CRUD over
the harness. It does not change emulator behavior directly.

## Trigger

Run a refiner pass when any of these are true:

- two recent slices in a row were inconclusive, black-idled, or repeated the
  same lane;
- three consecutive slices produced only counter rows with no route-speed
  decision;
- the user says the work is slow, circular, stale, or asks to re-evaluate;
- the current heartbeat/Stop-hook prompt points at stale evidence;
- a capture reveals a new target and the local skills still point elsewhere;
- a skill or prompt has become a chronological ledger instead of a decision
  surface;
- a deterministic analysis command is being rewritten by hand repeatedly.

## Trajectory Window

First build or inspect the recent window:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_continual_harness_review.ps1 -Mode Window
```

Then read `scratch/thor-debug/continual-harness-window.md`.

The window is the xenia-thor trajectory history: recent worklogs, research
notes, goal-loop prompt, git state, proof markers, capture metadata, and stale
next-action text. Use it to identify the failure signature before editing.

## Refiner Passes

Run the passes separately. A failure in one pass should not block useful edits
in another.

- **Prompt/plan:** compress `AGENTS.md`, `tools/thor/thor_codex_goal_loop.ps1`,
  and heartbeat prompts to current facts, closed lanes, and next decision
  rules. Remove chronology.
- **Skills:** edit or create repo-local `.agents/skills/*/SKILL.md` for
  repeated workflows. Keep skills procedural and short.
- **Memory:** write a dated `docs/research/YYYYMMDD-HHMMSS-topic.md` for new
  strategic evidence, source research, or lane closures.
- **Tools:** add or update `tools/thor/*.ps1` only when repeated manual
  analysis should become deterministic.
- **Deletion/merge:** delete or merge a skill when it has no unique trigger,
  duplicates another skill, mostly stores dated history, or cannot name a
  durable output.

Do not import Pokemon-specific code or assumptions from Continual Harness. The
portable idea is: recent evidence -> failure signatures -> CRUD over harness
stores -> persisted bootstrap for the next slice.

## Guardrails

- Keep all project skills repo-local.
- Keep active prompts and skills concise; history belongs in research/worklogs.
- Keep experiment cvars default-off until route proof justifies enabling them.
- Never turn a counter-only audit into a speed claim.
- Do not run a third same-lane counter-only slice without a harness refiner
  pass.
- Do not overwrite user edits or unrelated worktree changes.
- Do not commit scratch windows, raw captures, screenshots, game files, keys,
  or extracted copyrighted content.
- Commit/push only validated docs, tooling, or code changes on `master`.

## Blue Dragon Reset Rule

If the active next action is another narrow audit in a lane that has already
failed to prove speed, first ask whether it can produce a reusable backend rule.
If not, close the lane and move to a broader structural path: A64 backend
maturity, VMX128/NEON lowering, mixed/static hot-function design, measured GPU
bulk-work offload, or Android product UX.

Current Blue Dragon refiner output:
`docs/research/20260526-123000-continual-rerank-after-a64-closures.md` chooses
evidence refresh, not another A64 micro-audit. Default next is a best-current
quiet route proof refresh with the positive offset fastpath enabled, or Android
controller/settings UX if the user prioritizes playability. Do not reopen A64
micro-lanes until fresh route evidence changes the wall.

## Output

Every refiner pass should leave one of:

- a committed prompt/skill/tooling update;
- a dated research note explaining why no harness change is needed;
- a concrete blocker naming the stale or missing evidence and the skill/prompt
  that must be changed next.
