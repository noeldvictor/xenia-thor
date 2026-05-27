# Windows PowerShell Command Hygiene Skill

## Problem

The current Windows shell for this repo is PowerShell, not Bash. A recent status
probe used Bash-style `&&` chaining:

```powershell
git status --short && git branch --show-current && git diff -- ...
```

Windows PowerShell 5.1 rejected that with:

```text
The token '&&' is not a valid statement separator in this version.
```

This class of failure wastes continuation time and can be worse when mixed
shell assumptions touch file paths, quoting, or destructive operations.

## Fix

Added repo-local skill:

` .agents/skills/xenia-windows-powershell-command-hygiene/SKILL.md`

The skill requires future command-heavy xenia-thor work to:

- assume Windows PowerShell 5.1 unless another shell is explicit;
- avoid Bash `&&`, `||`, here-docs, `export`, `rm -rf`, Bash paths, and Bash
  quoting assumptions;
- prefer one command per tool call;
- use `multi_tool_use.parallel` for independent reads;
- use newline-separated PowerShell statements with `$LASTEXITCODE` checks when
  dependent multi-step commands must stay in one call;
- quote paths with spaces and use `-LiteralPath` for path-sensitive operations;
- avoid mixed-shell destructive file operations.

## Wiring

Updated these repo-local entry points so future heartbeat/goal-loop work reads
the skill before composing commands:

- `AGENTS.md`
- `.agents/skills/xenia-working-fast-autonomy/SKILL.md`
- `.agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md`
- `.agents/skills/xenia-codex-goal-loop/SKILL.md`
- `tools/thor/thor_codex_goal_loop.ps1`

This remains repo-local per `AGENTS.md`; no global Codex skill was created.

## Validation

- The goal-loop script still parses and reports status.
- The live Blue Dragon goal loop was re-armed with the new skill in its startup
  read list.
- `git diff --check` passed, with only the repo's normal CRLF warnings.
