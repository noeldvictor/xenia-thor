# Codex Hooks For The Blue Dragon Loop

Date: 2026-05-19 13:35 EDT

## Question

Is there something like Claude Code hooks that can make Codex keep pushing
toward the Blue Dragon title-screen goal?

## Short Answer

Yes. Codex has hooks, and the useful one for this goal is the `Stop` hook.
Official Codex docs describe hooks as shell commands triggered by lifecycle
events, configured globally in `~/.codex/config.toml` or per project in
`.codex/hooks.json`.

Source: <https://developers.openai.com/codex/hooks>

For `Stop`, Codex expects JSON on stdout. Returning this shape tells Codex to
continue with the hook reason as the next prompt:

```json
{
  "decision": "block",
  "reason": "Run one more pass over the failing tests."
}
```

The docs also say `continue: false` takes precedence and stops continuation.
That gives us a bounded autonomous loop instead of an endless runaway.

## Repo Implementation

Added:

- `.codex/hooks.json`
- `.codex/hooks/blue_dragon_stop.ps1`

The hook is passive by default. It only tries to auto-continue if this flag
exists:

```text
scratch/thor-debug/continue-until-blue-dragon-title.flag
```

It stops continuing if this proof marker exists:

```text
scratch/thor-debug/latest-title-proof.json
```

It also caps itself at 12 Stop-hook attempts and then asks Codex to summarize
the blocker. This is intentional: ADB/device disconnects, bad APK installs, or
new native crashes should turn into a concrete status report rather than an
infinite loop.

## Current Proof Marker

The current title proof marker was written to:

```text
scratch/thor-debug/latest-title-proof.json
```

It points at the clean title capture:

- `scratch/thor-debug/20260519-132943-screenshot.png`
- `scratch/thor-debug/20260519-132943-meta.txt`
- `scratch/thor-debug/20260519-132943-live-logcat-filtered.txt`

## Codex App Automations

Codex app automations are the other option. Official docs describe recurring
background tasks, project-scoped runs, and heartbeat-style thread automations
that wake up the same conversation on a schedule.

Source: <https://developers.openai.com/codex/app/automations>

Use hooks for local turn-end guardrails and short autonomous continuation. Use a
thread heartbeat automation when we want Codex to wake this conversation up
again later and continue polling, rebuilding, or triaging.

## Recommendation

For Blue Dragon:

- Keep the project `Stop` hook passive unless a flag file is intentionally
  created.
- Use the hook for bounded continuation inside an active Codex session.
- Use a Codex thread automation only when we want scheduled follow-up while a
  long device/build/debug loop is expected to continue later.
- Keep writing dated worklogs and proof markers so the loop has a clear stop
  condition.
