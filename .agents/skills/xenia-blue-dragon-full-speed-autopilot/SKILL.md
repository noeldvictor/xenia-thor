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

Latest route-structural closure:
`docs/research/20260526-115700-a64-scalar-context-load-store-lowering.md`
closes scalar context load/store behavior for the current mapped wall blocks.
A64 context loads/stores already lower to direct `x20 + offset` memory ops,
`CONTEXT_BARRIER` is a no-op in generated A64, `82281D28:8228233C-82282374`
stores are guest-call ABI live-ins, and the guest-stack offset side is already
covered by the route-clean positive offset helper. Do not continue this as
`STORE_CONTEXT` elision, `LOAD_CONTEXT` replacement, context-barrier fusion,
CR-store elision, guest-stack handoff, fast-entry behavior, or host-pointer
immediate fastmem.

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
- current scalar context load/store lowering for the mapped wall blocks unless
  a broader CFG/static-superblock or alternate-entry visibility contract is the
  explicit slice;
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
  `docs/research/20260526-034500-guest-call-hir-coverage-capture.md` ran that
  targeted coverage capture and fixed the audit's primary-vs-extra log split.
  Corrected coverage is now `callee_first_load_stores=247` and
  `callee_hir_missing_stores=126` for `82281D28`, with the body-weighted top
  call boundaries ABI-live.
  `docs/research/20260526-040500-a64-guest-call-fast-entry-feasibility.md`
  then proves fast-entry is source-feasible only as a separate guarded path or
  stub. Do not replace normal `machine_code()` or the global indirection slot.
  `docs/research/20260526-041500-a64-guest-call-fast-entry-audit-skeleton.md`
  adds that default-off counter-only skeleton. It is not speed proof and does
  not change generated behavior. The follow-up
  `docs/research/20260526-043900-a64-guest-call-fast-entry-audit-capture.md`
  FullDeployed and route-tested it for `0x82281D28`: `direct_calls=84`,
  `eligible_regular=67`, `arg_store_fields=421`, and
  `already_compiled_targets=32`, but `callee_first_use_known=0`,
  `callee_first_use_missing=421`, `normal_entry_fallback=67`, and
  `flush_context_barrier=260`. Do not patch fast-entry behavior or rerun the
  same capture unchanged.
  `docs/research/20260526-045000-guest-call-callee-aware-audit.md` makes the
  offline HIR audit callee-aware and joins the compile-audit row. It finds
  material known live-in volume (`callee_first_load_stores=247`,
  `body_weighted_live_in_fields=690421033`) but still blocks behavior because
  the runtime compile audit has `callee_first_use_known=0` and high flush
  pressure (`dirty_flush_points=268`, `flush_context_barrier=260`). Do not run a
  quiet speed A/B. If this lane continues, the next slice must add runtime
  per-target fast-entry rows and close the lane if flush pressure remains
  unmanageable.
