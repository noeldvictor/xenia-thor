---
name: xenia-working-fast-autonomy
description: Use when Codex should keep xenia-thor moving autonomously toward working and fast Blue Dragon on AYN Thor, especially across Stop-hook continuations, heartbeats, long debug loops, ARM64 backend speed sprints, and requests to "do not stop" until proof or a concrete blocker.
---

# Xenia Working Fast Autonomy

## Overview

This skill is the bounded worker loop under the Blue Dragon full-speed
autopilot. Use it to turn each continuation into one concrete, evidence-driven
work slice instead of re-orienting forever.

## Rule

Keep going, but keep it bounded:

- stop only for the full-speed proof marker, max hook attempts, or a concrete
  blocker;
- do not end a continuation after orientation only;
- do one variable at a time;
- update dated research/worklogs;
- commit and push validated progress on `master`;
- never commit game files, keys, raw copyrighted dumps, private screenshots,
  or scratch captures.

## Autonomy Contract

Every invocation must end in one of these states:

- full-speed proof marker written and summarized;
- a validated code/tooling/docs slice committed and pushed;
- a Thor capture or offline analysis artifact with a dated worklog entry;
- a concrete blocker with the exact failed command, missing input, and next
  experiment.

If proof is absent and no user decision is needed, re-arm the Stop hook before
ending. Do not ask the user to say "continue" just because the next step is
known.

## Startup Checklist

At the start of every autonomous slice:

```powershell
Test-Path scratch\thor-debug\latest-blue-dragon-full-speed-proof.json
Get-Content -Raw AGENTS.md
Get-Content -Raw .agents\skills\xenia-blue-dragon-full-speed-autopilot\SKILL.md
Get-Content -Raw .agents\skills\xenia-codex-goal-loop\SKILL.md
Get-Content -Raw scratch\thor-debug\codex-goal-loop.json
git status --short --branch
```

Read the newest `docs/worklogs/YYYYMMDD.md` entry and any research note named
by that entry. If the proof marker exists, summarize it and recommend pausing
the heartbeat instead of editing code.

## Pick One Lane

Choose exactly one lane for the slice:

- A64 generated-code speed: use
  `.agents/skills/xenia-a64-speed-hotpath/SKILL.md`.
- AArch64 backend/JIT parity: use
  `.agents/skills/aarch64-snapdragon-jit-port/SKILL.md`.
- PPC/XEX/HLE/Ghidra guest analysis: use
  `.agents/skills/xbox360-xenon-ppc-architect/SKILL.md` and
  `.agents/skills/xenia-ghidra-ooda-loop/SKILL.md`.
- Vulkan/Adreno/presenter evidence: use
  `.agents/skills/xenia-vulkan-adreno-renderdoc/SKILL.md`.
- Route proof and capture discipline: use
  `.agents/skills/xenia-blue-dragon-route-capture/SKILL.md`.
- Risky behavior changes: run
  `.agents/skills/xenia-thor-experiment-gate/SKILL.md` first.

Prefer the lane with the newest hard evidence. Do not retry a rejected
experiment unless a new audit, disassembly, or capture explains why the old
failure would not recur.

## Minimum Useful Output

Each slice must leave at least one durable output:

- a code patch plus build result;
- a Thor capture with metadata and interpretation;
- a deterministic analysis tool or report;
- a Ghidra/HIR/profiler research note naming the next patch;
- a blocker entry with the exact missing input, failing command, capture path,
  and next experiment.

If no safe code change is obvious, improve the offline OODA loop first: parser,
report, cvar audit, disassembly map, Ghidra queue, capture summarizer, or
experiment ledger.

## Validation Ladder

Use the cheapest validation that actually answers the question:

1. Offline report or compile-only check for analysis/tooling changes.
2. `thor_build.ps1 -Mode NativeCore` for native backend changes.
3. `thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` when device proof
   is needed.
4. `LaunchBlueDragonSpeedCapture` only after the patch has a named route
   hypothesis and rollback cvar when appropriate.

Keep trace-heavy captures separate from quiet speed captures. Do not judge FPS
from runs with broad disassembly, shader dumps, live logcat, RenderDoc, or
heavy audits enabled unless the note explicitly marks the speed data invalid.

## Current Default Bias

The current best autonomous lane is `82282490` state traffic in the opening
route. Before editing, run or inspect the state-span report:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_state_span_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Top 20
```

Then run the GPR promotion audit before cross-block state work:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_gpr_promotion_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Top 20
```

Prioritize real GPR state traffic around `r[1]`, `r[10]`, `r[11]`, and
`r[28]..r[31]` before another CR6 shortcut or broad vector rewrite. Any state
cache design must name flush behavior for helpers, exits, exceptions, memory
aliases, and context barriers.

Latest evidence: the default-off
`arm64_context_value_cache_preserve_barrier` audit reached the opening route but
still produced `82282490 loads/hits=546/0` because host-register invalidation
killed reuse. The next state-cache slice should move before A64 register
allocation or pin one/two PPC GPRs, not preserve more emit-time cache entries.
The current promotion audit found `118` blocks, `546` whole-GPR loads, `562`
whole-GPR stores, `29` dominated single-predecessor blocks, and `61` first
whole-GPR loads in those blocks. Top pre-RA candidates are `r[1]`, `r[11]`,
and `r[10]`. Do not wire predecessor `Value*` objects directly into successor
blocks; the PPC translator does not run `DataFlowAnalysisPass`.

Latest runtime check: guarded local-slot promotion for `r[1]`/`r[11]` reached
the opening route without fatal markers, but increased `82282490` code size and
did not improve comparable body-time intervals. Keep
`arm64_context_promotion_gpr_local_slots` default-off. Next runtime slice:
either add exact promotion counters/code-size attribution, or try a
lower-overhead pinned-GPR path for `r[1]`/`r[11]` with resets at volatile ops,
calls, conditional branches, returns, traps, multi-predecessor joins, exits,
exceptions, and overlapping context writes.

Avoid the known rejected lanes unless new evidence changes the premise:

- broad `PERMUTE_I32` lane-replace helper;
- naive VMX dot fastpath;
- non-constant V128 store cleanup;
- generic compare-branch fusion;
- old block-local/fallthrough context cache;
- CR store elision across barriers.

## Close The Slice

Before ending a continuation:

```powershell
git diff --check
python "C:\Users\leanerdesigner\.codex\skills\.system\skill-creator\scripts\quick_validate.py" .agents\skills\xenia-working-fast-autonomy
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonFullSpeed -MaxAttempts 48
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Status
```

Commit and push validated tracked changes. If the heartbeat automation is
active, keep its prompt aligned with the latest next action. Leave the next
action in the worklog as a concrete command or patch target so the Stop hook or
heartbeat can resume without rediscovery.
