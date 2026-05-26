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
- `docs/research/20260526-071500-a64-offset-address-helper-prototype.md` adds
  the default-off `arm64_offset_memory_address_fastpath` prototype and launch
  plumbing. It passed `NativeCore`, `ApkShell`, parser checks, and the updated
  feasibility audit, but has no Thor route proof. Next work is FullDeploy plus
  a route-clean `-Arm64OffsetMemoryAddressFastpath true` capture with delayed
  body-time comparators `82282490,82281D28,82287788`; no quiet speed A/B until
  that enabled route is clean.
- `docs/research/20260526-073440-a64-offset-address-fastpath-route-capture.md`
  proves route safety for the enabled cvar on commit `1ed1dec66` / APK SHA
  `3195B62E66ABCE289187F6A0C4A67E621AE2A32931BBD98B790CD32CB0E33AB9`. It
  reached visible opening sky / dragon-wing with a clean fatal-marker search
  and shrank hot generated code size, but it is not speed proof.
- `docs/research/20260526-075500-a64-offset-address-fastpath-quiet-ab.md`
  records the same-APK ON/OFF/ON sandwich on commit `5ffe7d20b`. The enabled
  path is route-clean and locally positive for generated-code/body-time:
  `82282490 87168 -> 86452`, `82281D28 96908 -> 95724`, and
  `82287788 35732 -> 35712`, with both ON captures below OFF body ticks. It is
  still not sustained 30 FPS proof. Keep the cvar globally default-off, enable
  `-Arm64OffsetMemoryAddressFastpath true` explicitly in future Blue Dragon
  speed captures, and do not repeat this unchanged A/B.
- `docs/research/20260526-083000-a64-no-wrap-memory-eligibility-audit.md`
  closes further fastmem/addressing behavior until a no-wrap range counter or
  static range analysis exists.
- `docs/research/20260526-084500-a64-guarded-stub-entry-design.md` closes
  guest-call fast-entry behavior until the source has separate fast-entry
  storage plus a dirty-flush payload ABI. Do not patch behavior by replacing
  `A64Function::machine_code`, rewriting global indirection, or materializing
  `r3-r10/lr` payloads from the existing audit-only evidence.
- `docs/research/20260526-085500-a64-nonclosed-guest-state-cache-audit.md`
  identifies the only still-plausible guest-state/cache targets as non-closed
  pointer/state GPRs `r31,r30,r29,r28,r27`. Do not patch behavior from this
  offline ranking; next work is a post-promotion counter-only audit with no
  store elision, no load replacement, and explicit flush/spill accounting.
- `docs/research/20260526-092500-a64-nonclosed-gpr-cache-capture.md` closes
  that post-promotion nonclosed-cache behavior for the current route:
  `clean_hits_possible=0`, `dirty_hits_possible=0`, and high boundary/spill
  pressure.
- `docs/research/20260526-093500-a64-fast-entry-data-model-rerank.md` makes
  the next speed-adjacent lane source-only fast-entry data modeling. A
  separate fast-entry pointer plus `r3-r10/lr` payload/dirty-flush contract now
  exists, but generated behavior is absent.
- `docs/research/20260526-094500-a64-fast-entry-dirty-flush-protocol.md`
  confirms the metadata covers every intended payload slot and flush boundary,
  while payload population, dirty flush codegen, stub codegen, callsite guards,
  and fast-entry stackpoint resume remain absent. Do not patch direct callsites
  unless a later default-off stub/protocol-helper patch makes dirty flushes,
  payload population, guard/fallback, late-bound target behavior, and
  stackpoint resume explicit with normal entry unchanged.
- `docs/research/20260526-101000-a64-fast-entry-stub-skeleton.md` adds that
  default-off skeleton and keeps behavior unchanged. The protocol audit now
  reports `stub_skeleton_source_ready_but_behavior_blocked`, with direct calls,
  normal entry, global indirection, payload population, dirty flush codegen,
  stub codegen, callsite guards, and stackpoint resume all unchanged. Do not
  run speed A/B or patch runtime behavior from this source-only evidence.
- `docs/research/20260526-102500-a64-fast-entry-guard-protocol.md` adds a
  source-only guard model and audit. It proves guard blockers/inputs/decision
  helpers exist but are unused by codegen; direct callsites, payload
  population, dirty flush codegen, and stub codegen are still absent. Continue
  only with behavior-neutral payload/dirty-flush source design or switch lanes.
- `docs/research/20260526-104000-a64-fast-entry-payload-flush-plan.md` adds
  that behavior-neutral planner and audit. It proves payload/flush blockers and
  inputs exist, but the planner is unused by generated code and behavior is
  still unchanged. Do not run a speed A/B or patch callsites from this source
  state; continue only with no-op codegen protocol helpers or switch lanes.
- `docs/research/20260526-110000-a64-fast-entry-codegen-protocol.md` adds the
  no-op codegen protocol gate and audit. It composes the guard protocol and
  payload/flush plan, but generated code still does not use it. Direct calls,
  normal entry, global indirection, payload population, dirty flush codegen,
  fast-entry stub codegen, and callsite behavior remain unchanged. Do not run
  speed A/B or patch runtime behavior from this source-only evidence; switch
  lanes or add only behavior-neutral emitter planning.
- `docs/research/20260526-111500-a64-fast-entry-emitter-planning.md` adds that
  emitter planning audit and closes the current fast-entry source-only chain.
  The attachment points are mapped, but normal entry, global indirection,
  payload population, dirty flush codegen, stub codegen, and callsite behavior
  remain unchanged. Prefer a non-fast-entry structural lane or a compile-time
  route-ranking audit for a different body-dominant A64 backend class.
- `docs/research/20260526-112900-a64-route-structural-ranking.md` adds the
  route-ranking audit and sets the next lane to source-only A64 context / CR
  compare / branch lowering for mapped body-dominant blocks. Do not turn this
  ranking into behavior, Thor capture, or speed A/B by itself.
- `docs/research/20260526-114200-a64-context-cr-branch-lowering-audit.md`
  closes CR branch behavior for this route. Continue only with source-only
  scalar context load/store lowering, not branch-across-barrier or CR-store
  elision.

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