`docs/research/20260526-051000-a64-guest-call-fast-entry-target-rows.md`
adds those default-off runtime rows and parser support. It is still
behavior-neutral. Next work is a route-clean Thor capture with the existing
fast-entry audit cvars for `0x82281D28`, not a speed A/B.
`docs/research/20260526-053600-a64-guest-call-fast-entry-target-row-capture.md`
captures that route. It confirms broad known live-in volume in the joined HIR
audit, but runtime target rows still have high unresolved-target, normal-entry
fallback, stackpoint, and dirty/context-barrier flush pressure. Do not patch
guest-call fast-entry behavior, do not run a quiet speed A/B, and do not rerun
the same capture unchanged. Reopen fast-entry only with a source-level guarded
stub / late-bound-entry design that handles normal-entry fallback, global
indirection, stackpoint/debug/exception visibility, and dirty flushes; otherwise
move to a hot-block A64 codegen-floor/disassembly audit.
`docs/research/20260526-054200-82281d28-hot-block-codegen-floor.md` ran that
audit. The mapped block `82281D28:8228233C-82282370` is scalar guest-stack
argument handoff (`load_offset.1=6`, `store_context=13`) around calls to
`0x826BF770` and recursive `0x82281D28`; it is not VMX/GPU work. ARM64 Capstone
disassembly was unavailable, so this is HIR/source codegen-floor evidence only.
Do not patch behavior from it. The next useful slice is a default-off
counter-only guest-stack argument handoff audit for direct guest calls, counting
`LOAD_OFFSET` from `r1 + constant` into `STORE_CONTEXT r3-r10/lr` before calls
with target/callsite rows, body weighting, normal-entry fallback, and blocker
classes.
`docs/research/20260526-055500-guest-stack-arg-handoff-audit-skeleton.md`
adds that audit surface plus Android/Thor plumbing and parser support. It is
behavior-neutral and not speed proof. Next work is FullDeploy plus a route-clean
`0x82281D28` capture with `-Arm64GuestStackArgHandoffAudit true`,
`-Arm64GuestStackArgHandoffAuditFunction 0x82281D28`, and
`-Arm64GuestStackArgHandoffAuditBudget 16`, then parse it with
`tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1`. Do not run a quiet
speed A/B or patch behavior from the skeleton alone.
`docs/research/20260526-061637-guest-stack-arg-handoff-capture.md` closes that
behavior lane: the route was clean, but `stack_arg_store_fields=87`,
`estimated_avoidable_bytes=1360`, `unresolved_direct_targets=52`,
`normal_entry_fallback=67`, `stackpoint_sensitive=67`,
`dirty_flush_points=268`, and `flush_context_barrier=260`.
`docs/research/20260526-063600-82281d28-load-store-offset-audit.md` then shows
the broader hot traffic is ordinary guest-stack memory addressing, not a narrow
handoff opportunity: `body_weighted_offset_ops=450860314`,
`body_weighted_guest_stack_ops=411865334`, and the top block is
`82281D28:8228233C-82282370` with `stw r11,0x64(r1)` plus
`ld r5-r10,0x170-0x198(r1)`. Next work should be a source-level A64
memory-lowering feasibility audit for `ComputeMemoryAddress`,
`AddGuestMemoryOffset`, and `OPCODE_LOAD_OFFSET` / `OPCODE_STORE_OFFSET`,
especially `r1 + small constant`. Do not patch behavior until the audit proves a
reusable legal lowering that preserves 32-bit guest wrap, membase, byte-swap,
MMIO/exception visibility, and fallback behavior.

`docs/research/20260526-070000-a64-memory-lowering-feasibility.md` proves that
candidate exists but must stay constrained: add no behavior from the audit
alone, do not run a quiet speed A/B, and only consider a default-off
offset-aware helper modeled on x64 `ComputeMemoryAddressOffset`. It must keep
`x0` as the final 32-bit guest address, preserve byte swap, MMIO / exception
visibility, store-watch `x0`, large-page threshold semantics, and fallback
behavior. Host pointer plus immediate addressing is blocked without a no-wrap
proof.

`docs/research/20260526-071500-a64-offset-address-helper-prototype.md` adds the
default-off `arm64_offset_memory_address_fastpath` prototype with Android/Thor
launch plumbing. Validation passed (`NativeCore`, `ApkShell`, parser checks,
and the updated feasibility audit), but there is no Thor route proof yet. The
next useful slice is FullDeploy plus a route-clean capture with
`-Arm64OffsetMemoryAddressFastpath true` and delayed body-time comparators
`82282490,82281D28,82287788`. Treat it as route-safety proof only; do not run a
quiet speed A/B until the enabled route reaches visible opening sky /
dragon-wing with clean fatal markers.

`docs/research/20260526-073440-a64-offset-address-fastpath-route-capture.md`
records that route-clean enabled capture. It reached the visible opening
sky / dragon-wing frame with clean fatal markers and shrank generated code sizes
in the hot cluster, but it is not speed proof.

