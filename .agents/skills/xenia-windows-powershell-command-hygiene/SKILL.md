---
name: xenia-windows-powershell-command-hygiene
description: Use whenever Codex runs shell commands in xenia-thor on Windows PowerShell, edits PowerShell scripts, composes Git/ADB/Gradle/Thor helper commands, or recovers from shell syntax failures such as Bash-style &&, ||, quoting, redirection, or mixed-shell path handling.
---

# Xenia Windows PowerShell Command Hygiene

This repo usually runs through Windows PowerShell 5.1. Treat it as PowerShell,
not Bash, unless the tool explicitly says otherwise.

## Hard Rules

- Do not use Bash command chaining such as `&&` or `||`.
- Do not assume Bash quoting, globbing, subshells, here-docs, `export`, `touch`,
  `cat > file`, `rm -rf`, or `/c/...` paths.
- Prefer one command per tool call. For independent reads, use
  `multi_tool_use.parallel`; for dependent steps, run a second command after
  checking the first result.
- When a single PowerShell call really needs multiple dependent steps, use
  newline-separated PowerShell statements plus explicit failure checks:

```powershell
git status --short
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
git branch --show-current
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
```

- Run repo PowerShell scripts with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\script.ps1 -Mode Example
```

- Quote paths with spaces by passing arguments, `-LiteralPath`, or call
  operator syntax:

```powershell
Get-Content -LiteralPath "C:\path with spaces\file.txt" -Raw
& "C:\path with spaces\script.ps1" -Mode Status
```

- Do not pipe generated path strings into destructive commands. Resolve and
  verify paths first; use native PowerShell cmdlets with `-LiteralPath`.
- Use `Set-Content` / `Add-Content` only for generated artifacts when
  `apply_patch` is not the right tool. For repo edits, use `apply_patch`.
- For ADB commands, pass arguments as arrays when scripting and include
  `-DeviceSerial` / `adb -s <serial>` whenever more than one device could exist.
- For screenshots and binary ADB streams, use the repo helper or a known-safe
  `cmd /c "adb exec-out screencap -p > file.png"` pattern; do not pipe binary
  PNG data through PowerShell text cmdlets.

## Common Replacements

- `cmd1 && cmd2` -> two tool calls, or newline plus `$LASTEXITCODE` checks.
- `grep` -> `Select-String`; prefer `rg` for repository text search.
- `cat file` -> `Get-Content -Raw file`.
- `ls` -> `Get-ChildItem`.
- `rm -rf path` -> `Remove-Item -LiteralPath path -Recurse -Force` only after
  confirming the resolved path is inside the intended workspace.
- `VAR=value command` -> `$env:VAR = "value"` before the command, then restore
  or remove it if needed.

## Before Committing

If this skill was updated because of a command failure, leave a dated worklog
entry naming the failure class and the guardrail added. Validate with
`git diff --check`; PowerShell CRLF warnings alone are acceptable for this repo.
