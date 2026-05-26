---
name: xenia-blue-dragon-full-speed-autopilot
description: Use when Codex should keep the xenia-thor Blue Dragon full-speed sprint moving across Stop-hook continuations or Codex Desktop heartbeats, while preventing stale micro-audit loops and requiring Thor proof, committed progress, or a concrete blocker.
---

# Xenia Blue Dragon Full-Speed Autopilot

This is the top-level contract for the user's Blue Dragon goal. It should steer
the loop, not become a research archive. Detailed history belongs in
`docs/research/` and `docs/worklogs/`.

## Start Every Continuation

1. Check for proof:
   `Test-Path scratch\thor-debug\latest-blue-dragon-full-speed-proof.json`.
2. Read `AGENTS.md`, this skill,
   `.agents/skills/xenia-working-fast-autonomy/SKILL.md`, and
   `.agents/skills/xenia-codex-goal-loop/SKILL.md`.
3. Read the latest dated worklog and the research note it names as freshest.
4. Run `git status --short --branch` and check for active build/deploy/capture
   or real git operations before editing.
5. If another active slice is running, do not start a duplicate; return a quiet
   status for heartbeats.

## Full-Speed Proof

Success requires `scratch/thor-debug/latest-blue-dragon-full-speed-proof.json`
with commit, APK SHA, launch path, cvars, capture/log/screenshot paths, clean
fatal-marker search, visible title/opening/gameplay evidence, and quiet
representative 180-second speed evidence sustaining at least 30 FPS. Track 60
FPS as stretch.

## Current Strategic Reset

The latest hard speed status remains: no sustained 30 FPS proof, and Thor
captures still show Main Thread / A64 generated-code as the wall rather than
GPU Commands. The edge-payload lane for
`82282490:82282598 -> 82287788` is closed for immediate behavior work:
`docs/research/20260525-195600-edge-payload-lifetime-audit.md` found
`segments_survived_no_kill=0`, `f1_reads_before_kill=0`, and every segment
first-killed by `CONTEXT_BARRIER`.

Do not continue the old chain with another narrow "first barrier" audit unless
the next output is a general rule or a patch that changes the plan. The useful
lesson is that this payload-storage shape has no route-surviving window.

## Closed Lanes

Do not spend the next sprint slice on these unless new evidence changes the
premise:

- exact `f[1]`/edge-payload materialization for
  `82282490:82282598 -> 82287788`;
- same-block `r1` clean-load replacement, `r11` dirty caching, or post-promotion
  residual register-cache behavior from the 2026-05-25 counters;
- narrow stvewx lane folds, the three-PC `MUL_ADD_V128` shortcut, local-only
  `822824F0` vector peepholes, broad CR compare/barrier fusion, broad GPR
  caches, or old `f[1]`/fpscr behavior A/Bs;
- broad Vulkan/RenderDoc/frame-pacing pivots while Main Thread remains the
  measured wall.

## Better Next Lanes

Pick one lane with a credible FPS path:

- **Backend maturity lane:** inspect A64 register allocation, helper ABI,
  block linking, direct/indirect call dispatch, fastmem/addressing, and
  guest-state caching as structural systems. Prefer generic backend fixes over
  Blue-Dragon-only one-PC cvars. Current next backend slice is tooling, not
  behavior: `docs/research/20260526-014000-82281d28-hir-block-profile-join.md`
  found the `82281D28` runtime block-profile to printed OptHIR join unsafe.
  `docs/research/20260526-015900-a64-block-profile-metadata-mapper.md` added
  stable block metadata and disabled ordinal fallback in the main HIR reports.
  `docs/research/20260526-022000-82281d28-metadata-capture-blocker.md` then
  proved the metadata capture works but still could not map the active hot
  span. `docs/research/20260526-023500-hir-block-profile-stamps.md` added
  HIR block stamps, and
  `docs/research/20260526-030200-82281d28-block-map-capture.md` FullDeployed
  them and proved the join usable after raising Thor logcat buffers to 64 MiB:
  `metadata_mappable_rows=88`, `hir_block_mappable_rows=88`,
  `active_metadata_unmappable_rows=0`, `join_status=metadata_required`. The
  mapped hot block `82281D28:8228233C-82282370` is scalar call setup around
  `0x826BF770` and recursive `0x82281D28`, not VMX/GPU work. Do not rerun the
  unchanged mapper capture. Next step is an offline/source call-setup audit
  that can prove or reject a reusable helper ABI/direct-call/stack-argument
  rule before behavior work.
  `docs/research/20260526-032000-82281d28-call-setup-audit.md` rejects local
  store elision: both call boundaries are fully `callee_live_in`, and strict
  local promotion for `r3-r10/lr` has zero safe wins. Do not patch that block
  locally or rerun the same audit. Reopen as a generic guest-call argument
  handoff audit only if it counts broad direct-call volume and normal-entry
  fallback constraints.
  `docs/research/20260526-033000-guest-call-arg-handoff-audit.md` adds
  `tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1`; first pass on
  `82281D28` found broad caller stores but too much missing callee HIR
  (`callee_hir_missing_stores=364`) to justify fast-entry behavior. Next work
  should capture or provide broader route/file-backed callee HIR and rerun the
  tool before any ABI patch.
- **VMX128/NEON lane:** harvest hot VMX/vector patterns from the current route,
  then implement opcode-level NEON improvements only when source review and
  counters show broad hit volume and correctness tests exist. Current counters
  close broad `PERMUTE` / `LOAD_VECTOR_SHL` / `LOAD_VECTOR_SHR` behavior for
  now; reopen only with fresh body-dominant route volume outside closed
  stvewx/vmaddfp local shapes.
- **Hybrid/static lane:** use the research notes on mixed execution/static
  translation as design pressure for hot function variants, but require normal
  entry fallback and no hidden guest-visible state.
- **Measured GPU offload lane:** add counters for CPU-side Xenos bulk work
  first. Only move EDRAM resolves, format conversion, deswizzle/tile transforms,
  vertex unpack, clears, or copies to Adreno when the work can stay GPU-resident
  and counters prove it is material.
- **Android UX lane:** controller/settings/profile work is valuable and may be
  user-priority, but do not count it as Blue Dragon FPS progress.

## Loop Breakers

- At most two counter-only slices may run in the same lane without either a
  behavior patch candidate or a written lane closure.
- A Thor route capture must answer a named hypothesis. Do not capture just
  because the previous note says "next useful slice".
- Do not dump multiple huge HIR functions through logcat when exact static
  context is the output; use one-function captures, add a file-backed dump, or
  raise Thor logcat buffers first with `adb -s c3ca0370 logcat -G 64M`.
- Do not use ordinal fallback as proof that runtime block profile rows match
  printed HIR blocks. If `tools/thor/thor_hir_block_profile_join_audit.ps1`
  reports `join_status=unsafe`, fix the metadata/dump first. As of
  `docs/research/20260526-030200-82281d28-block-map-capture.md`, the current
  `82281D28` mapper proof is complete; do not rerun it unchanged.
- A speed A/B is only valid after behavior changes, audit counters are off, and
  the route/cvars/APK are matched.
- When the loop feels circular, run
  `tools/thor/thor_continual_harness_review.ps1 -Mode Window`, then refine
  `AGENTS.md`, repo skills, and `tools/thor/thor_codex_goal_loop.ps1` before
  another risky run.

## Re-Arm

Use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonFullSpeed -MaxAttempts 48
```

The prompt must stay short and current. Do not paste chronological research
history into the goal loop; name only the freshest decision note and the closed
lanes.
