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
did not improve comparable body-time intervals. The counter follow-up showed
`103` local stores for only `3` replaced loads in `82282490`; `r[11]` produced
`68` local stores and `0` replaced loads, while `r[1]` produced `35` local
stores and `3` replaced loads. Keep
`arm64_context_promotion_gpr_local_slots` and
`arm64_context_promotion_gpr_local_slots_audit` default-off. Next runtime
check: the default-off pinned `r[1]` probe also missed. Pinned no-fallthrough
was route-clean but logged `loads/hits=107/0` and grew `82282490`; pinned
fallthrough black-stalled before the target function. Keep
`arm64_context_pinned_gpr_r1` and its fallthrough mode default-off. Next slice:
use the new `tools/thor/thor_hir_gpr_load_shape_report.ps1` result instead of
repeating the failed cache shapes. It found `76` first-in-block `r[1]` loads,
`87` multi-predecessor loads, and no aliasing stores, which means the next
useful worker slice is a CFG/live-in availability report or guarded
pre-register-allocation GPR state-cache design that proves all predecessors
leave `r[1]` clean before replacing loads.

Latest offline follow-up: `tools/thor/thor_hir_gpr_livein_report.ps1` found
that strict helper+barrier flushing exposes only `14` replaceable first `r[1]`
loads, while helper flushing with `context_barrier` preservation exposes `56`
replaceable first loads. The first guarded runtime implementation was
route-clean but too conservative: `scratch/thor-debug/20260522-152727-*` had no
searched fatal markers and logged `loads_attempted=107`,
`loads_replaced=16`, `loads_seeded=91`, `call_resets=91`, and
`82282490 code_size=87224`. Keep
`arm64_context_promotion_gpr_livein_r1` default-off. Next slice should add
actual-CFG/dirty-reason audit or improve the pre-RA carrier until the runtime
replacement count is close to the offline `56` first-load opportunity before
running another long Thor speed capture.
The first carrier fix is now in code: preserve `r[1]` state across
`branch_true` / `branch_false` for this explicit edge carrier, because their
generic volatile flag was killing state before successors. NativeCore passed.
Next slice should deploy and run an audited r1 live-in route capture, not a
quiet FPS run, and require the runtime `loads_replaced` count to approach `50+`.
That audit now passed: `scratch/thor-debug/20260522-155827-*` logged
`loads_replaced=64/107`, `branch_preserves=153`, and no searched fatal markers.
The quiet A/B is not a speed win:
`docs/research/20260522-162135-r1-livein-quiet-ab.md` shows control
`scratch/thor-debug/20260522-161306-*` reached the visible opening route and
logged `82282490` body-time rows, while live-in-on
`scratch/thor-debug/20260522-161710-*` stayed at loading and emitted no
`82282490` body-time rows after activation. Keep
`arm64_context_promotion_gpr_livein_r1` default-off. Do not repeat the exact
A/B unchanged; use one control-sandwich confirmation only if needed, otherwise
move to another `82282490` hotpath.

Next offline filter: run or inspect
`tools/thor/thor_hir_block_mix_report.ps1`. It joins `82282490` OptHIR block
shape to the prior block-profile capture. Current result: dynamic entry-count
hot blocks are the early control/state path, especially mixed block `822824F0`;
static context/vector-heavy blocks need block body-time proof before broad VMX
work. The next useful worker slice is an `822824F0` HIR/profile audit or a
lower-noise block body-time profiler, not another unchanged r1 live-in A/B.

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
