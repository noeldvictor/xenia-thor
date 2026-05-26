---
name: xenia-codex-goal-loop
description: Use for bounded Codex Desktop continuation loops for xenia-thor goals, including Stop-hook goal guards, heartbeat automation decisions, proof markers, Thor/Blue Dragon autonomous follow-up, concise prompt re-arming, and avoiding runaway or circular sessions.
---

# Xenia Codex Goal Loop

Use this skill when Codex should keep working toward a concrete xenia-thor goal
without manual re-prompting every turn.

## Rule

Use bounded continuation, not an unbounded loop:

- define one proof marker that ends the loop;
- cap Stop-hook attempts;
- keep the active prompt short and current;
- store history in worklogs/research, not in the loop prompt;
- write a concrete blocker when proof is not reached;
- commit and push only validated progress on `master`.

## Prompt Budget

The goal-loop prompt must be a decision surface, not a transcript. It should fit
on one screen and include only:

- the proof condition;
- the local skills to read;
- current speed status and wall;
- closed lanes that must not be repeated;
- the next broad decision lanes;
- validation, worklog, research, commit/push, and re-arm expectations.

If the prompt grows into dated chronology, run the continual-harness refiner
before re-arming it.

## Helper Commands

Status:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Status
```

Enable full-speed Blue Dragon loop:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonFullSpeed -MaxAttempts 48
```

Disable:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Disable
```

Reset attempt counter:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Reset
```

## State Files

- Config: `scratch/thor-debug/codex-goal-loop.json`
- Attempts: `scratch/thor-debug/codex-goal-loop-state.json`
- Current speed proof: `scratch/thor-debug/latest-blue-dragon-speed-proof.json`
- Full-speed proof: `scratch/thor-debug/latest-blue-dragon-full-speed-proof.json`
- Legacy title proof: `scratch/thor-debug/latest-title-proof.json`

Do not commit scratch proof markers or device logs unless the repo already
tracks an intentionally sanitized artifact.

## Stop-Hook Output Contract

The project hook `.codex/hooks/blue_dragon_stop.ps1` returns a blocking
decision while a goal is active and no proof marker exists. If the max attempt
count is exceeded, the hook stops continuation and asks for a blocker summary.

## Full-Speed Proof

For Blue Dragon full speed, the proof marker must be backed by:

- Disc 1 launched from the known Thor SD-card path;
- visible title/opening/gameplay route evidence;
- commit, APK hash, launch cvars, screenshot, and capture paths;
- quiet speed capture with profiler data;
- clean fatal-marker search;
- sustained at least 30 FPS over a representative 180-second route segment.

Track 60 FPS as a stretch target, not the definition of success.

## Loop Breakers

Before re-arming, check whether the last slices are repeating:

- no third same-lane counter-only slice without a refiner pass;
- no speed A/B from counter-only or route-clean-only evidence;
- no narrow first-barrier or one-PC audit unless it yields a reusable backend
  rule;
- no broad Vulkan pivot while Main Thread remains the wall.

If any breaker trips, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_continual_harness_review.ps1 -Mode Window
```

Then use `.agents/skills/xenia-continual-harness-refiner/SKILL.md` to update
the prompt, skills, memory, or tools before another risky Thor run.

Current Blue Dragon loop breaker: after
`docs/research/20260526-121500-a64-static-superblock-feasibility.md`, the
route-ranking audit reports
`decision=body_dominant_a64_micro_lanes_closed_need_refiner_or_new_evidence`.
Do not re-arm another A64 speed micro-audit unless the prompt first reranks via
the continual harness window or names a genuinely different structural class
with fresh evidence.
