---
name: xenia-codex-goal-loop
description: Use for bounded Codex Desktop continuation loops for xenia-thor goals, including Stop-hook goal guards, heartbeat automation decisions, proof markers, Thor/Blue Dragon autonomous follow-up, and avoiding runaway "never stop" sessions.
---

# Xenia Codex Goal Loop

Use this skill when the user wants Codex to keep working toward a concrete
xenia-thor goal without manually re-prompting every turn.

## Rule

Use bounded continuation, not an unbounded loop:

- opt in with an explicit config or flag;
- define one proof marker that ends the loop;
- cap Stop-hook attempts;
- write a concrete blocker when proof is not reached;
- keep dated worklogs and research notes current;
- commit and push only validated progress on `master`.

## Choose The Mechanism

Use the project Stop hook for active-session continuation:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonOpeningSpeed
```

Use a Codex thread automation when the work should wake this same chat later
on a schedule, especially after a long build, device run, download, or manual
pause. The automation prompt must name the same proof marker and stop/blocker
rules as the Stop hook.

Do not use a background automation for tight build/deploy loops if the current
thread can continue through the Stop hook; it adds latency and can collide with
local edits.

## Helper Commands

Status:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Status
```

Enable title proof loop:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonTitle
```

Enable current speed/opening loop:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonOpeningSpeed
```

Enable full-speed research loop:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonFullSpeed -MaxAttempts 48
```

For Blue Dragon full-speed work, also use
`.agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md`. That skill is
the higher-level contract for Stop-hook continuation, heartbeat wakeups, proof
markers, and current sprint priorities.

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

The project hook `.codex/hooks/blue_dragon_stop.ps1` returns this shape while a
goal is active and no proof marker exists:

```json
{
  "decision": "block",
  "reason": "Continue the configured goal..."
}
```

That causes Codex to continue the turn with the reason as the next prompt. If
the max attempt count is exceeded, the hook stops continuation and asks for a
blocker summary.

## Goal Prompt Checklist

Every loop prompt should say:

- which route or performance proof ends the loop;
- which local skills to use;
- whether to build, deploy, and capture on Thor;
- which logs, screenshots, APK hashes, cvars, or FPS data prove progress;
- when to stop and summarize a blocker instead of guessing again.

For Blue Dragon full speed, define the stop condition concretely:

- launch Disc 1 from the known Thor SD-card path;
- show title/opening/gameplay route evidence;
- record commit, APK hash, launch cvars, screenshot, and capture paths;
- use a quiet speed capture with profiler data;
- require sustained at least 30 FPS over a representative 180-second route
  segment, with 60 FPS tracked as a stretch target when the game itself allows
  it.