`docs/research/20260526-075500-a64-offset-address-fastpath-quiet-ab.md`
records the same-APK ON/OFF/ON sandwich on commit `5ffe7d20b` / APK SHA
`3195B62E66ABCE289187F6A0C4A67E621AE2A32931BBD98B790CD32CB0E33AB9`. The
enabled cvar is route-clean and locally positive for code size/body-time:
`82282490 87168 -> 86452`, `82281D28 96908 -> 95724`, and
`82287788 35732 -> 35712`, with both ON captures below OFF body ticks. It is
still not sustained 30 FPS proof because Main Thread remains near a full core.
Keep the cvar globally default-off, use
`-Arm64OffsetMemoryAddressFastpath true` explicitly in future Blue Dragon speed
captures, and do not repeat this unchanged A/B.

`docs/research/20260526-081300-82282490-82287788-codegen-floor-capture.md`
records a route-clean mapped OptHIR / block-body capture with the offset
fastpath enabled for `82282490` and `82287788`. It does not justify another
behavior patch: `82282490:822825E0-822825F0` is scalar context/CR traffic,
`82282490:8228252C-822825C4` is the already-closed stvewx / MUL_ADD_V128 /
fpscr local shape, and `82287788` offset traffic is small. Do not rerun this
unchanged codegen-floor capture. If fastmem/addressing continues, require a
broader no-wrap / normal-memory eligibility rule for hot `LOAD_OFFSET` /
`STORE_OFFSET` route blocks; otherwise close the lane.

`docs/research/20260526-083000-a64-no-wrap-memory-eligibility-audit.md` closes
the current fastmem/addressing lane. The route reports still have material
offset volume (`offset_ops_total=609`, `body_weighted_offset_ops_total=585213030`),
but there is no proof surface for host pointer + immediate addressing:
`runtime_no_wrap_counter_present=0`, `static_range_analysis_present=0`,
`static_no_wrap_provable_rows=0`, and `runtime_no_wrap_proven_rows=0`. Keep
using `-Arm64OffsetMemoryAddressFastpath true` in future Blue Dragon captures,
but do not make the next slice another fastmem/addressing probe unless it adds
an explicit no-wrap range counter or static range analysis.

`docs/research/20260526-084500-a64-guarded-stub-entry-design.md` closes the
current guest-call fast-entry behavior lane. The source contract is still a
single normal `A64Function::machine_code` entry and single global indirection
target; direct calls branch to normal entry with only `x0` as the guest return
address, and unresolved/indirection, stackpoint, debug/exception, and host-call
paths require normal fallback. There is no alternate-entry storage, no generic
`r3-r10/lr` payload ABI, and no behavior codegen path. Runtime blockers from
the target-row capture remain material. Do not patch fast-entry behavior unless
a new slice first adds the missing alternate-entry and dirty-flush payload data
model with generated behavior still unchanged.

`docs/research/20260526-085500-a64-nonclosed-guest-state-cache-audit.md` ranks
the remaining guest-state/cache opportunity after excluding closed `r1`/`r11`
and call-argument overlap. The strict targets are `r31,r30,r29,r28,r27`.
Behavior is still blocked because same-block promotion already exists and the
current emit-time cache resets per block.
`docs/research/20260526-090500-a64-nonclosed-gpr-cache-audit-skeleton.md`
adds that default-off post-promotion audit with Android/Thor plumbing. It is
counter-only and changes no generated behavior.
`docs/research/20260526-092500-a64-nonclosed-gpr-cache-capture.md` captured it
route-clean for `0x82281D28` and closes current behavior work:
`candidate_loads=181`, `candidate_stores=75`, but
`clean_hits_possible=0`, `dirty_hits_possible=0`, and high boundary/spill
pressure. Do not rerun unchanged nonclosed-cache captures for `0x82282490` or
`0x82287788`, and do not patch guest-state cache behavior without a broader
CFG/dataflow model.

