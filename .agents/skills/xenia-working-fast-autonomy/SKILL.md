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
- No multi-function HIR dump through logcat when exact hot-block mapping is the
  output; capture one large function at a time, use file-backed output, or set
  Thor logcat buffers to 64 MiB before launch.
- No behavior patch from runtime block-profile to HIR weighted joins unless
  `tools/thor/thor_hir_block_profile_join_audit.ps1` says the join is safe or
  a newer metadata dump proves the mapping.
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
- Broad VMX128 `PERMUTE` / `LOAD_VECTOR_SHL` / `LOAD_VECTOR_SHR` behavior from
  the 2026-05-26 route counters; vector volume was not the dominant wall and
  mostly sat in closed local shapes.
- `82281D28` weighted HIR/block-profile behavior from ordinal fallback: never
  use ordinal fallback for behavior decisions. The current mapper blocker is
  resolved by `docs/research/20260526-030200-82281d28-block-map-capture.md`,
  which reports `hir_block_mappable_rows=88` and
  `active_metadata_unmappable_rows=0` after a 64 MiB logcat capture. Do not
  rerun that mapper capture unchanged. The mapped hot block is
  `82281D28:8228233C-82282370`. The follow-up
  `docs/research/20260526-032000-82281d28-call-setup-audit.md` closes local
  store elision for that block because every setup store is live into the
  callee and strict local promotion has zero safe wins. Next work must be a
  generic guest-call argument handoff audit or a different structural lane, not
  another local call/setup capture.
- Generic guest-call argument handoff behavior is still blocked. The first
  audit in `docs/research/20260526-033000-guest-call-arg-handoff-audit.md`
  added `tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1` and found
  `callee_first_load_stores=62` but `callee_hir_missing_stores=364` for
  `82281D28`. Do not design a fast-entry variant until broader route or
  file-backed callee HIR proves body-weighted volume and normal-entry fallback
  constraints.
- The targeted coverage capture in
  `docs/research/20260526-034500-guest-call-hir-coverage-capture.md` reduces
  the blocker: corrected audit output is `callee_first_load_stores=247`,
  `callee_hir_missing_stores=126`, and `normal_entry_fallback_required=81`.
- The follow-up source audit in
  `docs/research/20260526-040500-a64-guest-call-fast-entry-feasibility.md`
  proves fast-entry is feasible only as a separate guarded entry path or stub.
  Do not replace `A64Function::machine_code()` or the global indirection slot.
- `docs/research/20260526-041500-a64-guest-call-fast-entry-audit-skeleton.md`
  adds the default-off counter-only `arm64_guest_call_fast_entry_audit`
  skeleton with Android/Thor launch plumbing. It is not speed proof. The
  route-clean follow-up in
  `docs/research/20260526-043900-a64-guest-call-fast-entry-audit-capture.md`
  found `direct_calls=84`, `eligible_regular=67`, `arg_store_fields=421`,
  and `already_compiled_targets=32`, but also `callee_first_use_known=0`,
  `callee_first_use_missing=421`, `normal_entry_fallback=67`, and
  `flush_context_barrier=260`. Do not run a quiet speed A/B, do not patch
  fast-entry behavior, and do not rerun this capture unchanged. Next slice
  must not repeat the same counter capture.
- `docs/research/20260526-045000-guest-call-callee-aware-audit.md` adds the
  callee-aware offline join. It reports `callee_first_load_stores=247`,
  `body_weighted_live_in_fields=690421033`, and
  `body_weighted_missing_fields=30011199`, with top known targets
  `0x82281D28` and `0x826BF770`. Behavior is still blocked because the runtime
  compile audit has no per-target first-use knowledge
  (`callee_first_use_known=0`) and high dirty flush pressure
  (`dirty_flush_points=268`, `flush_context_barrier=260`). Continue only with a
  runtime per-target row audit that has a hard close condition, or switch to a
  different structural A64 lane.
- `docs/research/20260526-051000-a64-guest-call-fast-entry-target-rows.md`
  adds the default-off runtime target rows and parser support. It is a
  counter-only patch. Next work is FullDeploy plus a route-clean capture for
  `0x82281D28` with the existing fast-entry audit cvars. Do not run a quiet
  speed A/B and do not patch behavior from the target-row skeleton alone.
- The target-row capture in
  `docs/research/20260526-053600-a64-guest-call-fast-entry-target-row-capture.md`
  keeps fast-entry behavior blocked by unresolved targets, normal-entry
  fallback, stackpoint sensitivity, and dirty/context-barrier flush pressure.
- `docs/research/20260526-061637-guest-stack-arg-handoff-capture.md` closes
  the narrow stack-argument handoff behavior lane. The stack-specific subset
  was too small and still had unresolved / fallback / flush blockers.
- `docs/research/20260526-063600-82281d28-load-store-offset-audit.md` shows
  the broader hot work is guest-stack `LOAD_OFFSET` / `STORE_OFFSET` traffic,
  not a direct handoff lane.
- `docs/research/20260526-070000-a64-memory-lowering-feasibility.md` keeps the
  fastmem/addressing lane open only as a constrained backend helper: preserve
  32-bit guest wrap in `x0`, byte swap, MMIO / exception visibility,
  store-watch expectations, large-page threshold semantics, and fallback
  behavior. Do not run a quiet speed A/B from this source audit alone.

## Useful Output Bias

Prefer work that changes the slope:

- structural A64 register allocation/helper/call-linking/fastmem work;
- deterministic profiling/HIR metadata tools when current evidence cannot be
  safely joined;
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
