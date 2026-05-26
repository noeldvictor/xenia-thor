---
name: xenia-working-fast-autonomy
description: Use when Codex should turn a xenia-thor continuation into one bounded, evidence-driven work slice that advances Blue Dragon performance, Thor device proof, Android usability, or the project harness without looping on stale audits.
---

# Xenia Working Fast Autonomy

This is the worker loop under the Blue Dragon autopilot. Each invocation should
produce one durable result, not more orientation.

## Required End State

End with one of:

- full-speed proof marker written;
- a validated code/tooling/docs patch committed and pushed;
- a Thor capture or offline analysis report with a dated worklog entry;
- a concrete blocker naming the missing input, failed command, and next
  experiment;
- a harness cleanup commit when the process itself is stale.

## Startup

Run or inspect:

```powershell
Test-Path scratch\thor-debug\latest-blue-dragon-full-speed-proof.json
git status --short --branch
Get-Content -Raw AGENTS.md
Get-Content -Raw scratch\thor-debug\codex-goal-loop.json
```

Read the newest `docs/worklogs/YYYYMMDD.md` entry and its freshest research
note. If a build/deploy/capture/real git operation is active, do not start a
parallel slice.

## Choose One Lane

Pick exactly one:

- A64/backend maturity: `.agents/skills/xenia-a64-speed-hotpath/SKILL.md`.
- AArch64 JIT parity and code-cache/ABI work:
  `.agents/skills/aarch64-snapdragon-jit-port/SKILL.md`.
- PPC/XEX/HLE/Ghidra analysis:
  `.agents/skills/xbox360-xenon-ppc-architect/SKILL.md` and
  `.agents/skills/xenia-ghidra-ooda-loop/SKILL.md`.
- Vulkan/Adreno evidence:
  `.agents/skills/xenia-vulkan-adreno-renderdoc/SKILL.md`.
- Route proof:
  `.agents/skills/xenia-blue-dragon-route-capture/SKILL.md`.
- Risky behavior change:
  `.agents/skills/xenia-thor-experiment-gate/SKILL.md`.
- Stale loop cleanup:
  `.agents/skills/xenia-continual-harness-refiner/SKILL.md`.

## Anti-Loop Rules

- No third counter-only slice in the same lane without a behavior candidate or
  lane closure.
- No Thor run without a named hypothesis and expected decision.
- No quiet speed A/B for counter-only patches.
- No one-PC or one-span fastpath unless it plausibly affects route FPS and has
  route-clean proof.
- If an audit proves zero surviving opportunities, close the lane instead of
  auditing one level deeper.

## Current Closures

Treat these as closed for immediate speed work:

- `82282490:82282598 -> 82287788` payload materialization: lifetime counters
  show every segment first-killed by `CONTEXT_BARRIER`.
- Post-promotion `r1`/`r11` register-cache behavior: residual counters showed
  no useful clean/dirty hits.
- Narrow stvewx, three-PC `MUL_ADD_V128`, standalone `f[1]`, fpscr dirty-cache,
  local-only `822824F0`, broad CR/GPR caches, and broad Vulkan pivots.

## Useful Output Bias

Prefer work that changes the slope:

- structural A64 register allocation/helper/call-linking/fastmem work;
- opcode-level VMX128/NEON lowering with tests and broad hit volume;
- Android controller/settings/profile UX when the user prioritizes playability;
- GPU offload only after counters identify CPU-side Xenos bulk transforms.

## Validation

Use the cheapest valid check:

1. Parser/tool run for docs and PowerShell analysis scripts.
2. `tools\thor\thor_build.ps1 -Mode NativeCore` for native changes.
3. `tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` for
   APK/device changes.
4. `LaunchBlueDragonSpeedCapture` only after a named runtime hypothesis exists.

Keep trace-heavy correctness captures separate from quiet speed captures.

## Close

Update the dated worklog and any research note before commit. Re-arm the goal
loop only after the prompt points at the current decision rather than old
chronology.