`docs/research/20260526-093500-a64-fast-entry-data-model-rerank.md` runs the
required harness rerank and reopens fast-entry only as behavior-neutral source
data modeling. `A64Function` now has separate fast-entry storage and an
explicit `r3-r10/lr` payload plus dirty-flush contract; current audits report
the data model present and generated fast-entry behavior absent.
`docs/research/20260526-094500-a64-fast-entry-dirty-flush-protocol.md`
confirms the metadata covers `r3-r10/lr` and all required dirty-flush
boundaries, but payload population, dirty flush codegen, stub codegen, callsite
guards, and stackpoint resume are still absent. Next fast-entry work, if any,
must be a default-off stub skeleton or no-op codegen protocol helper with
normal `machine_code()` and global indirection unchanged. Do not patch direct
callsites from this data model alone.

`docs/research/20260526-101000-a64-fast-entry-stub-skeleton.md` adds that
source skeleton and launch plumbing while preserving generated behavior. The
protocol audit now reports the skeleton and default-off cvar present, but
payload population, dirty flush codegen, stub codegen, callsite guards, normal
entry, direct calls, global indirection, and stackpoint resume are unchanged.
Do not run a speed A/B or behavior patch from this source-only state. Continue
only with direct-call guard / payload-population / dirty-flush / late-bound
fallback / stackpoint-resume design work, or switch lanes.

`docs/research/20260526-102500-a64-fast-entry-guard-protocol.md` adds the
source-only guard model and audit. It proves blocker classes and guard inputs
exist for direct-call guards, payload population, dirty flush coverage,
late-bound fallback, stackpoint resume, and debug/exception visibility, but
codegen still does not use the guard and behavior is unchanged. Next fast-entry
work is payload-population / dirty-flush source design only, or switch lanes.
`docs/research/20260526-104000-a64-fast-entry-payload-flush-plan.md` adds that
source-only planner and audit. It proves the payload/flush blocker model exists
and behavior is still unchanged: no callsite guard, payload population, dirty
flush codegen, fast-entry stub codegen, normal-entry replacement, or global
indirection rewrite. Next fast-entry work must be no-op codegen protocol
helpers with generated behavior unchanged, or switch lanes.
`docs/research/20260526-110000-a64-fast-entry-codegen-protocol.md` adds that
no-op codegen protocol helper and audit. It proves the guard plus payload/flush
planner can be composed as a single source gate, but codegen still does not use
the gate and behavior is unchanged. Do not run speed A/B or patch callsites
from this; switch lanes or keep the next fast-entry slice behavior-neutral
inside emitter planning only.
`docs/research/20260526-111500-a64-fast-entry-emitter-planning.md` adds that
emitter planning audit. It locates the direct-call, late-bound fallback,
stackpoint, debug, exception, and source-map attachment points, and confirms
normal entry, global indirection, payload population, dirty flush codegen, stub
codegen, and callsite behavior are still unchanged/absent. Close the current
fast-entry source-only chain for now; next useful work is a non-fast-entry
structural lane or a compile-time route-ranking audit for a different
body-dominant A64 backend class.
`docs/research/20260526-112900-a64-route-structural-ranking.md` adds the
compile-time route-ranking audit. It closes the ranking pass with
`decision=rank_context_cr_scalar_state_source_audit_next`: context-state /
scalar CR traffic is now the source-audit lane, while VMX128, fastmem host
pointer/immediate, nonclosed GPR cache, and fast-entry behavior remain closed
without fresh proof. Next work is source-only A64 context / CR compare /
branch lowering audit for the mapped body-dominant blocks, not a Thor capture
or speed A/B.
`docs/research/20260526-114200-a64-context-cr-branch-lowering-audit.md`
closes the CR branch behavior part of that lane. The CR blocks need the known
default-off branch-across-barrier or CR-store-elide paths that previously
crashed Blue Dragon; the `82281D28` block is scalar GPR/context traffic, not a
CR branch shape. Next source-only work is scalar context load/store lowering
without CR-store elision or barrier-branch fusion.

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
