# Working-Fast Autonomy Refresh

Date: 2026-05-22 11:23 EDT

Branch: `master`

Starting HEAD: `0fc12af3b`

## Goal

Respond to the request to not stop and keep working autonomously until Blue
Dragon is working and fast on AYN Thor, while keeping the loop bounded by proof,
max attempts, and concrete blockers.

## Decision

Do not add a duplicate autonomy skill. The repo already has
`.agents/skills/xenia-working-fast-autonomy/SKILL.md`, which is the right
concrete worker loop under `.agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md`.
Duplicating it would make future continuations choose between two similar
skills and slow the loop down.

Instead, refresh the existing skill so it is stricter:

- a continuation cannot end after orientation only;
- every invocation must leave proof, a committed slice, a capture/report, or a
  blocker with the exact next experiment;
- if proof is absent and no user decision is needed, re-arm the Stop hook;
- the current default next runtime patch is the `82282490` guarded load-only
  local-slot/data-flow GPR promotion for `r[1]` and `r[11]`;
- the skill now names `tools/thor/thor_hir_gpr_promotion_audit.ps1` before
  cross-block state work.
- `tools/thor/thor_codex_goal_loop.ps1 -Mode EnableBlueDragonFullSpeed` now
  emits the same no-orientation-only rule and current `r[1]`/`r[11]` target, so
  the live Stop-hook loop and heartbeat do not drift from the skill.

## Status

`succeeded` as a repo-local skill refresh. This does not itself improve FPS; it
keeps the next autonomous continuation from rediscovering the same loop-control
and GPR-promotion evidence.
