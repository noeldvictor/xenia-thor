---
name: xenia-codex-goal-loop
description: Use for bounded Codex Desktop continuation loops for xenia-thor goals, including Stop-hook goal guards, heartbeat automation decisions, proof markers, concise prompt re-arming, and avoiding runaway or circular sessions.
---

# Xenia Codex Goal Loop

Use this when Codex should keep working toward a concrete xenia-thor goal
without manual re-prompting every turn.

## Rule

Use bounded continuation, not an unbounded loop:

- define one proof marker that ends the loop;
- cap Stop-hook attempts;
- keep the active prompt short and current;
- store history in worklogs/research, not the loop prompt;
- write a concrete blocker when proof is not reached;
- commit and push only validated progress on the current branch.

## Prompt Budget

The goal-loop prompt must be a decision surface, not a transcript. It should fit
on one screen and include only:

- proof condition or product target;
- local skills to read, including PowerShell hygiene and remote-debug for
  Android work;
- the one or two current facts that steer the next slice;
- the lanes that must not be repeated;
- validation, worklog, research, commit/push, and re-arm expectations.

If the prompt grows into dated chronology, shorten it before re-arming. Put the
details in `docs/research/` and `docs/worklogs/`.

## Helper Commands

Status:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Status
```

Enable Android usability loop:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableAndroidUsability -MaxAttempts 48
```

Enable Blue Dragon full-speed loop only when explicitly restarted:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonFullSpeed -MaxAttempts 48
```

Disable or reset:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Disable
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Reset
```

## State Files

- Config: `scratch/thor-debug/codex-goal-loop.json`
- Attempts: `scratch/thor-debug/codex-goal-loop-state.json`
- Android usability proof: `scratch/thor-debug/latest-android-usability-proof.json`
- Blue Dragon full-speed proof:
  `scratch/thor-debug/latest-blue-dragon-full-speed-proof.json`

Do not commit scratch proof markers, device logs, screenshots, videos,
bugreports, game paths with private data, dumps, game files, or keys.

## Current Default

Android usability/compatibility is the active loop. Read:

- `AGENTS.md`
- `.agents/skills/xenia-windows-powershell-command-hygiene/SKILL.md`
- `.agents/skills/xenia-thor-remote-debug/SKILL.md`
- the global AYN Thor Xenia debug skill
- `docs/research/20260527-190000-android-remote-debug-test-rig.md`
- the one research note relevant to the active user bug

Current product target: use the real app picker, capture current screen/log
first for fresh visual/crash reports, and work on Project Sylpheed guest heap /
`RtlRaiseException`, black-frame compatibility, recent-game crash/stall
classification, or another user-reported Android UX bug.

Blue Dragon speed is paused unless explicitly restarted.

## Loop Breakers

- No third same-lane counter-only slice without a refiner pass.
- No old Blue Dragon/A64 speed history in Android usability prompts.
- No behavior claim without screenshot/video plus log evidence.
- No bugreport or Perfetto first for simple UI bugs.
- No prompt re-arm with long dated chronology.
