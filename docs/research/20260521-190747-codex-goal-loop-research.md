# Codex Goal Loop Research Refresh

Date: 2026-05-21 19:07 EDT

## Question

Can we create a tool or skill so Codex Desktop keeps working until a xenia-thor
goal is reached, similar to Claude Ralph Loop / hooks?

## Short Answer

Yes, but use a bounded loop. Codex has a `Stop` hook that can tell Codex to
continue by returning JSON with `decision: "block"` and a reason. Codex app
also has thread automations for heartbeat-style wakeups in the same
conversation. The useful xenia-thor pattern is:

1. Use a project Stop hook for active-session continuation.
2. Use a thread automation only for scheduled wakeups.
3. Put the repeatable procedure in a repo-local skill.
4. Require a proof marker and max attempts so the loop stops on success or a
   concrete blocker.

## Sources Checked

Access date: 2026-05-21.

- Codex hooks: <https://developers.openai.com/codex/hooks>
  - The `Stop` event accepts JSON on stdout.
  - Returning `{"decision":"block","reason":"..."}` makes Codex continue with
    the reason as a new continuation prompt.
  - `continue: false` takes precedence over continuation decisions.
- Codex app automations: <https://developers.openai.com/codex/app/automations>
  - Thread automations are heartbeat-style recurring wakeups attached to the
    current thread.
  - Automations can use plugins and skills, and prompts should describe how to
    decide when to stop or ask for input.
- Codex skills: <https://developers.openai.com/codex/skills>
  - Skills package instructions, resources, and optional scripts for reliable
    reusable workflows across Codex CLI, IDE extension, and app.
- Claude Code hooks: <https://code.claude.com/docs/en/hooks>
  - Claude supports `Stop` hooks and prompt/agent hooks that can evaluate
    whether the agent should stop.
- Ralph Loop plugin: <https://claude.com/plugins/ralph-loop>
  - Ralph Loop uses a stop hook to intercept session exits and re-feed the
    prompt while preserving file and git state.

## Decision For This Repo

Keep the existing `.codex/hooks.json` Stop hook path, but make it easier and
safer to control:

- Add `tools/thor/thor_codex_goal_loop.ps1`.
- Add repo-local skill `.agents/skills/xenia-codex-goal-loop/SKILL.md`.
- Extend `.codex/hooks/blue_dragon_stop.ps1` to read
  `scratch/thor-debug/codex-goal-loop.json`.
- Preserve the older Blue Dragon title flag path for compatibility.

## Operational Shape

Enable current Blue Dragon speed/opening loop:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonOpeningSpeed
```

Check state:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Status
```

Disable:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Disable
```

## Safety Notes

- Do not run unbounded "never stop" loops.
- Stop after the configured max attempts and summarize blocker evidence.
- Do not let background automations collide with active local edits.
- Keep all proof markers and device logs under ignored `scratch/` unless they
  are explicitly sanitized and intentionally tracked.
