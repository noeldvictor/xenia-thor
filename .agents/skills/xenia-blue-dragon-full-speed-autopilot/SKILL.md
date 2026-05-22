---
name: xenia-blue-dragon-full-speed-autopilot
description: Use when Codex should automatically continue the xenia-thor Blue Dragon full-speed sprint across Stop-hook continuations and Codex Desktop heartbeats until AYN Thor reaches the full-speed proof marker or a concrete blocker is recorded.
---

# Xenia Blue Dragon Full-Speed Autopilot

This is the repo-local autopilot contract for the user's Blue Dragon goal. Use
it when the user says to keep going, continue automatically, do not stop until
full speed, or asks how Codex should keep the conversation alive.

## Non-Negotiables

- Work on `master` only.
- Keep this fork framed as an unofficial, unstable research experiment.
- Use only repo-local skills for new project-specific behavior.
- Keep the Stop hook bounded; never create an unbounded infinite loop.
- Update dated worklogs and research notes for every meaningful work slice.
- Commit and push validated progress often.
- Do not commit game files, extracted XEX files, keys, private screenshots, or
  raw copyrighted content.

## Arm The Conversation

First check whether the proof marker already exists:

```powershell
Test-Path scratch\thor-debug\latest-blue-dragon-full-speed-proof.json
```

If proof does not exist, arm the active-session Stop hook:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonFullSpeed -MaxAttempts 48
```

Then check the hook config:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Status
```

The expected config is:

- `goal_name`: `blue-dragon-full-speed`
- `proof_path`: `scratch/thor-debug/latest-blue-dragon-full-speed-proof.json`
- `enabled`: `true`

## Heartbeat Use

Use the Codex Desktop heartbeat automation as a later-wakeup safety net, not as
the tight inner loop. The active project Stop hook should do the immediate
continuation when the chat is live.

The heartbeat id is:

```text
blue-dragon-full-speed-sprint
```

If the heartbeat exists, keep its prompt aligned with this skill. If the proof
marker exists, summarize proof and recommend deleting or pausing the heartbeat.

## Full-Speed Proof

Only stop the autopilot for success after writing:

```text
scratch/thor-debug/latest-blue-dragon-full-speed-proof.json
```

The proof JSON must include:

- commit hash;
- APK SHA-256;
- Blue Dragon launch path on the Thor SD card;
- capture directory;
- speed logcat path;
- screenshot path;
- launch mode and cvars;
- visible route label: `title`, `opening`, or `gameplay`;
- quiet speed-capture evidence;
- sustained FPS evidence for at least a representative 180-second segment;
- searched fatal-marker result.

Treat `>=30 FPS` as the first full-speed proof target for this route. Track
`60 FPS` as a stretch target when the title and route allow it.

## Each Continuation

At the start of every continuation:

1. Read `AGENTS.md`.
2. Read this skill and `.agents/skills/xenia-codex-goal-loop/SKILL.md`.
3. Read the latest `docs/worklogs/YYYYMMDD.md`.
4. Check `scratch/thor-debug/codex-goal-loop.json`.
5. Check for the proof marker.
6. Run `git status --short --branch`.

If proof is absent, pick exactly one next lane:

- use `.agents/skills/xenia-a64-speed-hotpath/SKILL.md` for FPS and generated
  CPU hot paths;
- use `.agents/skills/aarch64-snapdragon-jit-port/SKILL.md` for backend parity,
  thunks, code cache, generated code, or ARM64 lowering;
- use `.agents/skills/xbox360-xenon-ppc-architect/SKILL.md` for guest PPC,
  kernel/HLE, XEX imports, or Ghidra guest analysis;
- use `.agents/skills/xenia-vulkan-adreno-renderdoc/SKILL.md` for GPU,
  presenter, PM4, Vulkan, RenderDoc, or Adreno evidence;
- use `.agents/skills/xenia-thor-experiment-gate/SKILL.md` before risky
  behavior changes.

## Current Best Next Move

As of the latest sprint, `82282490` remains the opening-scene body-time wall.
The offline HIR reports now map context offsets to PPC state names and
barrier/span churn. Prioritize a real GPR/CR state-traffic reduction plan for:

- `r[1]`, `r[10]`, `r[11]`, `r[28]`, `r[29]`, `r[30]`, `r[31]`;
- CR6 fields: `cr6.all_equal`, `cr6.1`, `cr6.none_equal`;
- explicit flushes at helpers, exits, exceptions, and aliasing barriers.

Use `tools/thor/thor_hir_state_span_report.ps1` before editing A64 state-cache
code. Current `82282490` signal: `r[1]` is the leading cross-span repeated
load, while `r[11]`, `r[10]`, and `r[31]` are leading cross-span
load-after-store GPRs.

Do not restart the rejected broad `PERMUTE_I32` lane-replace helper, naive VMX
dot-product fastpath, non-constant V128 store cleanup, generic compare-branch
fusion, or old block-local/fallthrough context cache without new evidence.

## Stop Conditions

Stop and notify the user only when:

- the proof marker exists;
- max Stop-hook attempts are exhausted;
- ADB/device/build state is blocking further progress;
- two experiments in the same subsystem fail or go inconclusive and the next
  step needs new instrumentation or a user decision;
- continuing would require unsafe, copyrighted, or unsupported material.

Otherwise continue through the Stop hook or heartbeat using the most recent
evidence.
