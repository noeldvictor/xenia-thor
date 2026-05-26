# Agent Instructions

## Mission

Port this Xenia fork to native Android ARM64 for the AYN Thor Max while keeping performance work visible from the first day.

This repository is a local research experiment, not official Xenia. Do not send
fork-specific crashes, compatibility claims, RenderDoc traces, or half-built
ARM64 backend questions to upstream Xenia, the Xenia Discord, or unrelated
open-source maintainers unless the problem is independently reproduced on an
official upstream build.

## Communication Style

- Be concise and direct. Prefer short, concrete status, findings, and next
  actions over broad narration.
- Avoid AI-slop language: no filler praise, vague hype, fake certainty,
  inflated metaphors, or generic "game changer" phrasing.
- Write like an engineer leaving useful notes for the next run: what changed,
  what proved it, what is still unknown, and what to try next.
- Keep speculation clearly labeled, and tie technical claims to logs, captures,
  source references, or dated research notes.

## Branch And Commit Rules

- Work on `master` only unless the user explicitly changes this policy.
- Do not create `codex/` feature branches for this repo while this rule is in effect.
- Commit and push often after validated Android UI, native ARM64/Vulkan core,
  debug automation, and device capture slices.
- Keep each commit tied to a dated worklog entry. Research-driven changes must
  also have a dated research note.
- New Codex skills for this project must be repo-local under `.agents/skills/`.
  Do not create or update global skills for xenia-thor unless the user explicitly
  reverses this rule.

Primary target:

- Device: AYN Thor Max.
- OS: Android native, not Termux or a Windows compatibility layer.
- ABI: `arm64-v8a` first. Keep `x86_64` only when it helps desktop Android emulator or regression comparison.
- SoC: Snapdragon 8 Gen 2 / Adreno 740.
- Graphics path: Vulkan first.
- CPU path: PowerPC guest code must eventually run through an AArch64 host backend, not the existing x64 backend.
- User milestone: Vulkan game running on Thor Max, with Blue Dragon as the likely first legally owned test title.
- First known legal Blue Dragon target on the Thor SD card:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`.
- Priority is ARM64 on AYN Thor Max. Windows/x64 is useful as a control or
  semantics reference, but do not default to Windows-first workflows unless the
  exact question needs it.
- For xenia-thor work, prefer Thor device proof whenever a change affects
  runtime behavior. Unrelated Windows-only PS3/Eternal Sonata tests are not an
  active xenia-thor slice by themselves and should not block Thor build, deploy,
  ADB, or capture work unless they are actually saturating the host or touching
  the same repo/device/git operation. Keep PS3 Eternal Sonata work Windows-only
  unless the user explicitly starts a separate Thor PS3 sprint.

## Thor Max Translation Model

Use this mental model when planning work: this fork is not trying to be a
cycle-accurate Xbox 360 hardware clone on Android. The practical path is
translation plus HLE:

1. Xbox 360 PowerPC guest code -> AArch64 JIT/DBT code cache on Snapdragon.
2. Xenos GPU packets, shaders, resolves, and EDRAM behavior -> Vulkan/SPIR-V
   work on Adreno 740.
3. Xbox kernel, XAM, files, input, audio, timers, threads, events, and devices
   -> high-level emulation with correctness probes where games depend on exact
   behavior.

Thor Max is a good research target because it has Snapdragon 8 Gen 2-class
CPU cores, Adreno 740, Vulkan 1.3 support, 16 GB RAM in the Max models, active
cooling, Android developer access, microSD content storage, and real handheld
controls. Treat the hardware headroom as real but not automatically fungible:
raw CPU/GPU/RAM ratios can be far above Xbox 360-era hardware, but the effective
speed depends on A64 codegen quality, endian-aware memory lowering, VMX128/FP
lowering, guest/host transition cost, kernel timing, Xenos-to-Vulkan translation,
shader compilation, EDRAM/resolve emulation, synchronization, and Adreno driver
behavior.

Use "about 20x more powerful" as an intuition for why this is a plausible
translation-based research target, not as a performance promise. Validate every
claim with Thor speed captures, screenshots, APK hashes, cvars, and worklog
notes. Prefer translation/HLE hot-path fixes over exact-cycle hardware modeling
unless a concrete game-correctness bug proves that the lower-level detail is
required.

## Hard Documentation Rules

- Every research note must be a Markdown file in `docs/research/` with this filename shape:
  `YYYYMMDD-HHMMSS-topic.md`.
- Every work session must update a Markdown worklog in `docs/worklogs/` with this filename shape:
  `YYYYMMDD.md`.
- Worklogs must include start/end timestamps, branch name, commands run, files changed, findings, blockers, and next actions.
- Do not leave important research only in chat. Put sources, dates, confidence, and decisions in a dated research file.
- When a source may have changed recently, re-check it and include the access date in the research note.
- Clearly mark this fork as an extremely unstable research-only experiment in user-facing docs.
- Do not imply official Xenia support or compatibility.

## Continual Harness Adaptation

Use the repo-local Continual Harness refiner pattern when the autonomous loop is
starting to repeat itself or when a capture changes the next target:

- Skill: `.agents/skills/xenia-continual-harness-refiner/SKILL.md`.
- Window builder:
  `tools/thor/thor_continual_harness_review.ps1 -Mode Window`.
- Scratch output:
  `scratch/thor-debug/continual-harness-window.md` (do not commit it).
- Research note:
  `docs/research/20260523-125940-continual-harness-adaptation.md`.

The portable idea from Continual Harness is recent trajectory window -> refiner
pass -> conservative CRUD over prompt, local skills, memory notes, and tooling.
For this repo, the trajectory window is worklogs, research notes, goal-loop
state, git state, and capture metadata. The CRUD targets are `AGENTS.md`,
repo-local skills, `tools/thor/thor_codex_goal_loop.ps1`, dated research
memory, and deterministic analysis tools. Do not import Pokemon-specific code or
let a refiner pass change emulator behavior without the normal experiment gate.

## Current Blue Dragon Plan Reset

The sprint was starting to loop because active prompts and skills accumulated
chronology instead of decision rules. Keep this file and repo-local skills
short. Put detailed evidence in dated `docs/research/*` and `docs/worklogs/*`.

Latest speed status:

- No sustained 30 FPS proof exists on AYN Thor.
- Current captures remain CPU/JIT-bound, not GPU-bound. Recent examples show
  Main Thread near a full core while GPU Commands stays around `7.6%-11.5%`.
- The recurring generated-code wall is still the `82282490`, `82281D28`,
  `82287788` cluster.

Closed immediate lanes:

- Do not materialize edge payload state for
  `82282490:82282598 -> 82287788`. The lifetime audit
  `docs/research/20260525-195600-edge-payload-lifetime-audit.md` found
  `segments_survived_no_kill=0`, `f1_reads_before_kill=0`, and every observed
  segment first-killed by `CONTEXT_BARRIER`.
- Do not run another quiet speed A/B from the f1/edge-payload, stvewx,
  `MUL_ADD_V128`, fpscr, pre/post-promotion `r1`/`r11`, or local-only
  `822824F0` lanes unless fresh evidence changes the premise.
- Do not make the next default slice a narrow "first CONTEXT_BARRIER" audit
  unless it produces a general backend rule or closes a broad class of
  barriers. A one-off barrier proof is not enough.
- Do not pivot the Blue Dragon speed sprint to broad Vulkan work until captures
  show GPU Commands, present waits, queue submit stalls, pipeline compilation,
  barriers, or texture/upload work overtaking CPU/JIT.
- Do not use ordinal fallback to join `82281D28` runtime block profiles to
  OptHIR for behavior decisions. The current profile guests are source-offset
  addresses missing from printed HIR labels/comments.

Better next lanes:

- A64 backend maturity: register allocation, guest-state cache design, helper
  ABI, block linking, fastmem/addressing, and direct-call cost. As of
  `docs/research/20260526-001500-a64-register-allocation-audit-capture.md`,
  the default-off `arm64_register_allocation_audit` route capture found no
  material allocator spill pressure in `0x82282490`:
  `blocks_with_spills=0`, `local_slots_added=0`, and INT/FLOAT/VEC
  `spill_requests=0`. Do not patch allocator spill behavior from this route;
  `docs/research/20260526-004800-a64-helper-block-link-audit.md` then found
  `dispatch_delta_total=4216370` in the final interval, with
  `resolve_misses_total=0` and only `resolves_delta=5`. Do not make resolver
  behavior the next warmed-route lane. The follow-up
  `docs/research/20260526-011000-a64-call-edge-recursion-capture.md` found
  `82282490` and `82281D28` dominated by self-recursive child body time
  (`69.62%` and `72.22%`), not mostly caller-side dispatch overhead. Do not run
  another unchanged call-edge capture or generic recursive-call/stackpoint
  probe. `docs/research/20260526-012000-vmx128-route-stabilized-counters.md`
  then closed broad VMX128 `PERMUTE` / `LOAD_VECTOR_SHL` /
  `LOAD_VECTOR_SHR` behavior for now: `PACK` stayed absent, `UNPACK` stayed
  zero weighted, active `82282490` / `82281D28` vector volume was concentrated
  in already-closed stvewx/vmaddfp shapes, and `82287788` vector work did not
  dominate its function body time. The follow-up
  `docs/research/20260526-014000-82281d28-hir-block-profile-join.md`
  recovered a one-function `82281D28` OptHIR dump but found the current
  runtime block-profile to HIR join unsafe: `8228233C` is a runtime
  `SOURCE_OFFSET` block address, not a printed HIR label/comment, and ordinal
  fallback maps it to a different HIR label. Do not patch behavior from
  weighted `82281D28` HIR/block-profile joins until a deterministic mapper or
  metadata dump proves the join safe.
  `docs/research/20260526-015900-a64-block-profile-metadata-mapper.md` added
  that metadata surface and disabled ordinal fallback by default in the main
  HIR profile reports. The follow-up capture in
  `docs/research/20260526-022000-82281d28-metadata-capture-blocker.md` proved
  metadata rows are present, but the active hot span
  `82281D28:8228233C-82282370` is still absent from the printed OptHIR stream:
  `active_metadata_unmappable_rows=88` and `join_status=unsafe`. Do not patch
  `82281D28` behavior from weighted HIR/profile joins until per-block HIR text
  and source spans make that active block mappable.
  `docs/research/20260526-023500-hir-block-profile-stamps.md` adds
  behavior-neutral `HIRBuilder::Dump()` block stamps and join-audit parsing for
  `hir_block_*` fields. The follow-up
  `docs/research/20260526-030200-82281d28-block-map-capture.md` FullDeployed
  that patch and proved the join safe after increasing Thor logcat buffers to
  64 MiB: `metadata_rows=88`, `metadata_mappable_rows=88`,
  `hir_block_metadata_rows=88`, `hir_block_mappable_rows=88`,
  `active_metadata_unmappable_rows=0`, and
  `join_status=metadata_required`. The mapped hot block is
  `82281D28:block20 guest=8228233C`, source span `8228233C-82282370`,
  with `context_loads=5`, `context_stores=13`, `memory_loads=6`,
  `calls=2`, `context_barriers=2`, and call targets `0x826BF770` plus
  recursive `0x82281D28`. This closes the mapper lane for now; do not rerun an
  unchanged HIR mapper capture or use ordinal fallback for behavior decisions.
  Next behavior-relevant work is an offline/source audit of that mapped
  call/setup block to decide whether a reusable helper ABI, direct-call, or
  stack-argument rule exists.
  `docs/research/20260526-032000-82281d28-call-setup-audit.md` ran that audit:
  the `826BF770` call has four pre-call stores and all are `callee_live_in`;
  the recursive `82281D28` call has nine pre-call stores and all are
  `callee_live_in`; and strict local promotion for `r3-r10/lr` found zero
  safe wins. Close local `82281D28:8228233C-82282370` store elision and
  one-off `826BF770` caller inlining for now. The useful structural follow-up
  is a generic A64 guest-call argument handoff audit for direct guest calls
  that currently store `r3-r10/lr` to `PPCContext` before the callee reloads
  them.
  `docs/research/20260526-033000-guest-call-arg-handoff-audit.md` adds that
  reusable audit surface as
  `tools/thor/thor_hir_guest_call_arg_handoff_audit.ps1`. First old-log pass
  on `82281D28` found `direct_ppc_bl_calls=81`,
  `argument_store_fields=439`, `callee_first_load_stores=62`, and
  `callee_hir_missing_stores=364`. The two known hot call boundaries are
  ABI-live, and most remaining candidates lack callee HIR coverage. Do not
  design a guest-call fast-entry behavior patch from this single-function
  sample. Next useful work is broader route or file-backed HIR coverage for
  missing direct-call-heavy callees, followed by this audit to prove
  body-weighted callee-first-load volume and normal-entry fallback constraints.
  `docs/research/20260526-034500-guest-call-hir-coverage-capture.md` ran that
  coverage capture on Thor and fixed the audit so extra logs are callee-only.
  Corrected coverage for `82281D28` is now `direct_ppc_bl_calls=81`,
  `argument_store_fields=439`, `callee_first_load_stores=247`,
  `callee_hir_missing_stores=126`, and
  `normal_entry_fallback_required=81`. The body-weighted top call boundaries
  are ABI-live, so this is not local store elision.
  `docs/research/20260526-040500-a64-guest-call-fast-entry-feasibility.md`
  adds `tools/thor/thor_a64_guest_call_fast_entry_feasibility.ps1` and proves
  the source contract: fast-entry is feasible only as a separate guarded entry
  path or stub. Do not replace `A64Function::machine_code()` or the global
  indirection slot.
  `docs/research/20260526-041500-a64-guest-call-fast-entry-audit-skeleton.md`
  adds the default-off counter-only `arm64_guest_call_fast_entry_audit`
  skeleton plus Android/Thor launch plumbing. It changes no generated
  behavior: no alternate codegen, normal entry unchanged, global indirection
  unchanged, and `payload_materializations_allowed=0`.
  `docs/research/20260526-043900-a64-guest-call-fast-entry-audit-capture.md`
  FullDeployed and route-tested it for `0x82281D28`: `direct_calls=84`,
  `eligible_regular=67`, `arg_store_fields=421`, and
  `already_compiled_targets=32`, but `callee_first_use_known=0`,
  `callee_first_use_missing=421`, `normal_entry_fallback=67`, and
  `flush_context_barrier=260`. Do not patch fast-entry behavior or rerun the
  same capture unchanged.
  `docs/research/20260526-045000-guest-call-callee-aware-audit.md` makes the
  offline HIR audit callee-aware and joins the compile-audit row. It reports
  real known live-in volume (`callee_first_load_stores=247`,
  `body_weighted_live_in_fields=690421033`) versus lower missing weighted volume
  (`callee_hir_missing_stores=126`, `body_weighted_missing_fields=30011199`),
  with top known targets `0x82281D28` and `0x826BF770`. Behavior still remains
  blocked because the runtime compile audit has only summary knowledge
  (`callee_first_use_known=0`) and high dirty flush / context-barrier pressure
  (`dirty_flush_points=268`, `flush_context_barrier=260`). Do not patch
  fast-entry behavior or run a quiet speed A/B. If this lane continues, the next
  slice must add default-off runtime per-target rows for direct guest calls and
  close the lane if those rows do not show broad known first-use traffic with
  manageable flush pressure.
  `docs/research/20260526-051000-a64-guest-call-fast-entry-target-rows.md`
  adds those default-off runtime target rows and parser support. It is still
  counter-only and behavior-neutral: no alternate codegen, normal entry
  unchanged, global indirection unchanged, and no payload materialization. Next
  useful work is FullDeploy plus a route-clean `0x82281D28` target-row capture,
  then join those rows with the offline callee-aware HIR audit. Close
  guest-call fast-entry behavior if target rows do not show broad known
  first-use traffic with manageable flush pressure.
  `docs/research/20260526-053600-a64-guest-call-fast-entry-target-row-capture.md`
  FullDeployed and captured those rows. The joined HIR result confirms broad
  known live-in volume (`callee_first_load_stores=247`,
  `body_weighted_live_in_fields=690421033`), but runtime behavior remains
  blocked: `unresolved_direct_targets=52`, `normal_entry_fallback=67`,
  `stackpoint_sensitive=67`, `dirty_flush_points=268`,
  `flush_context_barrier=260`, and `parent_pre_call_flush_points=195`.
  Do not patch guest-call fast-entry behavior, do not run a quiet speed A/B,
  and do not rerun this target-row capture unchanged. Reopen fast-entry only
  with a source-level guarded-stub / late-bound-entry design that explicitly
  preserves normal entry, global indirection, stackpoint/debug/exception
  visibility, and dirty flushes; otherwise move to a hot-block A64
  codegen-floor/disassembly audit.
  `docs/research/20260526-054200-82281d28-hot-block-codegen-floor.md` ran that
  mapped hot-block audit for `82281D28:8228233C-82282370`. It confirms this
  block is scalar call / guest-stack argument setup, not VMX/vector/GPU work:
  `store_context=13`, `load_offset.1=6`, `load_context=5`, `calls=2`, and
  `context_barriers=2`. Literal A64 disassembly was unavailable because ARM64
  Capstone is not present in the capture. Do not patch local store elision,
  fast-entry behavior, VMX128, or GPU/Vulkan from this evidence. Next useful
  work is a default-off counter-only guest-stack argument handoff audit for
  direct guest calls: count `LOAD_OFFSET` from `r1 + constant` feeding
  `STORE_CONTEXT r3-r10/lr` before direct calls, with target/callsite rows,
  body-time weighting, resolved/compiled state, normal-entry fallback pressure,
  context-barrier / stackpoint / debug / exception blockers, helper/extern/tail
  blockers, and estimated avoidable guest stack load/context store traffic.
  `docs/research/20260526-055500-guest-stack-arg-handoff-audit-skeleton.md`
  adds that default-off counter-only audit plus Android/Thor launch plumbing
  and parser support. It is not speed proof and changes no generated behavior:
  `payload_materializations_allowed=0`, `behavior_changed=0`,
  `alternate_codegen=0`, `normal_entry=unchanged`, and
  `global_indirection=unchanged`.
  `docs/research/20260526-061637-guest-stack-arg-handoff-capture.md`
  FullDeployed and captured that audit on Thor. The route was clean and reached
  visible opening sky / dragon-wing, but the stack-specific subset is too
  narrow and blocked for behavior: `stack_arg_store_fields=87`,
  `estimated_avoidable_bytes=1360`, `unresolved_direct_targets=52`,
  `normal_entry_fallback=67`, `stackpoint_sensitive=67`,
  `dirty_flush_points=268`, and `flush_context_barrier=260`. Close the current
  guest-stack argument handoff behavior lane. Reopen only inside a broader
  guarded-stub / late-bound-entry design that handles normal entry, global
  indirection, stackpoint/debug/exception visibility, dirty flushes, and
  unresolved targets. Next useful structural work is fastmem/addressing or A64
  load/store codegen-floor counters for hot `LOAD_OFFSET` / `STORE_OFFSET`
  forms in `82281D28`.
  `docs/research/20260526-063600-82281d28-load-store-offset-audit.md` adds the
  offline file-backed HIR/profile join via
  `tools/thor/thor_hir_load_store_offset_audit.ps1`. The latest route-clean log
  shows `offset_ops=365`, `load_offset_ops=253`, `store_offset_ops=112`,
  `body_weighted_offset_ops=450860314`, and the work is dominated by guest-stack
  `r1` forms: `body_weighted_guest_stack_ops=411865334` versus
  `body_weighted_context_gpr_ops=38994980`. The dominant block is still
  `82281D28:8228233C-82282370`, especially `stw r11,0x64(r1)` and
  `ld r5-r10,0x170-0x198(r1)`. Do not patch behavior or run a quiet speed A/B
  from this offline audit alone. Next work is a source-level A64 memory-lowering
  feasibility audit of `ComputeMemoryAddress`, `AddGuestMemoryOffset`, and
  `OPCODE_LOAD_OFFSET` / `OPCODE_STORE_OFFSET`, proving whether `r1 + small
  constant` has a reusable legal lowering while preserving 32-bit guest wrap,
  membase, byte-swap, MMIO/exception visibility, and fallback behavior. If not,
  close this fastmem/addressing lane for `82281D28`.
  `docs/research/20260526-070000-a64-memory-lowering-feasibility.md` adds that
  source audit via `tools/thor/thor_a64_memory_lowering_feasibility.ps1`. It
  keeps behavior unchanged and finds the fastmem/addressing lane is still
  viable, but only as a constrained backend rule: model an A64 offset-aware
  helper on x64 `ComputeMemoryAddressOffset`, first for normal non-MMIO
  constant-offset paths, keeping `x0` as the final 32-bit guest address before
  membase. Do not use host pointer plus immediate addressing without a no-wrap
  proof. Preserve byte swap, MMIO / exception visibility, store-watch `x0`, and
  the `allocation_granularity() > 0x1000` threshold semantics. Next useful slice
  is a default-off helper prototype or source-tested codegen audit, not a quiet
  speed A/B.
- VMX128-to-NEON lowering that improves broad opcode families, especially
  permute/load-shift/splat/compare/pack/unpack and exact vector memory shapes.
  Current route counters do not justify a broad VMX128 behavior patch; reopen
  this only with fresh body-dominant route volume outside the closed
  stvewx/vmaddfp local shapes.
- Mixed/static hot-function translation research only where guest-visible state,
  entry fallback, invalidation, and exceptions are explicit.
- Measured GPU offload only for Xenos-like bulk graphics work that can stay on
  Adreno: resolves, format conversion, deswizzle/tile transforms, vertex fetch
  unpack, clears, copies, and postprocess.
- Android UX work such as controller mapping and settings can proceed as a
  separate product lane, but it must not be confused with FPS proof.

Harness rules from Continual Harness:

- Use recent trajectory windows, not memory by accretion. Run
  `tools/thor/thor_continual_harness_review.ps1 -Mode Window` after two
  repeated or inconclusive slices, after any user "circles/slow/wtf" complaint,
  or before re-arming a stale loop.
- Make CRUD edits across separate stores: active prompt (`AGENTS.md` and
  `tools/thor/thor_codex_goal_loop.ps1`), skills (`.agents/skills/*`), memory
  (`docs/research/*`), deterministic tools (`tools/thor/*`), and worklogs.
- Delete or merge a repo-local skill when it has no unique trigger, mostly
  duplicates another skill, mostly stores dated chronology, or cannot name a
  durable output. This pass keeps all skill directories because each still has
  a distinct trigger, but it prunes chronology from the bloated ones.
- Re-arm the goal loop only with a concise current decision prompt.

## Thor Hardware Acceleration Plan

Use `docs/research/20260525-171305-thor-hardware-acceleration-menu.md` as the
current hardware-acceleration menu for AYN Thor. The tested Thor exposes
NEON/Advanced SIMD, `asimddp`, `i8mm`, `bf16`, `asimdfhm`, `fcma`, `crc32`, and
LSE-style atomics, but no `sve` / `sve2`. Optimize for the hardware actually
present.

Current priority remains CPU/JIT until captures change: the latest route-clean
Blue Dragon sample ended with Main Thread `92.3%`, GPU Commands `11.5%`, and
Draw Thread `3.8%`.
Do not move branchy PPC/JIT state traffic, fpscr writebacks, call-edge carriers,
or helper-heavy VMX semantics to Vulkan compute.

Hardware lanes to pursue:

- A64/JIT: structural register allocation, guest-state cache design, helper
  ABI, block linking, direct/indirect call dispatch, and fastmem/addressing.
  `arm64_register_allocation_audit` can now capture per-function INT/FLOAT/VEC
  allocation pressure and spills. Its first `0x82282490` route capture found
  no spill pressure, so do not spend the next slice on allocator spill behavior
  unless a broader function or route counter reopens it.
  Keep the exact `82282490:82282598 -> 82287788` edge-payload storage lane
  closed unless a future general entry/fallback model reopens it.
- NEON/VMX128: exact 128-bit boolean, splat, permute, shift, compare, min/max,
  pack/unpack, vector-load-shift, and layout-transform lowering. Use dot/I8MM
  only for proven integer sum-of-products shapes, not VMX FP32.
- CRC/LSE/prefetch: apply only after source/counter evidence finds hot checksum,
  lock/reservation, or predictable memory-stream pressure.
- GPU/Adreno: push only Xenos-like bulk work to GPU: EDRAM resolves, format
  conversions, texture deswizzle/endian/tile transforms, vertex fetch unpack,
  clears/copies, and GPU-resident postprocess. Add counters for bytes, queue
  waits, barriers, render-pass load/store choices, uploads, readbacks, pipeline
  creation, and cache misses before behavior patches.

When the user asks to "get GPU work to GPU", translate that into a measured
graphics-work audit first. If it finds a bulk CPU graphics transform, make one
default-off Vulkan compute or render-pass experiment with no immediate CPU
readback and route-clean Thor proof.

## Vulkan Speed Diagnostics

Do not treat "push more GPU" as a renderer rewrite request while Blue Dragon
Thor captures still show the guest CPU/JIT main thread as the wall. Mature
emulator Vulkan ports first add counters for pipeline creation, shader
translation, queue submission, present/acquire waits, barriers, render-pass
churn, readbacks, uploads/copies, descriptor churn, resolves, and completion
waits.

Use `docs/research/20260524-165737-mature-vulkan-port-speed-design.md` for the
external design ladder and
`docs/research/20260524-171203-vulkan-counter-surface-audit.md` for the
repo-local source map. Run
`tools/thor/thor_vulkan_counter_surface_audit.ps1` before a GPU speed patch.
The default-off Vulkan perf counter skeleton is documented in
`docs/research/20260524-173639-vulkan-perf-counter-skeleton.md` and is enabled
with `-VulkanTracePerfCounters true` plus
`-VulkanTracePerfCountersLogInterval <N>`. It currently reports IssueSwap,
pipeline create/cache-hit, queue submit, present/acquire/submit, barrier, and
render-pass counters. The route-clean sandwich in
`docs/research/20260524-175206-vulkan-counter-route-sandwich.md` reached the
opening sky/dragon-wing route with counters on and off, clean fatal-marker
searches, and no queue/present/pipeline failures; it still showed Main Thread
around one full core and GPU Commands well below one full core. Do not pivot to
RenderDoc-only FPS claims, Swappy/frame pacing, or broad Adreno rewrite work
until counters expose a real GPU wall.

## Mobile TBR / GPU Memory Locality Lane

Use Diya Joseph's 2024 thesis,
`docs/research/20260525-165028-mobile-tbr-gpu-memory-thesis.md`, as a
GPU-memory-locality checklist for Android Vulkan work. The thesis is about
tile-based rendering, texture/cache locality, inter-frame reuse, and GPU memory
latency hiding; it is not evidence that Blue Dragon's current A64/JIT wall
should move to GPU compute.

- Apply the thesis only after counters show GPU Commands, present waits, queue
  submits, pipeline creation, barriers, resolves, readbacks, uploads, or texture
  traffic overtaking the CPU/JIT wall.
- Translate TCOR/DTexL/Boustrophedonic Frames/WaSP into diagnostics and
  renderer hygiene: render-pass load/store behavior, resolve bytes, texture
  upload/copy bytes, descriptor/pipeline churn, frame-to-frame resource reuse,
  synchronization stalls, and Adreno/AGI bandwidth or cache counters.
- Do not implement hardware-scheduler ideas directly in Xenia. Prefer coherent
  batching, fewer tiny GPU tasks, stable resource reuse, and measured Vulkan
  changes with Thor captures.

## Cemu / Wii U Android Comparison Lane

Use current public Cemu Android work as a serious design reference for
xenia-thor, especially for Android-native performance, controller UX, settings
surfaces, and handheld ergonomics. Do not "steal" code blindly. Treat this as
license-aware comparative engineering:

- Mainline Cemu is MPL-2.0. Xenia is BSD-style. Do not copy Cemu source into
  this repo unless a file-level license plan is explicit and reviewed first.
  Prefer architecture notes, measured behavior, UX patterns, and clean-room
  reimplementation.
- The SSimco Android Cemu fork is an active Android-port reference, but it is
  still experimental. Use it to learn how mature emulator code is packaged,
  configured, launched, profiled, and mapped to Android input, not as proof that
  Xbox 360 work should be easy.
- The SapphireRhodonite Cemu fork is a useful Android handheld UX reference for
  dual-screen/external-display routing and device-specific presentation logic.
  For xenia-thor, translate that lesson into clean surface lifecycle, external
  display handling, game view scaling, and controller-friendly settings.

Technical comparison rules:

- Wii U and Xbox 360 both make the Android target feel plausible: both are
  PowerPC-family consoles with ATI/AMD GPU lineage and unified memory ideas.
  They are not interchangeable. Xbox 360 has Xenon SMT cores, VMX128, Xenos
  EDRAM/resolve behavior, Xbox kernel/HLE contracts, and different memory and
  synchronization pressure. Validate every borrowed idea on Thor.
- Before a new A64/JIT speed experiment, scan Cemu Android and other mature
  emulator backends for the pattern: guest-state register caches, block linking,
  fast dispatch, code-cache invalidation, host ABI choices, SIMD/FP lowering,
  shader/pipeline cache behavior, per-game hacks, and debug counters. Convert
  findings into a dated `docs/research/YYYYMMDD-HHMMSS-*.md` note before code.
- Do not pivot away from the current Blue Dragon CPU/JIT wall just because a
  Wii U Android fork is fast. Use Cemu as pressure to improve our backend
  design discipline, not as a substitute for Thor captures.

Android UX mandate:

- User preference as of 2026-05-25: build the Cemu-style settings/profile path,
  including GPU, audio, controller/input, runtime, and expert/research config
  surfaces. Do not limit the lane to a one-off Thor controller fix.
- xenia-thor must grow a real handheld-facing settings and controller layer,
  not only ADB cvars. Prioritize launcher-visible settings, per-game profiles,
  controller mapping, profile import/export, safe defaults, and an expert page
  for dangerous speed/debug toggles.
- Controller work should first make the AYN Thor built-in controls reliable in
  `EmulatorActivity`, then add external Bluetooth/USB gamepads, remapping,
  dead zones, trigger handling, rumble, and a visible input test screen.
- Settings should distinguish stable user settings from research cvars. Any
  experimental speed toggle exposed in UI needs a plain rollback path and must
  stay default-off until route proof justifies otherwise.
- When starting the controller/settings lane, ask the user which UX shape they
  want first: minimal Thor-only controller fix, Cemu-style settings/profiles,
  or a broader emulator launcher polish pass.

## Binary Translation Research Triage

Treat recent DBT/static-translation papers as pressure to improve design, not
as proof that a full Xbox 360 game can be turned into a native Android APK in
one sprint. Always record source links in dated research notes before changing
JIT, memory-model, or GPU-offload behavior.

- Elevator-style fully-static translation is a long-term offline
  specialization/checking lane. Near term, translate that idea into
  route-backed static guest-function audits, code-size estimates, and
  function/edge variants with normal-entry fallback.
- Partial cross-compilation maps to hot function/edge variants with explicit
  native-call channels and fallbacks. For Blue Dragon, this currently points at
  state-carrier and caller-local edge-variant work such as
  `82282490:82282598 -> 82287788`, not a broad whole-game static conversion.
- Learning-based DBT rule generation is only actionable when paired with
  symbolic equivalence, PPC tests, and route proof. Do not land LLM/ML-derived
  instruction rewrites from pattern matching alone.
- Arancini/AtoMig-style weak-memory work means ARM ordering must stay explicit.
  Do not delete barriers, stack sync, call-visible state writebacks, or external
  visibility just because a local A64 span looks expensive.
- Host-GPU latency research reinforces the current Vulkan rule: offload only
  bulk, stable, cache-friendly kernels after counters show GPU headroom. Do not
  move Blue Dragon CPU/JIT work to Vulkan compute while Main Thread remains the
  wall.

## VMX128 Inline Patching Lane

The user believes VMX128 inline patching plus advanced ARM64 instructions can
be a major speed source. Treat that as a high-priority research lane, but keep
it proof-gated.

- Thor `c3ca0370` reports AYN Thor / `kalama` with `asimd`, `asimddp`, `i8mm`,
  `bf16`, `asimdfhm`, `fcma`, `crc32`, and related ARMv8/Armv9 features, but
  no `sve` / `sve2` in `/proc/cpuinfo`. Do not build a Thor-required SVE/SVE2
  path unless a future device capture proves those features.
- Prefer default-off, function/PC-gated inline VMX128 patches over global
  opcode rewrites. Every candidate needs a static HIR/source audit, dynamic
  hit counters, fallback counters, generated-code size deltas, PPC tests when
  available, and route-clean Thor proof before quiet A/B.
- Good first-class NEON targets: direct 128-bit boolean ops, splats, permutes,
  shifts, compares, min/max, pack/unpack, vector load-shift joins, and local
  state/vector traffic reduction.
- Current VMX128 audit tool:
  `tools/thor/thor_a64_vmx128_neon_family_audit.ps1`. As of
  `docs/research/20260525-233000-vmx128-route-volume-audit.md`, PACK/UNPACK
  unit coverage is complete but PACK is absent and UNPACK had zero weighted
  hot-block volume in the available route profiles. Do not patch PACK/UNPACK
  behavior now. `docs/research/20260526-012000-vmx128-route-stabilized-counters.md`
  then checked current PERMUTE / LOAD_VECTOR_SHL/SHR route volume and closed
  broad behavior work for now: route volume was not body-dominant and mostly
  aligned with closed local shapes. Exact stvewx and three-PC `MUL_ADD_V128`
  remain closed.
- Use ARM dot-product/I8MM only when the VMX opcode is an integer
  sum-of-products shape and signedness, lane order, saturation, endianness, and
  accumulation semantics are proven. Do not apply `SDOT` / `UDOT` to FP32 VMX
  dot products.
- Be suspicious of fused FP shortcuts. `FMLA` can be fast, but VMX FP32 has
  guest-visible NaN, denormal, FPCR/NJM, and result-order behavior. Existing
  Blue Dragon MUL_ADD_V128 and VMX-dot lanes stayed default-off after mixed or
  negative speed proof, so reopen them only with broader fresh evidence.
- BF16/FHM/FCMA are not drop-in replacements for Xbox 360 VMX FP32. Use only
  after an opcode-specific audit proves exact semantics.
- Use the Box64 lesson as architecture, not copied code: inline native patches
  need safe invalidation, normal-entry fallback, feature gates, and per-title
  counters. For Xenia, map this to code-cache invalidation, edge/function
  variants, and normal-entry fallback rather than blind self-modifying-code
  assumptions.

## Repo Facts As Of 2026-05-17

- Origin is SSH: `git@github.com:noeldvictor/xenia-thor.git`.
- The Android Studio project is under `android/android_studio_project`.
- Android Gradle config currently uses:
  - `compileSdkVersion 33`
  - `targetSdkVersion 33`
  - `minSdkVersion 24`
  - `ndkVersion '25.0.8775105'`
  - `abiFilters 'arm64-v8a', 'x86_64'`
- Premake defines Android platforms `Android-ARM64` and `Android-x86_64`.
- The production desktop CPU backend remains `src/xenia/cpu/backend/x64`.
- The Android ARM64 CPU path is now a hard aX360e/Edge-style A64 backend
  import under `src/xenia/cpu/backend/a64`, exposed through the
  `src/xenia/cpu/backend/arm64` compatibility wrapper.
- The previous helper-backed ARM64 mini-JIT scaffold was removed on
  2026-05-18. Do not spend new work on mini-JIT helper patches unless the user
  explicitly asks to resurrect that path for comparison.
- `src/xenia/emulator.cc` can select the ARM64 scaffold on `XE_ARCH_ARM64` or via `cpu=arm64` launch arguments.
- `src/xenia/cpu/cpu_flags.cc` advertises `any`, `x64`, and `arm64`.
- `src/xenia/app/premake5.lua` includes `xenia_main.cc` for Android and skips the HID demo in the Android single-library bundle.
- Android currently has Java/native shell pieces for windowed apps, a launcher, a game activity, a window demo, and the Vulkan trace viewer.
- On 2026-05-17, `app-github-debug.apk` installed on ADB device `c3ca0370` and `WindowDemoActivity` rendered the Vulkan ImGui demo on Adreno 740.
- On 2026-05-17, the launcher showed `LAUNCH GAME`, `GPU TRACE VIEWER`, and `WINDOW DEMO`; launching the emulator path reached the intended ARM64 JIT-not-implemented blocker rather than a Java/native crash.
- On 2026-05-17, Blue Dragon Disc 1 launched into `EmulatorActivity` and executed guest XThreads through the ARM64 HIR interpreter scaffold. It is not playable or visibly rendering game frames; the current wall is interpreter speed and missing AArch64 JIT/emitter work.
- Use `tools/thor/thor_build.ps1` to split build lanes:
  - `FullApk`: native core plus APK packaging.
  - `NativeCore`: C++/Vulkan/CPU backend library only.
  - `ApkShell`: Java/XML/resources/manifest package only, using the previous native library.
  - Nonzero Gradle or ADB exits must stop the script; do not trust an install
    after a failed native build.
- Use `tools/thor/thor_xenia_debug.ps1` for repeatable ADB install, launch, and capture.
- `tools/thor/thor_xenia_debug.ps1` now retries flaky ADB transports for the
  known Thor serial and records reconnect events in capture metadata.
- `tools/thor/thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture` launches
  Blue Dragon, keeps live logcat open during the run, writes filtered logcat,
  metadata, and a PNG screenshot, and is preferred over dump-afterward logcat
  for early GPU/presenter probes.
- `tools/thor/thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture` is the
  preferred speed lane. It runs Blue Dragon with the known nop HID sequence,
  disables trace-heavy flags, uses error-only Xenia logging, avoids live logcat,
  writes timed perf snapshots, then captures logcat, metadata, and a screenshot.
- Blue Dragon live/title/speed capture modes force-stop Xenia after the final
  screenshot by default to avoid stale frozen emulator processes making the Thor
  feel slow after a run. Pass `-StopAppAfterCapture false` only when the next
  step needs the app left open for manual inspection.
- Blue Dragon speed/title defaults explicitly pass `arm64_context_value_cache=false`
  because Android's saved `xenia.config.toml` can preserve old experiment cvars
  between app launches. Do not trust blank metadata for a cvar if the filtered
  log says the feature is enabled; check the launch args and device config.
- As of 2026-05-18, Blue Dragon's VdSwap frontbuffers `1CA1C000` and
  `1CDB4000` are still all-zero on Thor, but forced-presenting resolve
  candidate `1C340000` as 640x360 format 7 produces a visible dark-blue guest
  surface. This is not title, gameplay, or compatibility; use it only as a
  debug checkpoint for render-source selection.
- Use `tools/arm64/hir_coverage_report.ps1` to summarize latest Thor logcats
  into HIR opcode counts, unimplemented opcodes, legacy mini-JIT fallback
  reasons from older captures, slow interpreter functions, guest crash PCs,
  ARM64 guest-store watch hits, and PPC global-reference scanner hits.
- Use `tools/arm64/arm64_jit_gap_report.ps1` to compare HIR opcodes against
  the legacy ARM64 mini-JIT and interpreter switch coverage when reviewing
  pre-swap captures, then attach recent Thor log watchdog/fallback signals to a
  dated Markdown report.
- Use `tools/arm64/arm64_conversion_audit.ps1` before broad ARM64 backend work
  or after suspicious Thor runs. It summarizes x64-vs-ARM64 backend services,
  HIR switch coverage, legacy helper-heavy mini-JIT surface, reject/error
  signals, and recent Thor evidence into a dated Markdown report.
- Use `tools/thor/ghidra_headless_import.ps1` for repeatable Ghidra headless imports. Local Ghidra was found at:
  `C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC`.
  The script auto-detects that path, but `GHIDRA_HOME`, `-GhidraHome`, and
  `-AnalyzeHeadless` still override it.
- Use `tools/thor/thor_renderdoc.ps1` for Android Vulkan layer setup, RenderDoc status, cleanup, and capture pulling.
- Current translation strategy digest:
  `docs/research/20260519-153016-xbox360-thormax-translation-report.md`.
  It says Blue Dragon performance work should treat CPU/A64 dispatch, helper
  fallbacks, XMA/audio cost, and debug overhead as the first wall; GPU/Adreno
  deep dives should stay narrow until evidence points back to rendering.
- Rejected hotpath note:
  `docs/research/20260519-234533-blue-dragon-copy-fastpath-dead-end.md`.
  It documents why the tempting `8272A3A4:8272A8B4` Blue Dragon byte-copy
  fastpath was removed: partial HIR-block replacement either crashed or
  black-idled the route. Revisit only with a full PPC/HIR region proof and a
  route-clean fastpath-on capture.
- Current `8272A3A4` CR lowering note:
  `docs/research/20260520-184020-a64-cr-shape-relaxed-peephole.md`.
  The CR-shape audit found 60 exact `LT/GT/EQ` triplets and 50 `UGT/EQ` pairs
  in the hot HIR, with the old only-use guard rejecting all triplets. The A64
  backend now materializes compare results into their assigned value registers
  and can still fuse the adjacent CR stores when those values have later users.
  Thor proof `scratch/thor-debug/20260520-183741-*` shrank `8272A3A4` from
  `12544` to `12196` bytes and `8272A8E8` from `5508` to `5356`, with no
  searched fatal markers. It still black-idled later, so treat it as a
  generated-code shrink only; next work is state traffic, barriers, and hot GPR
  context slots.
- Current A64 context-cache/spinlock note:
  `docs/research/20260520-192930-a64-context-cache-and-spinlock-fastpaths.md`.
  The block-local `arm64_context_value_cache` probe found zero `8272A3A4` load
  hits (`255/0`) despite 240 cacheable stores, so it is default-off and should
  not be treated as the PPC state-cache solution. A real state cache must live
  across HIR blocks with explicit helper/exit/barrier flush rules. The raised
  IRQL spinlock fastpaths are default-on and route-clean with Blue Dragon when
  `a64_inline_kf_lower_irql=false`; keep the naive `KfLowerIrql` inline
  default-off because it black-idled the route when it skipped native APC
  delivery.
- Current A64 critical-section leave note:
  `docs/research/20260520-220803-a64-rtl-leave-final-unlock.md`.
  `a64_inline_rtl_leave_final_unlock` is default-on and inlines only the
  uncontended final release shape for `RtlLeaveCriticalSection`
  (`recursion_count == 1`, `lock_count == 0`, owner is current thread). If a
  waiter races in, it restores the critical-section fields and falls back to
  native HLE so the wake path is preserved. Thor proof
  `scratch/thor-debug/20260520-220613-*` reached the Blue Dragon Voice Language
  screen with no searched fatal markers. The speed route still has unrelated
  black-idle nondeterminism; both on and off captures idled in the same cleaned
  APK, so use one-variable A/B before blaming this toggle.
- Current A64 critical-section audit note:
  `docs/research/20260520-223349-a64-critical-section-audit.md`.
  `a64_rtl_leave_fastpath_audit` is default-off and logs recursive/final/native
  `RtlLeaveCriticalSection` path counts through the speed profiler. Audit proof
  `scratch/thor-debug/20260520-222648-*` reached Voice Language and showed
  final unlock dominating by the final interval (`195628` final inline,
  `23189` recursive inline, `3028` native fallback, `14` restore races).
  `a64_rtl_enter_free_first` exists only as a default-off experiment:
  `scratch/thor-debug/20260520-223025-*` black-idled with it on, while the same
  APK reached Voice Language with `-A64RtlEnterFreeFirst false`.
- Current A64 KfLowerIrql APC-guard note:
  `docs/research/20260521-002511-a64-kf-lower-irql-apc-guard.md`.
  `a64_inline_kf_lower_irql_apc_guard` is default-off and diagnostic only.
  It checks the current thread's host APC pending counter before inlining the
  IRQL restore, with optional native polling through
  `a64_kf_lower_irql_apc_guard_native_poll_interval`. Blue Dragon still
  black-idled with no-poll, poll-64, and poll-4 captures, while the same APK
  reached Voice Language with the guard off. Do not enable this by default or
  count it as a speed win until a later native `KfLowerIrql` / `CheckApcs`
  audit proves what host-side scheduling/APC behavior must be preserved.
- Current A64 LSE kernel lock note:
  `docs/research/20260521-160124-a64-lse-kernel-lock-fastpaths.md`.
  `a64_lse_kernel_lock_fastpaths` is default-on and guarded by the existing
  host `kA64EmitLSE` feature check. It replaces `ldaxr`/`stlxr` retry loops in
  hot kernel lock/IRQL fastpaths with LSE atomics (`swpal`, `casal`,
  `ldaddal`) where possible. Thor A/B proof
  `scratch/thor-debug/20260521-155831-*` reached the Blue Dragon Voice
  Language screen with LSE on, while same-APK
  `scratch/thor-debug/20260521-155946-*` black-idled with
  `-A64LseKernelLockFastpaths false`. Keep the cvar's rollback path in all
  Blue Dragon scripts and do not treat this as the final FPS fix; the next wall
  remains `827294CC`, `8272A3A4`, `8272A8E8`, XMA/audio, and GPU command work.
- Current Blue Dragon nop input route note:
  `docs/research/20260521-164045-blue-dragon-nop-keystroke-route-fix.md`.
  The speed lane now depends on nop HID emitting scheduled
  `XamInputGetKeystroke` transitions, not only `GetState` button states. The
  no-auto-input title proof `scratch/thor-debug/20260521-162805-*` reached and
  stayed at `press START`; after the nop keystroke fix,
  `scratch/thor-debug/20260521-163237-*` reached the loading spinner and
  `scratch/thor-debug/20260521-163453-*` reached the opening rendered sky/wing
  scene by 180 seconds with APK SHA
  `FB4877DF6BEA31D86B8354632668A36BDAD134D48738132E26813FD7C5F631B6`.
  Keep `arm64_blue_dragon_stricmp_deferred_cr_fastpath=false`: capture
  `scratch/thor-debug/20260521-161210-*` guest-crashed at PC `826A2498` with it
  enabled. Keep `arm64_blue_dragon_jump_table_inline_in_caller=false`: the
  post-input-fix retest `scratch/thor-debug/20260521-164314-*` black-idled with
  `entry_delta=0` from 16:44:02 onward.
- Current delayed A64 body-time note:
  `docs/research/20260521-170708-blue-dragon-delayed-body-time-profile.md`.
  Body-time instrumentation from boot black-idled
  `scratch/thor-debug/20260521-164744-*`, so use
  `-Arm64SpeedProfileBodyTimeAfterMs 120000` for the current opening-scene
  route. Delayed run `scratch/thor-debug/20260521-165657-*` reached the opening
  sky/wing scene and showed `82282490` as the real body-time leader despite
  lower entry count: final interval `body_ticks_delta=9488930` at `65296`
  entries. Block-profile run `scratch/thor-debug/20260521-170107-*` reached the
  opening "Microsoft Game Studios Presents" scene; next speed work should
  classify `82282490` blocks `822824B8`, `822824F0`, `8228252C`, `822825E0`,
  `822825F4`, and `82282600` before changing codegen.
- Current Blue Dragon VMX dot negative:
  `docs/research/20260521-172826-blue-dragon-vmx-dot-negative.md`.
  HIR/disassembly capture `scratch/thor-debug/20260521-170941-*` shows
  `82282490` is a large VMX-heavy routine with repeated `vmsum4fp128` /
  `dot_product_4`, `vmrghw` / `permute.2`, `lvx`, `stvx`, `byte_swap`, and
  high vector context load/store traffic. The naive
  `arm64_vmx_dot_f32_fastpath` shortcut is default-off: same-APK A/B capture
  `scratch/thor-debug/20260521-171859-*` black-idled with the cvar true, while
  `scratch/thor-debug/20260521-172247-*` reached the opening sky/dragon-wing
  scene with it false. Prioritize semantics-preserving `PERMUTE_I32` /
  `vmrghw`, `stvx` byte-swap/store fusion, and vector state traffic reductions
  before trying another dot-product rewrite.
- Current Blue Dragon `PERMUTE_I32` zip fastpath:
  `docs/research/20260521-174106-blue-dragon-permute-i32-zip-fastpath.md`.
  `arm64_permute_i32_zip_fastpath` is default-on and Android-forwarded. It
  lowers exact PPC merge-word masks `0x05010400` (`vmrghw`) to NEON `zip1 .s4`
  and `0x07030602` (`vmrglw`) to `zip2 .s4`; all other `PERMUTE_I32` controls
  keep the old TBL path. Thor proof `scratch/thor-debug/20260521-173359-*`
  reached the opening sky/dragon-wing scene with APK SHA
  `9E6A13BD7B8CC0B9A67F5042DA34599BE4F9623A3697350A9FD61D9F71B21BC1` and
  shrank `82282490` code size to `87168`. Same-APK rollback run
  `scratch/thor-debug/20260521-173734-*` black-idled before body-time
  activated, so keep the rollback flag but leave the fastpath on for Thor.
- Reverted V128 swapped-store address-spill cleanup:
  `docs/research/20260521-175053-v128-store-address-spill-negative.md`.
  The local attempt to skip the `x17` address spill for non-constant swapped
  `STORE_V128` built and deployed, but
  `scratch/thor-debug/20260521-174627-*` black-idled before delayed body-time
  activated. The code was reverted before commit. Do not reintroduce this tiny
  store cleanup without a route-clean Blue Dragon opening-scene proof and an
  audit showing it actually shrinks `82282490`.
- Range-aware context promotion foundation:
  `docs/research/20260521-180835-context-promotion-range-aware.md`.
  `ContextPromotionPass` now tracks full byte ranges for `LOAD_CONTEXT` reuse
  and block-local `STORE_CONTEXT` DSE instead of using only the start offset.
  Thor proof `scratch/thor-debug/20260521-180212-*` reached the opening
  sky/dragon-wing scene with APK SHA
  `3A9C1EF2FB39F2DA4ACFA1B8C969A06D106439DBBFF1556D3084D1AA7A3CDCF3`, but
  `82282490` stayed at `code_size=87168`; count this as optimizer foundation,
  not an FPS win. Runtime `82282490` context audit capture
  `scratch/thor-debug/20260521-175626-*` black-idled with `entry_delta=0` from
  17:57:14 onward, so prefer low-noise compile-time audits for the next context
  sprint.
- Reverted `PERMUTE_I32` lane-replace probe:
  `docs/research/20260521-182124-permute-i32-lane-replace-negative.md`.
  Offline `82282490` HIR still shows tempting non-zip masks, but the broad
  generic lane-replace helper black-idled twice:
  `scratch/thor-debug/20260521-181513-*` and
  `scratch/thor-debug/20260521-181920-*`. Keep only the proven
  `0x05010400`/`0x07030602` zip lowering default-on. Do not re-add generic
  lane-replace `PERMUTE_I32` without an exact-mask correctness harness or a
  route-clean opening-scene proof.
- Offline HIR hotpath report tool:
  `docs/research/20260521-182504-hir-hotpath-report-tool.md`.
  Use `tools/thor/thor_hir_hotpath_report.ps1` on filtered function-dump logs
  before guessing at another `82282490` patch. Known command:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_hotpath_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Top 20`.
  Current `82282490` OptHIR summary is `store_context=1415`,
  `load_context=763`, `dot_product_4=136`, `permutes=381`, and
  `context_barriers=213`. As of
  `docs/research/20260521-195741-hir-context-offset-annotations.md`, the tool
  annotates context offsets with PPC field names and class totals. `82282490`
  OptHIR class totals are loads `GPR=546`, `VMX=155`, `FPR=38`, and stores
  `GPR=562`, `VMX=373`, `CR=343`, `LR/CTR=68`. The next state-cache sprint
  should start with GPR slots `r[1]`, `r[11]`, `r[10]`, `r[29]..r[31]` and CR6
  stores before another vector micro-peephole.
- HIR state-span report:
  `docs/research/20260521-210004-hir-state-span-report.md`.
  Use `tools/thor/thor_hir_state_span_report.ps1` to rank same-span and
  cross-span context churn. On the existing `82282490` OptHIR dump, the largest
  cross-span repeated-load GPR is `r[1]` (`81`), while cross-span
  load-after-store GPRs are led by `r[11]` (`25`), `r[31]` (`16`), `r[10]`
  (`14`), `r[3]` (`11`), `r[29]` (`10`), and `r[30]` (`9`). Treat this as the
  next state-cache design map, not a runtime speed win. As of
  `docs/research/20260521-211840-gpr-state-cache-candidate-plan.md`, the
  report also prints a candidate GPR cache plan. The top scores are `r[1]`
  (`220`), `r[11]` (`169`), `r[10]` (`98`), `r[31]` (`92`), `r[29]` (`78`),
  `r[30]` (`71`), and `r[28]` (`69`). First patch should keep clean INT64 GPR
  values only, preserve across no-op `context_barrier` behind a guarded cvar,
  and reset on calls, branches, labels, helper-expanded instruction ranges,
  volatile ops, and overlapping writes. Do not elide stores in that first
  patch.
- A64 clean-GPR barrier-preserve negative:
  `docs/research/20260521-212700-a64-gpr-cache-barrier-negative.md`.
  `arm64_context_value_cache_preserve_barrier` is default-off and audit-only.
  Thor capture `scratch/thor-debug/20260521-212305-*` reached the visible
  opening sky/dragon-wing route with no searched fatal markers, but
  `82282490` still reported `loads/hits=546/0` despite
  `barrier_preserves=213` and `stores/cached=562/463`. The loss is host-register
  invalidation (`register_invalidations=768`), not just barrier reset. Next
  state-cache work should move before A64 register allocation or pin one/two
  PPC GPRs with explicit helper/exit/branch flushes.
- HIR GPR promotion audit:
  `docs/research/20260521-213650-hir-gpr-promotion-audit.md`.
  Use `tools/thor/thor_hir_gpr_promotion_audit.ps1` before cross-block GPR
  promotion work. On the current `82282490` OptHIR dump it found
  `118` blocks, `546` whole-GPR loads, `562` whole-GPR stores,
  `29` dominated single-predecessor blocks, and `61` first whole-GPR loads in
  those blocks. Top pre-RA load-promotion candidates are `r[1]` (`score=32`),
  `r[11]` (`13`), and `r[10]` (`11`). Do not create cross-block SSA values
  directly in `ContextPromotionPass`; the PPC translator does not currently run
  `DataFlowAnalysisPass`, so the first runtime patch needs local-slot lowering
  or a guarded data-flow stage before `RegisterAllocationPass`.
- A64 GPR local-slot promotion probe:
  `docs/research/20260522-113012-gpr-local-slot-promotion-probe.md`.
  `arm64_context_promotion_gpr_local_slots` is default-off and Android/Thor
  script-routable with optional function filter
  `arm64_context_promotion_gpr_local_slots_function`.
- A64 GPR local-slot counter note:
  `docs/research/20260522-114745-gpr-local-slot-promotion-counters.md`.
  `arm64_context_promotion_gpr_local_slots_audit` is also default-off and
  Android/Thor script-routable. Capture `scratch/thor-debug/20260522-114838-*`
  reached the visible opening sky/dragon-wing route and had no searched fatal
  markers, but explained the slowdown: for `82282490`, the pass inserted `103`
  HIR local stores and replaced only `3` loads. `r[11]` produced `68` local
  stores and `0` replaced loads; `r[1]` produced `35` local stores and `3`
  replaced loads. Keep the local-slot path as a default-off scaffold only. The
  next state-cache slice should try a lower-overhead pinned-GPR path for `r[1]`
  first, with explicit helper/exit/branch/exception/volatile/alias reset rules.
- A64 pinned `r[1]` cache probe:
  `docs/research/20260522-123855-a64-pinned-r1-cache-probe.md`.
  `arm64_context_pinned_gpr_r1` and
  `arm64_context_pinned_gpr_r1_fallthrough` are default-off and script-routable.
  Final APK control `scratch/thor-debug/20260522-123536-*` reached the loading
  spinner with no searched fatal markers and `82282490 code_size=87168`.
  Pinned no-fallthrough `scratch/thor-debug/20260522-123918-*` was route-clean
  but logged `loads/hits=107/0`, `pin_loads=107`, and grew `82282490` to
  `87596` bytes. Pinned fallthrough `scratch/thor-debug/20260522-123232-*`
  black-stalled before reaching `82282490`. Do not tune emit-time `x29`
  pinning next; first classify the `r[1]` loads by block/predecessor/alias
  shape or move to a pre-register-allocation live-in/state-cache design.
- HIR `r[1]` load-shape report:
  `docs/research/20260522-125206-r1-load-shape-report.md`.
  Use `tools/thor/thor_hir_gpr_load_shape_report.ps1` for exact GPR load
  shape checks. On `82282490`, `r[1]` has `107` exact loads, `11` exact stores,
  `0` aliasing stores, `76` first loads in their block, `87` loads in
  multi-predecessor blocks, and `31` loads after a context barrier before the
  next branch. This explains why post-RA emit-time and fallthrough pinning
  produced zero hits. Do not tune the old caches next; add a CFG/live-in
  availability report or guarded pre-register-allocation state-cache design
  that proves all predecessors leave `r[1]` clean before replacing loads.
- HIR `r[1]` live-in availability report:
  `docs/research/20260522-150536-r1-livein-availability-report.md`.
  Use `tools/thor/thor_hir_gpr_livein_report.ps1` for edge-aware target-GPR
  availability before a pre-RA cache patch. On `82282490`, strict helper and
  barrier flushing exposes only `14` replaceable first `r[1]` loads, while
  helper-flush plus `context_barrier` preservation exposes `56` replaceable
  first loads and `61` total loads. Next runtime patch should be default-off,
  function-filtered, audited, pre-register-allocation, and barrier-aware; it
  must kill state at calls/helpers, exits, exceptions, volatile context ops,
  and exact/aliasing writes.
- HIR block-mix report:
  `docs/research/20260522-163537-82282490-block-mix-report.md`.
  Use `tools/thor/thor_hir_block_mix_report.ps1` before the next `82282490`
  codegen patch. Known command:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_block_mix_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -BlockProfileLog scratch\thor-debug\20260521-170107-speed-logcat.txt -Top 20`.
  The report joins OptHIR block shape to the prior block profile by guest PC
  first, then block index. Dynamic entry-count hot blocks are the early control
  path: `822824B8`, `822824F0`, `822825E0`, `822825F4`, `822825C8`,
  `82282490`, `82282600`, and `822824EC`. Static context/vector-heavy blocks
  include `82282CE4`, `82282678`, `82282C2C`, `822849B8`, `822847E8`,
  `82283DBC`, `822836C8`, and `82283828`, but those need block body-time proof
  before a broad VMX rewrite. Current next target is dynamic-hot mixed block
  `822824F0`, or a lower-noise block body-time profiler if static vector-heavy
  blocks are revisited.
- HIR block-detail report:
  `docs/research/20260522-164404-822824f0-hir-profile-audit.md`.
  Use `tools/thor/thor_hir_block_detail_report.ps1` for a single hot block.
  Known command:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_block_detail_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -BlockGuest 822824F0 -Top 20`.
  `822824F0` has `198` HIR instructions, `22` context loads, `43` context
  stores, `16` memory loads, `4` memory stores, `3` permutes, `3` `mul_add`,
  `6` splats, `9` extracts, two calls (`0x82274DB0`, `0x82287788`), and
  `5` context barriers. It is mixed CR6 gate, stack/call setup, vector
  math/store, FPR/FPSCR, and CR tail work. Do not patch it from entry counts
  alone; next add per-block body-time attribution or an A64 `stvewx` /
  `extract` / `splat` codegen audit before a peephole.
- HIR vector-shape report:
  `docs/research/20260522-165526-82282490-vector-shape-audit.md`.
  Use `tools/thor/thor_hir_vector_shape_report.ps1` before any
  `stvewx` / `extract` / `splat` peephole. Known command:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_vector_shape_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -BlockProfileLog scratch\thor-debug\20260521-170107-speed-logcat.txt -Top 12`.
  Current `82282490` read: `117` blocks, `6798` HIR instructions,
  `extract=22`, `extract_dynamic=6`, `splat=152`, `extract_then_splat=16`,
  `stvewx=6`, `stvewx_store1=6`, `stvewx_dynamic_extract=6`, `mul_add=6`,
  `permute=381`, `load_vector_shl=73`, and `load_vector_shr=64`.
  `822824F0` is the only dynamic-hot vector block in the old entry-count
  profile (`total=1994364`) and carries `3` `stvewx`, `3` dynamic extracts in
  `stvewx`, `9` total extracts, `6` splats, `3` `mul_add`, and `3` permutes.
  Static-heavy vector blocks still need body-time proof. This no longer
  justifies a peephole by itself; use block body-time evidence below first.
- A64 block body-time profiler:
  `docs/research/20260522-171725-82282490-block-body-time-profiler.md`.
  `arm64_speed_profile_block_body_time` is default-off and launchable through
  `tools/thor/thor_xenia_debug.ps1 -Arm64SpeedProfileBlockBodyTime true` with
  `-Arm64SpeedProfileBlockFilter 82282490`. `tools/thor/thor_hir_block_mix_report.ps1`
  now parses `A64 speed profile block body top` rows and adds a `Dynamic
  Body-Time Blocks With HIR Mix` section. Capture
  `scratch/thor-debug/20260522-170927-*` reached the visible opening sky/wing
  route with no searched fatal markers. The body-time ranking overturns the old
  entry-count target: `822825E0` dominates (`body_total=34726883`, peak
  `14525259`, peak `ticks_per_entry=61`), then `822825C8`
  (`body_total=3216407`, peak `1041116`, peak `ticks_per_entry=500`), then
  `822824F0` (`body_total=1280491`, peak `554835`, peak `ticks_per_entry=1`).
  Do not start the `822824F0` `stvewx` peephole yet.
- `82282490` call-path audit:
  `docs/research/20260522-173542-82282490-call-path-audit.md`.
  Use `tools/thor/thor_hir_call_path_report.ps1` to join focused HIR dumps to
  block body-time captures. Current result: the hottest charged target is
  `822825E0 -> 0x82282490` (`charged_body_total=34726883`), followed by
  `822825C8 -> 0x8227FEE8` (`charged_body_total=3216407`). `822825E0` is only
  a child-pointer guard around a recursive call, and `822825C8` is a compact
  call setup computing `r5 = r28 + ((*(r31 + 0) << 6) & 0xFFFFFFC0)`. Capture
  `scratch/thor-debug/20260522-172738-*` tried to dump `8227FEE8` but idled
  before the route and emitted no filtered dump, with no fatal markers. Next
  useful slice is a default-off direct-call edge/body profiler for
  `822825E0 -> 82282490` and `822825C8 -> 8227FEE8`, or a control-sandwiched
  callee capture after proving the route is stable.
- A64 call-edge profiler:
  `docs/research/20260522-181040-a64-call-edge-profiler.md`.
  `arm64_speed_profile_call_edge_filter` is default-off and launchable through
  `tools/thor/thor_xenia_debug.ps1 -Arm64SpeedProfileCallEdgeFilter 82282490`.
  It profiles non-tail direct guest call edges inside filtered caller
  functions and logs `A64 speed profile call edge top` rows. The first capture
  `scratch/thor-debug/20260522-175432-*` found and fixed an instrumentation
  bug: the profiler clobbered `x9` after loading the direct callee address.
  Fixed edge capture `scratch/thor-debug/20260522-175951-*` and same-APK
  control `scratch/thor-debug/20260522-180335-*` both had no fatal markers but
  black-idled before `82282490`, so no call-edge rows existed yet. Follow-up
  control sandwich `docs/research/20260522-183742-call-edge-control-sandwich.md`
  tightened the read, but the later audit-only note
  `docs/research/20260522-190502-call-edge-audit-only-and-edge-proof.md`
  corrected the important route detail: the route-stable runs include delayed
  `arm64_speed_profile_body_time_filter=82282490`. New default-off
  `arm64_speed_profile_call_edge_audit_only` logs compile-time slot counts
  without emitting edge counters. Audit-only with the delayed body-time filter
  reached opening and logged `blocks=164`, `direct_call_edges=60`,
  `instrumentation=0`; audit-only without the body-time filter black-idled
  before `82282490` and emitted no compile-audit row. Real call-edge profiling
  with the delayed body-time filter also reached opening and produced dynamic
  edge rows: `822825E0 -> 82282490` dominates (`body_ticks_total=21299726`,
  peak `7315115`), followed by `822825C8 -> 8227FEE8`
  (`body_ticks_total=4515376`, peak `1933191`). Keep both call-edge cvars
  default-off, but future edge captures for this route should include
  `-Arm64SpeedProfileBodyTimeFilter 82282490` and
  `-Arm64SpeedProfileBodyTimeAfterMs 120000`. Next priority is a focused
  `8227FEE8` HIR/body-time capture or a recursive-child-path audit from the
  proven edge rows, not the stale `822824F0` `stvewx` peephole.
- `8227FEE8` focused capture:
  `docs/research/20260522-193049-8227fee8-focused-capture.md`.
  Route-clean capture `scratch/thor-debug/20260522-191427-*` reached the visible
  opening sky/wing route with no searched fatal markers and measured
  `8227FEE8` as a real secondary child target (`body_ticks_total=3650429`,
  peak delta `1408271`, peak `ticks_per_entry=738`, code size `49804`), while
  `82282490` still dominated (`body_ticks_total=25866736`). The capture did not
  include HIR because the stable speed lane uses `log_level=1` and filtered
  dumps were info-level. A `-LogLevel 0` retry `scratch/thor-debug/20260522-191832-*`
  black-idled before useful route progress. Filtered function dumps now log at
  warning level and `tools/thor/thor_xenia_debug.ps1` records the disassembly
  filter in metadata, but first patched filtered run
  `scratch/thor-debug/20260522-192429-*` black-idled before `82282490` and
  emitted no HIR/body rows. Keep the tooling patch, do not rerun that exact
  capture unchanged, and next prove patched APK route safety with a no-filter
  control or a control-sandwiched filtered capture before relying on
  `8227FEE8` HIR.
- Patched no-filter route control:
  `docs/research/20260522-230909-patched-route-control-black-idle.md`.
  Capture `scratch/thor-debug/20260522-230518-*` used HEAD `0ec440af6`, APK SHA
  `89086669EF6CC19A028049FBF5957827CF7CFA97C85F0083021A739B9C16FFAF`, no
  disassembly filter, delayed body-time filter `82282490,8227FEE8`, and
  `arm64_context_promotion_gpr_livein_r1=false`. It still black-idled before
  route progress: counters went flat from `23:06:05` onward, body-time
  activated with no `82282490`/`8227FEE8` body rows, final screenshot was
  black, and fatal-marker search was clean. Idle snapshot triggered but skipped
  because the processor debug lock stayed busy with
  `last_global_owner_sys_tid=21741`.
- Idle owner attribution and patched route recheck:
  `docs/research/20260522-232945-a64-idle-owner-attribution.md`.
  The A64 idle snapshot skip line now includes a lock-free native-TID hint
  (`owner_hint`, guest thread ID, handle, and state) when the processor debug
  lock is busy; successful thread snapshots also include `native=...`. Patched
  capture `scratch/thor-debug/20260522-232133-*` used APK SHA
  `E92DAC2CB4E7080C196DB9656305F372DC20C189E7697A2FCCD47D1E12DA3FA3`, reached
  the visible opening sky/wing route with no searched fatal markers, and did
  not exercise the owner-hint line because counters stayed active. Next run a
  control-sandwiched filtered `8227FEE8` capture with delayed body-time route
  stabilizer. If it reaches opening, use the warning-level HIR dump for focused
  codegen audit; if it black-idles, inspect `owner_hint` before changing guest
  behavior.
- `8227FEE8` filtered HIR sandwich:
  `docs/research/20260522-234847-8227fee8-filtered-hir-sandwich.md`.
  Filtered middle capture `scratch/thor-debug/20260522-233545-*` used the same
  APK SHA `E92DAC2CB4E7080C196DB9656305F372DC20C189E7697A2FCCD47D1E12DA3FA3`,
  `-DisassembleFunctionFilter 8227FEE8`, and delayed body-time
  `82282490,8227FEE8`; it reached visible opening, logged the warning-level
  `8227FEE8` OptHIR dump, and had no searched fatal markers. Final body-time
  still showed `82282490` dominant (`body_ticks_total=26728115`,
  `ticks_per_entry=120`) with `8227FEE8` secondary
  (`body_ticks_total=4125095`, `ticks_per_entry=264`, code size `49804`).
  HIR shape for `8227FEE8`: `store_context=1426`, `load_context=896`,
  `context_barrier=332`, `branches=284`, `calls=78`, `permute=48`,
  `byte_swaps=30`, no dot/extract/splat/stvewx surface. The static vector
  candidates are blocks `82280A68` and `82280E1C`, each with
  `lvlx/lvrx/stvlx/stvrx/vor` and `perm=24`, but they still need body-time
  proof. Post-control no-filter capture `scratch/thor-debug/20260522-234038-*`
  black-idled with clean fatal search and `owner_hint=miss` for
  `last_global_owner_sys_tid=7347`, so route nondeterminism remains. Next run
  delayed `8227FEE8` block body-time with
  `-Arm64SpeedProfileBlockFilter 8227FEE8 -Arm64SpeedProfileBlockBodyTime true`;
  do not start broad r1/GPR cache or vector peepholes without that block
  attribution.
- `8227FEE8` block body-time black-idle and owner attribution:
  `docs/research/20260523-001018-a64-owner-thread-id-attribution.md`.
  Capture `scratch/thor-debug/20260522-235449-*` attempted delayed
  `8227FEE8` function/block body-time but black-idled before useful target
  rows: screenshot black, no searched fatal markers, and no `8227FEE8` body or
  block rows after activation. The idle line again reported a busy processor
  debug lock with `last_global_owner_sys_tid=14186` and `owner_hint=miss`.
  Diagnostic patch now records both native system TID and
  `xe::threading::current_thread_id()` for the global critical-region owner,
  lets `Processor` query the lock-free thread hint cache by guest thread ID or
  handle, and logs `last_global_owner_thread_id`, `owner_hint_source`, and
  `owner_hint_sys_tid`. NativeCore and FullDeploy passed; patched APK SHA
  `962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5`.
  Short validation capture `scratch/thor-debug/20260523-000506-*` stayed
  active at the loading spinner with clean fatal search, so it build/deploy
  validated the patch but did not exercise the new idle line.
- `8227FEE8` route-clean block body-time:
  `docs/research/20260523-122718-8227fee8-block-body-time-route-clean.md`.
  Capture `scratch/thor-debug/20260523-122223-*` reached the visible opening
  sky/wing route on APK SHA
  `962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5` with a
  clean fatal-marker search. It finally produced delayed `8227FEE8` block
  body-time rows. Final `8227FEE8` function row:
  `body_ticks_total=6914659`, `body_ticks_delta=1871176`,
  `entries_delta=2041`, `ticks_per_entry=916`, `code_size=71868`.
  Internal block body-time points away from the stale `82280A68` /
  `82280E1C` vector peephole for now. The dynamic wall is call-heavy block
  `822809F4` (`body_total=1937201`, peak `665665`, peak
  `ticks_per_entry=128`) with direct calls to `0x8227F1D8` and
  `0x8247BE20`. Next split those callees with a delayed body-time/call-edge
  capture for `8227FEE8,8227F1D8,8247BE20` before writing a codegen patch.
- `8227FEE8` callee call-edge split:
  `docs/research/20260523-124029-8227fee8-callee-call-edge-split.md`.
  Route-clean capture `scratch/thor-debug/20260523-123406-*` reached the
  visible opening sky/wing route on APK SHA
  `962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5` with a
  clean fatal-marker search. Final body-time put `8227F1D8` first in the last
  interval (`body_ticks_delta=844204`, `body_ticks_total=3714635`,
  `ticks_per_entry=84`, `code_size=20180`) while `8247BE20` stayed tiny
  (`body_ticks_total=27747`, `ticks_per_entry=2`, `code_size=796`). Dynamic
  edge timing under `8227FEE8` confirms `822809F4 -> 8227F1D8` as the wall:
  `calls_total=26098`, `body_ticks_total=2031295`, peak delta `1137492`, and
  peak `ticks_per_call=216`. The next speed slice should dump and profile
  `8227F1D8` itself with delayed body/block-time; do not start the old
  `82280A68` / `82280E1C` vector peephole or broad GPR cache from this
  evidence alone.
- `8227F1D8` filtered HIR and block body-time:
  `docs/research/20260523-131338-8227f1d8-filtered-hir-block-body-time.md`.
  Route-clean capture `scratch/thor-debug/20260523-130934-*` reached visible
  opening, emitted warning-level `8227F1D8` OptHIR, and had a clean
  fatal-marker search. Final `8227F1D8` row:
  `body_ticks_total=4238549`, `body_ticks_delta=907049`,
  `entries_delta=9675`, `ticks_per_entry=93`, `code_size=28180`. Internal
  block body-time is almost entirely block `8227F1D8`
  (`body_ticks_total=4152240`, peak delta `1764204`), and that block has two
  calls: `0x82490030` and `0x826BFC7C`. Next split those two edges with
  `-Arm64SpeedProfileCallEdgeFilter 8227F1D8` and delayed body-time for
  `8227F1D8,82490030,826BFC7C`; do not start a function-local peephole yet.
- `8227F1D8` call-edge split:
  `docs/research/20260523-153235-8227f1d8-call-edge-split.md`.
  Route-clean capture `scratch/thor-debug/20260523-152754-*` reached visible
  opening on APK SHA
  `962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5` with a
  clean fatal-marker search. Final dynamic rows show `82490030` almost fully
  explains `8227F1D8`: parent `8227F1D8 body_ticks_total=4117139`, child
  `82490030 body_ticks_total=4007328`, and edge
  `8227F1D8 -> 82490030 body_ticks_total=4054641`, `calls_total=32107`,
  `ticks_per_call=72`. `826BFC7C` did not appear as a meaningful dynamic row
  in this route. Next capture should dump/profile `82490030` with delayed
  body/block-time and keep `8227F1D8` as the parent comparator; do not start a
  local `8227F1D8` peephole yet.
- `82490030` filtered capture black-idle:
  `docs/research/20260523-154117-82490030-filtered-capture-black-idle.md`.
  Capture `scratch/thor-debug/20260523-153726-*` used the intended
  `82490030` disassembly filter plus delayed `8227F1D8,82490030` body-time,
  but black-idled before the opening route. Fatal-marker search was clean, no
  `82490030` HIR/body/block rows landed, and the final screenshot was black.
  Counters went flat by `15:38:15`; body-time activated later with
  `entry_delta=0`. Idle attribution reported a busy processor debug lock with
  `last_global_owner_sys_tid=15739`, `last_global_owner_thread_id=F80002E8`,
  `owner_hint=hit`, `owner_hint_source=thread_id_or_handle`,
  `owner_hint_tid=00000016`, `owner_hint_handle=F80002E8`, and
  `owner_hint_state=zombie`. Do not repeat this exact filtered run unchanged
  and do not patch `82490030` from it.
- `82490030` no-disassembly control black-idle:
  `docs/research/20260523-155012-82490030-control-black-idle.md`. Capture
  `scratch/thor-debug/20260523-154626-*` used the same APK SHA
  `962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5`, no
  disassembly filter, delayed body-time for `8227F1D8,82490030`, and
  `arm64_context_promotion_gpr_livein_r1=false`. It also black-idled before
  visible route progress: fatal-marker search was clean, no `8227F1D8` or
  `82490030` body rows landed, final screenshot was black, and counters went
  flat by `15:47:15`. Idle attribution repeated the same zombied owner path:
  `last_global_owner_sys_tid=20126`, `last_global_owner_thread_id=F80002E8`,
  `owner_hint=hit`, `owner_hint_source=thread_id_or_handle`,
  `owner_hint_sys_tid=946810032`, `owner_hint_tid=00000016`,
  `owner_hint_handle=F80002E8`, and `owner_hint_state=zombie`. Do not rerun the
  exact filtered `82490030` capture or the exact no-disassembly control
  unchanged, and do not patch generated code from this evidence. Next improve
  zombie owner/native TID attribution, especially live `/proc/<pid>/task/<tid>`
  mapping for `last_global_owner_sys_tid` and clearer separation from stale
  `owner_hint_sys_tid`.
- A64 owner native-TID attribution:
  `docs/research/20260523-160650-a64-owner-native-tid-attribution.md`.
  Diagnostic patch `scratch/thor-debug/20260523-160357-*` deployed APK SHA
  `2E3D88F46BB709AA3A869634C24219FBBA0568695C1F4902693132701CF9EBE5` and
  reproduced the black-idle with clean fatal-marker search. The new idle line
  fixed the stale Android `pthread_t` hint problem: `owner_hint_source=system_tid`
  and `owner_hint_sys_tid=28245` now match `last_global_owner_sys_tid=28245`.
  `/proc/self/task/28245` was already gone
  (`native_owner_live=false`, `owner_hint_native_live=false`) while the cached
  XThread was `owner_hint_state=zombie`. The next lane is not 82490030 codegen;
  it is diagnostic global critical-region ownership/lifetime attribution around
  the processor debug lock, so the next black-idle can tell stale last-owner
  bookkeeping from a native thread exiting while holding the lock.
- Global critical owner-source attribution:
  `docs/research/20260523-232053-global-critical-owner-source-attribution.md`.
  Diagnostic patch adds global critical-region owner sequence, owner age, and
  owner source fields to the A64 idle skip line, and tags `Acquire`,
  `AcquireDirect`, `TryAcquire`, `XThread::LockApc`, and
  `ObjectTable::LookupObject`. `NativeCore` and `FullDeploy` passed; APK SHA
  `D68ED877D6C265420CD6853AB6C108C18F17E23911C2A1135016797D345C4823`.
  Short validation `scratch/thor-debug/20260523-231636-*` stayed active for
  100 seconds with clean fatal-marker search and no idle-snapshot skip line,
  ending at the loading overlay. This proves no early diagnostic regression,
  but it did not yet exercise `global_lock_owner_*`. Next run a longer
  route-stabilized attribution capture; if it black-idles, inspect
  `global_lock_owner_seq`, `global_lock_owner_age_ms`, and
  `global_lock_owner_source`. If it stays route-clean, return to measured
  performance profiling instead of repeating stale `82490030` captures.
- Processor thread lifecycle owner tags:
  `docs/research/20260523-234203-processor-thread-lifecycle-owner-tags.md`.
  After the 180-second attribution capture
  `scratch/thor-debug/20260523-232432-*` reproduced black-idle with
  `global_lock_owner_source='Acquire'`, add a tagged `Acquire(source)` overload
  and label `Processor::OnThreadCreated`, `OnThreadNativeStarted`,
  `OnThreadExit`, `OnThreadDestroyed`, `OnThreadEnteringWait`, and
  `OnThreadLeavingWait`. `NativeCore` and `FullDeploy` passed; APK SHA
  `862F86C44625B460A5BAB8528E25AB4E946F52CDB30137D7479D24AC3BD50FCB`.
  Short validation `scratch/thor-debug/20260523-233953-*` stayed active for
  100 seconds at the loading overlay with clean fatal-marker search and no idle
  skip line, so the new specific labels are route-safe but not yet exercised.
  Next run a longer tagged-lifecycle attribution capture; if black-idle
  reproduces, inspect whether `global_lock_owner_source` names a processor
  lifecycle method before changing lock behavior.
- Object release outside global lock:
  `docs/research/20260523-235800-object-release-outside-global-lock.md`.
  Tagged-lifecycle capture `scratch/thor-debug/20260523-234500-*` reproduced
  black-idle on commit `11747d104` and named
  `global_lock_owner_source='Processor::OnThreadDestroyed'`. The fix defers
  `object->Release()` in `ObjectTable::ReleaseHandle` and `RemoveHandle` until
  after the object table/global critical-region lock is dropped. `NativeCore`
  and `FullDeploy` passed; APK SHA
  `FE5CBCF23E832807E51547D66387C3680DF6DB4B4802C9DB5BB64ACB46B8489B`.
  Validation `scratch/thor-debug/20260523-235417-*` reached the visible
  Blue Dragon sky/wing opening route with clean fatal-marker search, no idle
  snapshot skip line, and live body-time rows. Treat the
  `Processor::OnThreadDestroyed` black-idle as fixed unless it reappears with
  fresh evidence. Resume performance profiling from the restored
  `8227F1D8 -> 82490030` route.
- Current `82490030` split:
  `docs/research/20260524-001152-82490030-call-edge-split.md`.
  Route-clean capture `scratch/thor-debug/20260524-000643-*` reached the
  visible Blue Dragon sky/wing opening route, had clean fatal-marker search,
  and emitted no idle owner lines. The dynamic child target summary says
  `82486178` is the next target (`body_ticks_total=2302621`,
  `calls_total=33218`), followed by `82485DD8` (`804023`) and `82486018`
  (`679426`). Do not patch local `82490030` codegen yet; run a filtered HIR
  plus delayed body/block-time capture for `82486178` with `82490030`,
  `82485DD8`, and `82486018` as body-time comparators.
- `82486178` VMX copy-loop fastpath probe:
  `docs/research/20260524-005156-82486178-vmx-copy-loop-fastpath.md`.
  The default-off `arm64_blue_dragon_vmx_copy_loop_fastpath` toggle is
  function/block gated to `82486178:8248627C`. It is route-clean, but the
  same-APK inner-loop A/B did not prove speed: fastpath-on
  `scratch/thor-debug/20260524-004420-*` reached visible opening with
  `82486178 body_ticks_total=2050839`, while fastpath-off control
  `scratch/thor-debug/20260524-004802-*` reached visible opening with
  `82486178 body_ticks_total=1727468`. Keep the cvar default-off and do not
  repeat that exact A/B unchanged. Next speed slice should use fresh evidence
  on remaining `82490030` children, especially `82485DD8` and `82486018`.
- `82485DD8` word copy-loop fastpath probe:
  `docs/research/20260524-011500-82485dd8-word-copy-loop-fastpath.md`.
  Fresh route-clean HIR/block capture `scratch/thor-debug/20260524-010013-*`
  identified `82485E70` as a local word copy/update loop. The default-off
  `arm64_blue_dragon_word_copy_loop_fastpath` toggle is gated to
  `82485DD8:82485E70` and is a positive same-APK profiled A/B:
  fastpath-on `scratch/thor-debug/20260524-010729-*` lowered
  `82485DD8 body_ticks_total` to `695645` and `82485E70` block body-time to
  `75310`; fastpath-off control `scratch/thor-debug/20260524-011110-*` had
  `82485DD8=2307804` and `82485E70=1122726`.
  `docs/research/20260524-013145-82485dd8-word-loop-quiet-ab.md` confirmed the
  route-wide win without block body-time/disassembly: quiet fastpath-off
  `scratch/thor-debug/20260524-012352-*` had `82490030=3923146` and
  `82485DD8=1010794`, while quiet fastpath-on
  `scratch/thor-debug/20260524-012713-*` had `82490030=3573598` and
  `82485DD8=774505`. The Blue Dragon Thor speed preset may enable this toggle;
  keep the backend cvar globally default-off and do not combine it with the
  `82486178` VMX copy-loop toggle yet.
- Word-loop preset rebaseline:
  `docs/research/20260524-014050-word-loop-preset-rebaseline.md`.
  Capture `scratch/thor-debug/20260524-013649-*` confirmed the Blue Dragon
  speed preset sends `arm64_blue_dragon_word_copy_loop_fastpath=true`, reached
  the visible opening sky/wing route, and had clean fatal-marker search. Final
  body-time moved the main wall back to the opening-scene cluster:
  `82282490=25383515`, `82281D28=7175068`, `82490030=3480696`,
  `82282388=2082550`, `82282410=2004094`, `82486178=1358411`,
  `82485DD8=618538`, `82486018=457433`. Next speed slice should run delayed
  `82282490` block body-time under the current preset; if the old
  `822825E0 -> 82282490` recursive wall still dominates, use call-edge/HIR
  callee analysis there.
- Current `82282490` recursive wall and stack-sync probe:
  `docs/research/20260524-021116-82282490-recursive-stack-sync-probe.md`.
  Fresh block body-time `scratch/thor-debug/20260524-014437-*` and call-edge
  capture `scratch/thor-debug/20260524-014858-*` reached the visible opening
  sky/wing route with clean fatal searches and confirmed the current wall is
  recursive call work, not stale `822824F0` vector work. The top edge is
  `822825E0 -> 82282490` (`body_ticks_total=28602334`,
  `calls_total=117425`, peak `ticks_per_call=1244`), with
  `822825C8 -> 8227FEE8` secondary (`body_ticks_total=11951162`). A64 direct
  guest calls use generated-code `blr` when the target has machine code, so the
  hot recursive edge is not repeatedly entering via the host-to-guest thunk.
  Android/Thor tooling now exposes
  `a64_enable_host_guest_stack_synchronization`; keep the Blue Dragon speed
  pack default at `true`. Stack-sync-off was route-clean twice and
  deterministically shrank code size for `82282490` (`87168` -> `85104`), but
  the second off capture landed at `82282490=27192157`, essentially matching
  the stack-sync-on control (`27192906`). Treat this as code-size evidence, not
  a speed proof.
- Entry/exit profiler update:
  `docs/research/20260524-025544-a64-entry-exit-profiler.md`. The default-off
  `arm64_speed_profile_entry_exit_time_filter` / Thor
  `-Arm64SpeedProfileEntryExitTimeFilter` lane reached the visible opening
  route in `scratch/thor-debug/20260524-025000-*` with clean fatal search.
  `82282490` prolog+epilog/stackpoint attribution was only `273393` ticks over
  `186010` entries, or `1.47` ticks/call and `0.93%` of body total.
  `82281D28` was similarly small at `1.547` ticks/call and `1.56%` of body
  total. Do not run more unchanged stack-sync or entry/exit A/B captures, and
  do not treat stackpoint/prolog/epilog as the main wall. Next focus is
  exclusive attribution inside `82282490` body/recursive child work, or fresh
  body-backed state-traffic reduction.
- Current `82282490` exclusive attribution:
  `docs/research/20260524-031017-82282490-exclusive-callee-attribution.md`.
  Same-run block body-time plus call-edge timing
  `scratch/thor-debug/20260524-030450-*` reached the visible opening route
  with clean fatal search. Dynamic direct-call edges account for `84969304`
  ticks, or `84.08%` of final `82282490` body ticks; the self-recursive
  `822825E0 -> 82282490` edge alone is `72194274` ticks, about `71.44%` of
  final `82282490` body time and `98.32%` of the `822825E0` block body. Treat
  this as callee-body work, not generic call/prolog overhead. Do not rerun this
  exact capture unchanged. Next map dynamic block-body guests such as
  `8228252C` back to exact HIR/PPC spans before a local `82282490` codegen or
  state-traffic patch.
- Current `82282490` dynamic block slicing:
  `docs/research/20260524-032005-82282490-dynamic-block-slice-report.md` and
  `tools/thor/thor_hir_dynamic_block_slice_report.ps1`. The first report maps
  `8228252C` to exact slice `8228252C-822825C4`, with body total `8530471`,
  edge body `5653971`, and approximate exclusive `2876500`. This is now the
  top local `82282490` patch candidate. Its shape is vector/FPR/state traffic:
  `vspltw:6`, `lvx128:4`, `stvewx:3`, `vmaddfp:3`, `store_context:27`,
  `load_context:14`, `extract:9`, and `splat:6`. Next run an A64 generated
  code audit for this slice before implementing a default-off function/block
  gated vector/store or state-traffic experiment.
- Current `8228252C` A64 generated-code audit:
  `docs/research/20260524-033116-8228252c-a64-codegen-audit.md` and
  `tools/thor/thor_hir_a64_codegen_audit.ps1`. The audit confirms the local
  slice is mostly context traffic plus three `stvewx` dynamic extract/store
  shapes and three heavy `vmaddfp` lowerings. Approximate local exclusive work
  is `2876500` ticks after subtracting the `82287788` child edge. Do not start
  a broad `822824F0` vector peephole. Next slice should prove stack lane
  provenance for `82282580`, `82282584`, and `82282588`, or make a default-off
  function/block-gated `stvewx` lane-folding probe only for the two apparent
  `r1 + constant` stores, with rollback and correctness logging.
- Current `8228252C` `stvewx` lane provenance:
  `docs/research/20260524-034103-8228252c-stvewx-lane-provenance.md` and
  `tools/thor/thor_hir_stvewx_lane_audit.ps1`. The audit proves
  `82282580 -> lane 0` from `r1 + 0x50` and `82282584 -> lane 1` from
  `r1 + 0x54` under the normal 16-byte PPC stack-pointer alignment assumption.
  `82282588` remains unknown because its address is `r6 + 0x8`.
- Current `8228252C` `stvewx` lane fastpath probe:
  `docs/research/20260524-040404-blue-dragon-stvewx-lane-fastpath.md`.
  The default-off, Blue-Dragon/function/PC-gated A64 `EXTRACT_I32` fastpath
  for only `82282580` and `82282584` is route-clean and dynamically audited:
  `scratch/thor-debug/20260524-035227-*` reached the visible opening sky/wing
  route with clean fatal search and final audit counters
  `fastpath=446272/1338814 fallback=0/0`. Quiet same-APK A/B was not a speed
  win: control `scratch/thor-debug/20260524-035623-*` reached
  `Microsoft Game Studios Presents`, while fastpath-on
  `scratch/thor-debug/20260524-035944-*` reached an earlier sky/wing frame.
  Keep `arm64_blue_dragon_stvewx_stack_lane_fastpath` and audit default-off.
  Treat the patch as route-clean/code-size evidence only (`82282490`
  `87168 -> 87088` bytes), and do not repeat the exact A/B unchanged. Next
  target should be larger body-backed work in `8228252C-822825C4`, especially
  `8228252C -> 82287788` or broader state/vector/FPR traffic.
- Current `82287788` focused child-lane read:
  `docs/research/20260524-042555-82287788-focused-callee-split.md`.
  Two route-clean captures (`scratch/thor-debug/20260524-041413-*` and
  `scratch/thor-debug/20260524-042031-*`) reached the visible opening sky/wing
  route on APK SHA
  `A4CB9C0E8F4CBB63EB09F3DA38640BEA5410DFE5916A155627C18F3B90C39BD8` with
  clean fatal-marker searches. The lower-noise call-edge capture keeps
  `82282490` dominant at `31584286` body ticks, with `82287788` secondary at
  `3659269` body ticks (`ticks_per_entry=5`, `code_size=35380`). The only
  meaningful dynamic child edge was `82287788 -> 821CE028` at `1147798` body
  ticks over `340310` calls. The top local span is `822877BC-82287B38`:
  `store_context=186`, `load_context=145`, `context_barrier=15`,
  `lvlx/lvrx/vor/stvx`, `lvx128:6`, `vmaddfp:6`, `vspltw:6`, constant
  `extract=6`, and `splat=10`. Do not revive the known-crashy CR
  compare/barrier fusion or CR-store elision cvars, and do not restart broad
  VMX-dot or stale `822824F0` peepholes.
- Current `822877BC` span-reduction audit:
  `docs/research/20260524-044223-822877bc-span-reduction-audit.md`. The new
  `tools/thor/thor_hir_span_reduction_audit.ps1` folds together filtered HIR,
  block-body rows, and separate call-edge rows. It found the parent span
  `822877BC-82287B38` has `body_ticks_total=1173620`, but the child edge
  `82287788 -> 821CE028` accounts for `1147798` body ticks over `340310`
  calls, leaving only `25822` approximate parent-exclusive ticks (`2.2%`).
  Treat the parent CR/state/vector traffic as misleading until the callee is
  understood. Next run should be a route-stabilized filtered HIR plus delayed
  body/block-time capture for `821CE028`, keeping `82282490` and `82287788` as
  comparators. Do not patch local `822877BC-82287B38` generated code first.
- Current `821CE028` focused child read:
  `docs/research/20260524-045602-821ce028-focused-capture.md`. Capture
  `scratch/thor-debug/20260524-045022-*` reached the visible opening sky/wing
  route with clean fatal-marker search on APK SHA
  `A4CB9C0E8F4CBB63EB09F3DA38640BEA5410DFE5916A155627C18F3B90C39BD8`. Final
  body rows were `82282490=37190996`, `82281D28=7446911`,
  `82287788=4992080`, and `821CE028=2523574`. `821CE028` is fully local; top
  block `821CE048-821CE1CC` had `body_total=607573`, no child edge, and a
  repeated VMX compare/dot/CR reduction shape (`vmsum4fp128*:6`,
  `vcmpgefp.:6`, `vcmpgtfp.:6`, `vslw:6`, `vxor:6`, `mfcr:12`,
  `rlwinm:13`). Do not patch it immediately: CR store/barrier and broad
  VMX-dot lanes remain rejected, and the only safe-looking lane is a narrow
  offline `r10` state-traffic audit. Prefer the larger next target:
  route-stabilized filtered HIR plus delayed body/block-time capture for
  `82281D28`, keeping `82282490`, `82287788`, and `821CE028` as comparators.
- Current `82281D28` focused capture:
  `docs/research/20260524-050931-82281d28-focused-capture.md`. Capture
  `scratch/thor-debug/20260524-050427-*` reached the visible opening sky/wing
  route with clean fatal-marker search on the same APK SHA
  `A4CB9C0E8F4CBB63EB09F3DA38640BEA5410DFE5916A155627C18F3B90C39BD8`.
  Final body rows put `82281D28` first at `65346716`, followed by
  `82282490=31615045`, `82282388=18965773`, `82282410=17323669`,
  `82287788=3277477`, and `821CE028=716173`. The hottest internal block row
  is `8228233C` with `body_ticks_total=46564347`, but the warning-level HIR
  dump starts at `82282398`, so exact local span mapping is incomplete. Visible
  HIR after that point shows calls to `0x826BF770` and recursive
  `0x82281D28`, and the sibling/alias-looking `82282388` and `82282410` body
  rows are also large. Do not patch local `82281D28` generated code yet. Next
  run should be a route-stabilized `82281D28` call-edge split with delayed
  body-time comparators:
  `82281D28,82282388,82282410,82282490,826BF770,82287788,821CE028`.
- Current `82281D28` call-edge split:
  `docs/research/20260524-052524-82281d28-call-edge-split.md`. Capture
  `scratch/thor-debug/20260524-052031-*` reached the visible opening sky/wing
  route with clean fatal-marker search on the same APK SHA. With the HIR/block
  dump removed, `82282490` returned to the top body row:
  `82282490=30214411`, `82281D28=10212419`, `82287788=3195362`,
  `82282388=3020644`, `82282410=2902097`, `821CE028=674464`, and
  `826BF770=563064`. Dynamic edge rows say `82281D28` is mostly recursive:
  `8228233C -> 82281D28` has `body_ticks_total=7350722`,
  `calls_total=69445`, and accounts for about `72.0%` of final `82281D28`
  body time. Reported direct edges account for about `89.1%`, leaving only
  about `1115427` ticks of approximate parent-exclusive work. Do not patch
  local `82281D28` generated code yet or rerun the exact HIR/block capture
  unchanged. Next step should collapse/reason about the recursive SCC and
  return to body-backed `82282490` work, or improve the mapper enough to
  recover `8228233C` static span context before any local `82281D28` patch.
- Current call-path attribution helper:
  `docs/research/20260524-053502-call-path-recursive-heuristic.md`. The
  updated `tools/thor/thor_hir_call_path_report.ps1` now prints a
  `Dynamic Recursive/Exclusive Heuristic` from body-time plus call-edge rows.
  Validation reports `82281D28` direct edges at `89.08%` and self-recursion at
  `71.98%`, while `82282490` direct edges are `84.08%` and self-recursion is
  `71.44%`. Use this report before patching call-heavy blocks. The next patch
  candidate should come from body-backed local/exclusive `82282490` spans such
  as the previously mapped `8228252C-822825C4` area, not inclusive recursive
  block totals alone.
- Current `82282490` local/exclusive candidate ranking:
  `docs/research/20260524-054204-82282490-exclusive-candidate-ranking.md`.
  `tools/thor/thor_hir_dynamic_block_slice_report.ps1` now prints a
  `Local Exclusive Candidate Ranking`. It keeps `8228252C-822825C4` first
  (`approx_exclusive=2876500`, `exclusive_pct=33.72`) and `822824F0-82282528`
  second (`approx_exclusive=2021174`, `exclusive_pct=57.72`). Do not repeat
  the exact narrow `stvewx` stack-lane fold A/B for `82282580/82282584`; it
  was route-clean but did not prove speed.
- Current `82282490` top-span comparison:
  `docs/research/20260524-055108-82282490-top-span-comparison.md`.
  `8228252C-822825C4` remains the larger absolute local target
  (`approx_exclusive=2876500`) but its safe lanes are either already rejected
  or need more provenance: CR fusion/elision is closed, exact `stvewx` lane
  fold missed speed proof, and broad state/vector/FPR traffic is too wide for
  a blind toggle. `822824F0-82282528` is smaller but cleaner
  (`approx_exclusive=2021174`, `exclusive_pct=57.72`) with a narrow
  `lvlx/lvrx -> load_vector_shl + permute -> v[0]` shape. Do not patch
  generated-code behavior from the comparison alone. Next slice should add or
  run focused provenance for that exact `822824F0` vector-load join before any
  default-off function/span-gated codegen experiment.
- Current `822824F0` vector-load provenance:
  `docs/research/20260524-060827-822824f0-vector-load-provenance.md` and
  `tools/thor/thor_hir_vector_load_join_audit.ps1`. The auditor confirms
  `822824F0-82282528` is real local work (`body_ticks_total=3501617`, child
  edge `82274DB0=1480443`, approximate exclusive `2021174`), but the vector
  load join is not self-contained. `82282520 lvlx vr13,r0,r11` loads from
  `r30 + 0x14` and stores `v[13]`; `82282528 lvrx vr0,r11,r10` loads from
  `r30 + 0x20`, has the `lvrx` zero path, and stores `v[0]`; the first
  matching `vor vr0,vr13,vr0` join is at `8228254C` in the next span. Do not
  patch local `822824F0-82282528` generated-code behavior yet. Next step is a
  cross-span provenance audit through at least `8228254C`, or a return to the
  larger `8228252C-822825C4` state/vector/FPR target with better provenance.
- Current `822824F0` cross-span consumer audit:
  `docs/research/20260524-062037-822824f0-cross-span-consumer-audit.md`.
  `tools/thor/thor_hir_vector_load_join_audit.ps1` now prints vector consumer
  rows. The full `822824F0-82282574` chain is self-contained in the larger
  span (`lvlx/lvrx`, `vsldoi`, four stack-ish `lvx128` loads, `vor`, three
  `extract+splat` pairs, and three `vmaddfp` lowerings), with
  `body_ticks_total=3501617`, child edge `82274DB0=1480443`, and approximate
  exclusive `2021174`. Do not implement a quick local-only `822824F0` vector
  peephole: the real cost includes heavy `MUL_ADD_V128` lowering with VMX FPCR
  handling, scratch-stack saves, PPC NaN fixup, and denormal handling. Next
  useful step is a generated-instruction estimate/source review for this exact
  chain, then compare it against `8228252C-822825C4` before any default-off
  function/span-gated codegen experiment.
- Current `82282490` source-reviewed span estimate:
  `docs/research/20260524-063109-82282490-span-instruction-estimate.md`.
  `tools/thor/thor_hir_a64_codegen_audit.ps1` now prints a
  `Source-Reviewed A64 Floor Estimate`. The estimate is a heuristic floor, not
  exact generated instruction count. `822824F0-82282574` has estimated floor
  `164`, approximate exclusive `2021174`, and `12324.23` exclusive ticks per
  estimated instruction. `8228252C-822825C4` has estimated floor `192`,
  approximate exclusive `2876500`, and `14981.77` exclusive ticks per
  estimated instruction. Do not patch local-only `822824F0` vector codegen from
  the cross-span audit. The next speed slice should target the larger
  `8228252C-822825C4` lane or a shared `MUL_ADD_V128` cost lane with a
  default-off, function/span-gated audit first. Good concrete next steps are a
  `MUL_ADD_V128` source/audit lane for `82282568/8228256C/82282570`, or better
  `82282588` dynamic-`EXTRACT_I32`/`stvewx` provenance before another lane-fold
  experiment.
- Current `82282588` `stvewx` provenance:
  `docs/research/20260524-064118-82282588-stvewx-provenance.md`.
  `tools/thor/thor_hir_stvewx_lane_audit.ps1` now supports
  `-ProvenanceStartGuest` and simple `store_context` -> `load_context`
  forwarding inside the audited provenance window. With target slice
  `8228252C-822825C4` and provenance start `82282514`, all three dynamic
  `stvewx` extract lanes are proven under the normal 16-byte PPC stack
  alignment assumption: `82282580 -> lane 0`, `82282584 -> lane 1`, and
  `82282588 -> lane 2` via `r6 = r1 + 0x50`, then `r6 + 0x8`. Do not repeat
  the old two-site `stvewx` lane-fold A/B unchanged. If generated code is
  patched next, make it a new default-off, Blue-Dragon/function/PC-gated
  all-three-site `EXTRACT_I32` fastpath with audit counters first. If that
  misses route-speed proof, move to `MUL_ADD_V128` cost instead of another
  narrow `stvewx` tweak.
- Current all-three `stvewx` fastpath audit:
  `docs/research/20260524-065602-blue-dragon-all-three-stvewx-fastpath.md`.
  The existing default-off
  `arm64_blue_dragon_stvewx_stack_lane_fastpath` now folds all three proven
  `82282490` dynamic `EXTRACT_I32` PCs: `82282580 -> lane 0`,
  `82282584 -> lane 1`, and `82282588 -> lane 2`. `NativeCore` and
  `FullDeploy` passed. Capture `scratch/thor-debug/20260524-065132-*` reached
  the visible opening sky/dragon-wing route on APK SHA
  `8A3DA22B2208AA67DE13C07383490F3BEBA14E2DC1538AAB5CE08FD035626771`, had a
  clean fatal-marker search, and ended with audit counters
  `fastpath=722256/2008221 fallback=0/0`. This is route-clean correctness
  evidence, not a speed win: audit counters and block body-time were enabled.
  Keep the cvar default-off in presets. Next useful step is a quiet same-APK
  A/B or control sandwich with audit off; if that is inconclusive again, move
  to `MUL_ADD_V128` cost for `82282568/8228256C/82282570`.
- Current all-three `stvewx` quiet A/B:
  `docs/research/20260524-071045-blue-dragon-all-three-stvewx-quiet-ab.md`.
  Control `scratch/thor-debug/20260524-070253-*` and fastpath-on
  `scratch/thor-debug/20260524-070613-*` used the same commit `a924aba56` and
  APK SHA `8A3DA22B2208AA67DE13C07383490F3BEBA14E2DC1538AAB5CE08FD035626771`
  with audit, body-time, block body-time, and disassembly all off. Both reached
  visually matching opening sky/dragon-wing frames with clean fatal-marker
  searches. The fastpath shrank `82282490` code size from `86828` to `86708`,
  but final `82282490` total entries stayed identical at `183498`, final
  interval deltas were close (`54908` control vs. `51940` fastpath-on), and
  final Main Thread stayed around one full core. Treat this as no proven speed
  win. Keep `arm64_blue_dragon_stvewx_stack_lane_fastpath` default-off and stop
  spending new sprint slices on narrow `stvewx` lane folds unless a broader
  state/vector experiment reopens them. Next lane: `MUL_ADD_V128` source/cost
  audit for `82282568/8228256C/82282570`.
- Current `MUL_ADD_V128` source/cost audit:
  `docs/research/20260524-072228-mul-add-v128-source-cost-audit.md`.
  `tools/thor/thor_hir_a64_codegen_audit.ps1` now prints a
  `MUL_ADD_V128 Source-Cost Audit` section with exact `vmaddfp` PCs, source
  anchors, lowering shape, slow-path hazards, and semantics gates. Both
  `822824F0-82282574` and `8228252C-822825C4` hit the same three PCs:
  `82282568`, `8228256C`, and `82282570`. The larger target remains
  `8228252C-822825C4` (`approx_exclusive=2876500`,
  `mul_add_v128=3`, `approx_exclusive_ticks_per_estimated_instr=14981.77`).
  Do not patch `MUL_ADD_V128` behavior yet: the A64 lowering includes VMX FPCR
  mode handling, optional denormal flushing, scratch source saves, `fmla`, PPC
  NaN fast-path/repair, optional output flushing, and a destination copy. Also
  remember x64 intentionally avoids host FMA for `MUL_ADD_V128` because the
  fused path differed from `vmulps` plus `vaddps` tests. The next lane is a
  default-off, function/span/PC-gated runtime audit for
  `82282568/8228256C/82282570` that counts denormal flush need, NaN-fixup
  entry/per-lane repair, FPCR mode switches, and source/dest alias copies
  before any shortcut.
- Current `MUL_ADD_V128` runtime audit patch:
  `docs/research/20260524-074442-blue-dragon-mul-add-v128-runtime-audit.md`.
  The backend now has default-off cvar
  `arm64_blue_dragon_mul_add_v128_audit`, gated to Blue Dragon guest function
  `82282490` and PCs `82282568`, `8228256C`, and `82282570`. It logs total and
  per-PC calls, FPCR switches, software flush-path eligibility, input/output
  denormal sightings, NaN-fixup entry and repaired lanes, plus source/dest
  vector copies. `tools/thor/thor_xenia_debug.ps1` exposes
  `-Arm64BlueDragonMulAddV128Audit`; presets keep it default-off. `NativeCore`
  passed. Next step is `FullDeploy` plus a delayed `82282490` body-time
  speed capture with the audit enabled; do not patch a fastpath until those
  counters prove the expensive paths are cold.
- Clean route rebaseline:
  `docs/research/20260521-183001-clean-route-rebaseline.md`.
  After reverting the broad lane-replace probe and redeploying clean `master`,
  `scratch/thor-debug/20260521-182630-*` recovered the opening-scene route.
  HEAD `5aaf0d776`, APK SHA
  `FE1CA12C2B572C5643775702DF68D3DEA4A45661B2A2DCD7A8694EAF45F26312`.
  `82282490` stayed at `code_size=87168`; final sample had Main Thread about
  `92.3%`, XMA Decoder about `46.1%`, and GPU Commands about `11.5%`.

## Current Porting Priorities

1. Establish reproducible Android build generation.
   - Initialize submodules before build work:
     `git submodule update --init --recursive`.
   - Generate Android makefiles with:
     `python xenia-build setup --target_os=android` or `python xenia-build premake --target_os=android`.
   - Build from `android/android_studio_project` with Gradle after generated makefiles exist.
   - If the repo path contains spaces, run Gradle through a no-space `subst` drive because `ndk-build` may reject `APP_BUILD_SCRIPT` paths with spaces.
   - Install and smoke-test on Thor with:
     `powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode Install`
     and then `-Mode LaunchWindowDemo` or `-Mode LaunchLauncher`.

2. Make Android ARM64 build failures explicit.
   - Keep x64 backend code behind `XE_ARCH_AMD64` and architecture filters.
   - Keep the imported A64 backend honest: it now compiles as an Android native
     backend, but it is still non-playable research until device logs prove a
     retail game reaches title.
   - Do not fake guest execution by silently falling back to `NullBackend` for the emulator path.

3. Bring up the native Android emulator app.
   - Keep `xenia_main.cc` wired into the Android shared library.
   - Maintain the document/file selection flow for `.xex`, `.iso`, and direct developer ADB target extras.
   - Keep the existing Vulkan trace viewer and window demo working as smoke tests.

4. Build the AArch64 CPU backend.
   - Start from the shared backend interfaces in `src/xenia/cpu/backend`.
   - Mirror x64 backend structure only where it maps cleanly.
   - Decide on an AArch64 code emitter library through a dated research note before vendoring anything.
   - Validate instruction cache flushing, executable memory policy, signal/exception handling, and breakpoint behavior on Android.
   - Treat the target design as PPC/HIR to AArch64 DBT with code cache, direct
     block chaining, fast indirect branch lookup, pinned guest context and
     memory base, endian-aware memory lowering, and VMX128-to-NEON lowering.
   - Measure helper fallbacks, guest-to-host transitions, thunk entries,
     code-cache lookups, direct links, indirect branch misses, recompiles, and
     top guest PCs before guessing at hot fixes.
   - Do not judge speed with broad debug tracing, disassembly dumps, shader
     dumps, or compiled-call tracing enabled.
   - Do not treat VMX128 as "just NEON"; lane order, saturation, NaNs,
     denormals, and Xenos packed formats are correctness traps.

5. Optimize for Thor Max early.
   - Treat Adreno 740 / Vulkan driver behavior as a real target, not an afterthought.
   - Track CPU, GPU, thermal, fan, battery, and frame pacing observations in worklogs.
   - Preserve correctness before speed; every speed experiment needs a comparable baseline.
   - Keep trace-heavy correctness runs and trace-off speed runs separate. Mixing
     them gives bad answers.

## Translation Strategy From 2026-05-19 Report

- Highest-value current order:
  1. Keep the quiet Blue Dragon speed lane canonical, with strict metadata.
  2. Add A64 block dispatch, helper fallback, transition, and code-cache
     profiling that is cheap enough to leave on in speed captures.
  3. Keep XMA/audio shortcuts as explicit bring-up probes, not correctness
     fixes.
  4. Narrow the signed `2_10_10_10` / Adreno format fallback work only where
     Blue Dragon evidence points.
  5. Add real Android input mapping and repeatable menu/opening scripts.
- CPU work should improve the DBT spine before title-specific hacks:
  direct block chaining, indirect branch lookup, endian load/store fusion,
  common branch/condition lowering, VMX128 inline lowering, and reduced
  guest-to-host transitions.
- GPU work should be command-stream, shader-microcode, EDRAM/resolve, and
  format-specific. Do not rely on optional Vulkan formats without probing
  Adreno support, and do not treat EDRAM as normal host images.
- Mature Vulkan port work should follow
  `docs/research/20260524-135929-mature-vulkan-port-design.md`:
  device profile, console GPU milestone classification, deterministic
  trace/replay, shader/pipeline cache discipline, EDRAM/resolve correctness,
  explicit sync/frame-pacing attribution, and separate validation/RenderDoc/AGI
  lanes. Do not start broad async pipeline compilation, persistent cache
  prewarming, tile-memory tricks, or Adreno workarounds until route evidence
  proves the GPU path is the wall and the correctness risk is bounded.
- System work should keep kernel/XAM/thread/timer/storage/input/audio HLE
  visible and measurable because Blue Dragon can be CPU-ready while blocked by
  device-service behavior.
- Hot trace stitching, background optimization, pipeline-cache tuning, AGI, and
  RenderDoc are useful later, but the report ranks them below CPU/A64 and audio
  cost until the speed lane shows those are no longer the dominant wall.

## Debug Automation Rules

- Before a risky cvar, backend shortcut, GPU hack, timing tweak, or
  title-specific probe, run the repo experiment gate:
  `.agents/skills/xenia-thor-experiment-gate/SKILL.md`.
- Keep APK shell rebuilds and native core rebuilds separate:
  - UI/manifest/resource changes: `thor_build.ps1 -Mode ApkShell`.
  - C++/Vulkan/ARM64 backend changes: `thor_build.ps1 -Mode NativeCore`.
  - Release-to-device validation: `thor_build.ps1 -Mode FullDeploy`.
- Prefer scripted Thor debug loops over manual clicking once a path is known:
  `FindContent`, `LaunchBlueDragon`, `LaunchBlueDragonLiveCapture`,
  `LaunchBlueDragonSpeedCapture`, `LaunchEmulator`, `LaunchWindowDemo`, and
  `Capture`.
- Always clear logcat before a launch and capture a full log, filtered log, screenshot, metadata file, APK hash, branch, commit, process id, focused activity, and target path.
- For every speed note, record build hash, APK hash, cvars, run duration,
  thermal/fan/battery if available, screenshot path, process id, thread sample,
  and whether the run was trace-off or trace-heavy.
- Do not enable broad shader dumps, checksums, live logcat, broad GPU packet
  traces, disassembly dumps, or compiled-call tracing in the speed lane unless
  the research note explicitly says the speed result is invalid for comparison.
- Legacy ARM64 mini-JIT flags (`-Arm64MiniJit`, `-Arm64MiniJitBlacklist`,
  `-Arm64ForceInterpreterRanges`, and `-Arm64GuestStoreWatch`) belong to the
  removed scaffold path. Do not rely on them for current aX360e A64 backend
  runs.
- For current A64 backend bring-up, prefer:
  - `-MmapAddressHigh 8` for the aX360e-style fixed high memory/code-cache
    layout on Thor.
  - `a64_max_stackpoints` and
    `a64_enable_host_guest_stack_synchronization` only when debugging the
    imported A64 stackpoint path.
- For focused guest-code and wait debugging, use:
  - `-DisassembleFunctionFilter "8246DBB0,8246B408"` with
    `-DisassembleFunctions true` to dump only matching guest functions.
  - `-Arm64CompiledCallTraceFunctions "8246B408"` to sample a hot compiled
    guest function.
  - `-Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 16`
    to enable the low-noise A64 speed profiler in a speed capture. This emits
    periodic summaries for function-entry deltas, direct/indirect guest calls,
    guest-to-host transitions, runtime resolves, and top guest PCs. It is
    default-off and preferred before noisy compiled-call tracing.
  - `-Arm64SpeedProfileBodyTimeFilter "8272A3A4,8272A8E8,826C5620,827294CC,826BF770"`
    with the speed profiler to measure filtered generated-code body time using
    CNTVCT. Use this when entry counts are misleading after helper fastpaths.
    Body-time rows are sorted by elapsed guest body ticks, not call count.
  - `-Arm64SpeedProfileBlockFilter "8272A3A4"` with the speed profiler to add
    per-HIR-block entry counters inside a selected guest function. The block
    filter is exact-start scoped for single addresses. This is trace-heavy and
    useful for short OODA captures only; do not compare its speed to a clean
    speed lane. First `8272A3A4` results put hot blocks at `8272A8B4`,
    `8272AA50`, `8272A3F4`, `8272A474`, `8272A548`, and `8272A424`.
  - `-A64InlinePpcThreadFieldLeafHelpers false` to roll back the A64 inline
    for tiny PPC leaf helpers matching `lwz r11,D(r13); lwz r3,D(r11); blr`.
    Blue Dragon's hot draw-wait route calls one of these helpers at
    `8248B040`; keep the inline pattern-based rather than address-only.
  - `-A64InlineKernelHighFrequencyExports false` to roll back the A64 inline
    for selected high-frequency Xbox kernel exports. Current safe set is
    `KeRaiseIrqlToDpcLevel`, recursive/uncontended `RtlEnterCriticalSection`,
    `RtlTryEnterCriticalSection`, recursive `RtlLeaveCriticalSection`, and
    uncontended final `RtlLeaveCriticalSection` unlock with host fallback where
    needed.
  - `-A64InlineKernelSpinlockExports false` rolls back the raised-IRQL spinlock
    export inlines. They are default-on after
    `scratch/thor-debug/20260520-193232-*`; keep `a64_inline_kf_lower_irql`
    default-off because the naive IRQL-store inline skipped native APC delivery.
  - `-A64InlineRtlLeaveFinalUnlock false` rolls back the uncontended final
    `RtlLeaveCriticalSection` unlock inline. The off path should report
    `RtlLeaveCriticalSection code_size=448`; the on path reports `528`.
  - `-A64RtlLeaveFastpathAudit true` enables default-off RtlLeave path counters
    in the A64 speed profile. This is diagnostic and raises
    `RtlLeaveCriticalSection code_size` to about `640`.
  - `-A64RtlEnterFreeFirst true` is an unsafe/default-off experiment. Do not
    use it for default captures; it black-idled Blue Dragon in
    `scratch/thor-debug/20260520-223025-*`.
  - `-Arm64BlueDragonDrawWaitProbe true` to log the current Blue Dragon draw
    wait state.
  - `-XboxkrnlThreadWaitTrace true` and `-XboxkrnlEventTrace true` for kernel
    wait/event proof.
  - `-ClearMemoryPageState true` for Canary-style GPU shared-memory page-state
    experiments.
  - `-LogLevel 0` for speed captures where only Xenia errors should hit
    logcat; restore higher logging only for focused trace runs.
  - `-XmaTraceContextState true`, `-GpuTraceTextureCacheActions true`, and
    `-VulkanTracePipelineCreation true` only when those noisy subsystems are
    the target of the run.
  - `-XmaFastSilence true` is a bring-up-only audio shortcut. It may help
    compare XMA cost, but it is not an audio correctness path or compatibility
    fix.
  - `-GpuInterruptOnRingIdle true` and `-GpuBlueDragonKickWaitToken true` only
    for clearly documented Blue Dragon research runs; both are default-off
    hacks, not compatibility fixes.
  - `-GpuTraceInterrupts true` to trace `VdSetGraphicsInterruptCallback`,
    `PM4_INTERRUPT`, VBlank, source-1 dispatch begin/end, and Blue Dragon token
    memory without forcing a compatibility behavior.
  - `-GpuInterruptOnSwap true` only as a narrow research experiment that
    dispatches source-1 after `PM4_XE_SWAP`; it is safer to test than the broad
    ring-idle interrupt, but still not a compatibility fix.
  - `-TimeScalar 0.25` or similar only as a temporary watchdog/timing probe; it
    slows guest time so the Thor can reveal whether Blue Dragon is failing due
    to slow first-frame GPU/CPU progress rather than a missing completion event.
  - `-GpuTraceSwap true` to trace guest video setup, runtime `VdSwap`, PM4
    `XE_SWAP`, and Vulkan `IssueSwap` proof. Import/symbol lines mentioning
    `VdSwap` are not runtime call proof; use the explicit `GPU swap trace:`
    lines for runtime evidence.
  - `-VulkanPresentForcedResolveOnSwap true` plus
    `-VulkanPresentForcedResolveAddress`, width, height, pitch, and format to
    force-present a specific resolve source. This is a research-only probe for
    Blue Dragon black/blue frames, not a compatibility fix.
- `-VulkanPresentScoredResolveOnSwap true` to use slow resolve readback stats
  for picking a non-clear present candidate. Pair with `-TimeScalar 0.5` and
  keep `-VulkanPresentScoredResolveBudget` small enough for a Thor run.
  Candidate `size` is visible surface size and `pitch` is memory pitch; Blue
  Dragon has an 80x45 resolve with pitch 96, so do not collapse those fields.
- For Blue Dragon black-screen runs, first prove the split:
  `-GpuTraceSwapFrontbufferChecksum true -VulkanTraceCopyState true
  -VulkanTraceResolveChecksum true`. If `1CA1C000` / `1CDB4000` frontbuffers
  are zero while `IssueCopy` emits nonzero resolves, focus on resolve content,
  format, and render-target causality rather than Android presenter bring-up.
- To inspect final fullscreen vertex data without burning the trace budget on
  setup draws, use:
  `-VulkanTraceVertexFetchChecksum true -VulkanTraceVertexFetchShaderFilter 5CF1EE2E3C82546B -VulkanTraceVertexFetchChecksumBudget 24`.
  This is a slow research-only trace. The 2026-05-19 Blue Dragon run ruled out
  bad final-blit vertex position, UV, or color data as the immediate black
  frame cause.
- For upstream Blue Dragon render-chain probes, use shader-hash filters so
  draw-state, constants, and texture-source budgets land on the suspected
  passes:
  `-VulkanTraceDrawShaderFilter`, `-VulkanTraceShaderConstantsShaderFilter`,
  and `-VulkanTraceTextureSourceShaderFilter`. Current useful pixel hashes are
  `05775DE8A2B0B3F5`, `0ABADD9DA4373CBA`, `2A0674C564A8A8C5`,
  `57B736C8B5D4E953`, `B02CC5F55AD0D140`, and `9567C79307ACC6F5`.
- Known diagnostic forced-present probe: `1D88F000+00385000`, `720x720`, pitch
  `1280`, format `6` can turn the surface white on Thor. It is proof the
  presenter can show resolve memory, not proof the game reached title.
- Use `StopNoise` before game runs if another emulator or graphics app is stealing focus or polluting logcat.
- Use the default Blue Dragon path only for the user's local Thor SD card. Do not assume other machines or devices have the same mount UUID.
- Keep Blue Dragon attempts honest: until ARM64 JIT exists, guest code may execute slowly in the interpreter scaffold, but the expected result is still not a playable game.
- For native Android crashes, analyze the unstripped `arm64-v8a/libxenia-app.so` from `android/android_studio_project/app/build/intermediates/ndkBuild/githubDebug/obj/local/arm64-v8a/`.
- For guest-code static analysis, use Ghidra only on legally owned and locally extracted/decrypted code. Do not commit game binaries, extracted XEX files, ISO contents, keys, or private screenshots.
- Keep Ghidra projects, extracted guest code, generated JIT blobs, and temporary
  analysis products under ignored `scratch\ghidra\`:
  `native`, `guest`, `generated-jit`, `reports`, and `scripts`.
- Ghidra tracks:
  - Native ARM64: `libxenia-app.so`, JNI bridge, Vulkan path, ARM64 backend, crashes, and symbolized stack traces.
  - Guest PPC: Xbox 360/Xenon PowerPC disassembly, XEX metadata, entry points, import thunks, and functions implicated by Xenia logs.
- Use Ghidra as an OODA accelerator, not as a whole-disc guessing machine:
  runtime capture -> address queue -> smallest Ghidra import -> inferred
  function/import/global/timer purpose -> one next experiment.
- Create or update repo-local Codex skills when a debug loop repeats. Validate
  skill folders with `quick_validate.py` when available; otherwise do a basic
  frontmatter/readability check and keep skill bodies concise.
- Current repo-only skills live under `.agents/skills/` and should be read when
  their topic is active:
  - `.agents/skills/xenia-thor-war-room/SKILL.md` is the default coordinator
    for broad Blue Dragon/Thor bring-up, unclear crashes, black screens, and
    multi-subsystem triage.
  - `.agents/skills/xbox360-xenon-ppc-architect/SKILL.md` is the guest expert
    for Xenon PowerPC, VMX128, XEX imports, kernel shims, D3D watchdogs, and
    Ghidra guest PPC analysis.
  - `.agents/skills/aarch64-snapdragon-jit-port/SKILL.md` is the host CPU
    expert for x64-to-AArch64 parity, aX360e/xenia-edge donor adaptation,
    Android code cache, thunks, signal recovery, and Snapdragon constraints.
  - `.agents/skills/xenia-vulkan-adreno-renderdoc/SKILL.md` is the GPU expert
    for Xenos PM4, ring pointers, writebacks, `VdSwap`, `PM4_XE_SWAP`, Vulkan
    `IssueSwap`, Adreno 740, RenderDoc, and black-frame debugging.
  - `.agents/skills/xenia-a64-speed-hotpath/SKILL.md` is the speed expert for
    Thor ARM64 2-FPS runs, A64 speed-profile counters, hot guest PCs,
    helper/prologue churn, guest-to-host transitions, direct/indirect call
    overhead, XMA/audio cost, and Snapdragon profiling.
  - `.agents/skills/xenia-thor-experiment-gate/SKILL.md` is the one-variable
    gate for risky ARM64, GPU, audio, input, timing, debug-prop, or
    title-specific experiments.
  - `.agents/skills/xenia-blue-dragon-route-capture/SKILL.md` is the route and
    proof expert for loading Blue Dragon Disc 1, using nop HID START/A
    sequences, labeling title/opening progress, and capturing screenshots/logs.
  - `.agents/skills/xenia-ghidra-ooda-loop/SKILL.md` is the repo-local static
    analysis coordinator for mapping Thor runtime evidence to Ghidra/native
    ARM64/guest PPC/generated-JIT queues and dated triage reports.
  - `.agents/skills/xenia-codex-goal-loop/SKILL.md` is the bounded
    continuation expert for Codex Stop hooks, heartbeat automations, proof
    markers, and "keep going until goal or blocker" loops.
  - `.agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md` is the
    active autopilot contract for continuing this conversation through Stop
    hooks and heartbeats until Blue Dragon reaches full-speed proof on AYN
    Thor or a concrete blocker is recorded.
  - `.agents/skills/xenia-working-fast-autonomy/SKILL.md` is the concrete
    worker loop under the autopilot. It makes each continuation pick one lane
    and leave a code patch, Thor capture, deterministic analysis artifact, or
    blocker with the exact next experiment. As of 2026-05-22 it also carries
    the current `82282490` local-slot/data-flow GPR promotion target so the
    next continuation starts at `r[1]`/`r[11]` instead of repeating the failed
    emit-time context-cache lane.

## Agent Skill Donor Audit

- On 2026-05-19, the Thor agent-skill layout was compared against:
  - `https://github.com/noeldvictor/Vita3K-Thor/tree/master/.agents/skills`
    at commit `4059339e7434f9f3866696ebc14ac024febdb4b8`.
  - `https://github.com/noeldvictor/rpcsx-ui-android-thor/tree/master/.agents/skills`
    at commit `71e6a963fed5267cc263215521462420cfefba00`.
- Useful ideas adapted for xenia-thor:
  - experiment gate before risky probes;
  - durable worklog/research ledger instead of chat memory;
  - Android/Thor route and screenshot proof;
  - hotpath profiler lane tied to runtime evidence;
  - ADB safety and capture metadata discipline.
- Do not blindly copy donor skill text or scripts. Synthesize Xenia-specific
  workflows, preserve source attribution in dated research notes, and keep
  Xenia's priority different: AYN Thor ARM64 first, Windows only as a control.

## RenderDoc Rules

- RenderDoc is for Vulkan frame capture only. It is not proof that guest CPU execution is correct.
- Enable RenderDoc layers only for capture runs:
  `powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode EnablePerApp`.
- Disable layers immediately after capture:
  `powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode Disable`.
- Check the actual installed RenderDoc layer package with:
  `adb shell pm list packages | grep -i renderdoc`.
- Do not upstream RenderDoc findings from this fork unless reproduced on official Xenia.

## Android GUI And OSD Direction

- Treat the Android app as a real handheld emulator shell, closer to Dolphin/Azahar than a desktop debug window.
- Keep ImGui for internal dev demos, trace tooling, and diagnostics only. Do not make ImGui the main Android user interface.
- Build user-facing Android UI with native Android activities, XML resources, adaptive icons, drawables, and Java/Kotlin glue unless a stronger reason appears.
- Keep the launcher dark, controller-friendly, and dense enough for repeated emulator use: clear game launch, trace viewer, Vulkan demo, and future settings/library entries.
- On AYN Thor, the default game picker location should be the SD-card Xbox 360 folder using DocumentsUI initial URI document id `2664-21DE:roms/xbox360`.
- Maintain a Xenia Thor Android identity:
  - Adaptive launcher icon with a recognizable Xenia/Android/Thor mark.
  - `Xenia Thor` app surface naming for Android bring-up builds.
  - README warning that the fork is extremely unstable.
- The emulator activity should have a Dolphin-like OSD layer above the render surface:
  - Native overlay panels for target title, backend/runtime status, warnings, and future FPS/frame pacing.
  - No dependency on ImGui for normal in-game overlays.
  - Native OSD must be safe over Vulkan surfaces and easy to hide or update from native code later.
- Future OSD data should come from native runtime events rather than static Java text: FPS, frame time, backend, title ID, shader compilation, thermal/performance warnings, and fatal setup errors.
- Keep layout responsive for the Thor top screen in landscape first; avoid text overlap and verify screenshots on device after UI changes.

## Current Blue Dragon / ARM64 State

- Latest validated Thor captures:
  `scratch\thor-debug\20260519-141908-*` reached the Blue Dragon title prompt
  with Android OSD hidden, `vulkan_debug_texture_fetch_disable_exp_adjust=false`,
  commit `d9576ec4c`, and APK SHA-256
  `09F480292F913D6132F3A288C7FCAB758E02FF6BAD519B1DF089BB3EF40B1224`.
- The previous `0x826A23E8` Blue Dragon null-thunk crash was traced to
  `Sound::SOUNDBANK::Load XACTCreateSoundBank()` while Android was running
  with `apu=nop`.
- `NopAudioSystem` now creates a silent driver instead of returning
  `X_STATUS_NOT_IMPLEMENTED`, allowing the guest XACT/sound-bank path to
  initialize far enough to write `0x82785548`.
- Post-fix evidence: the store watch hit
  `826A2550@826A2598->82785548`, Blue Dragon created draw and sound threads,
  and no guest crash PC appeared in the 10:09, 10:11, 11:08, or 11:13
  captures.
- The former ARM64 mini-JIT coverage work was useful for finding missing HIR
  surfaces, but it has been superseded by the donor A64 backend import.
- As of `docs/research/20260518-164150-ax360e-a64-hard-swap.md`, the active
  ARM64 backend is the copied aX360e/Edge A64 backend with local compatibility
  shims for logging, HIR helpers/opcodes, cvars, Capstone naming, POSIX unwind
  placement, and disabled info-cache MMIO recording.
- Native core validation for that hard swap passed on 2026-05-18 for both
  Android `arm64-v8a` and `x86_64`.
- The KTHREAD timer wall at guest function `8246B408` has been identified and
  moved: `arm64_update_kthread_time` writes guest uptime to the current
  KTHREAD `+0x58`, and the draw wait `global_tick` now advances.
- The command processor now mirrors guest-visible ring pointers into
  `CP_RB_RPTR` / `CP_RB_WPTR` (`0x01C4` / `0x01C5`). Blue Dragon's D3D dump now
  shows matching drained ring pointers instead of a stale zero read pointer.
- Current milestone: Blue Dragon reaches the `press START` title prompt on Thor
  with `vulkan_debug_texture_fetch_disable_exp_adjust=false` after the SPIR-V
  texture fetch translator was fixed to read result exponent adjustment from
  fetch constant dword 3, not dword 4. `vulkan_force_signed_2101010_unorm_fallback=true`
  is still part of the validated run. This is title-screen progress, not
  compatibility.
- Current milestone extension: sequenced nop HID pulses can press START, select
  English on the language screen, and reach Blue Dragon's opening scene in
  `scratch\thor-debug\20260519-144529-*`.
- Current blocker: real Android controller mapping, longer scripted input, and
  performance. The latest thread sample shows the main guest CPU thread and XMA
  decoder as the top consumers, with GPU command work below them. Treat the
  current speed wall as CPU/JIT/audio/debug-overhead first, GPU second.
- Current speed discriminator:
  `docs/research/20260519-230000-a64-body-time-profiler.md`.
  The validated Thor capture `scratch\thor-debug\20260519-225321-*` reached the
  Blue Dragon loading spinner with APK SHA-256
  `3951852CB716C00DB006ED96216BFBA056070BF679FF8733286E43DFED17F6FC` and no
  searched fatal markers. Filtered body-time rows show `8272A3A4` as the
  largest measured guest-body cost in the loading route; `827294CC` and
  `826C5620` remain high-frequency but relatively cheap after their current
  fastpaths. Prefer body-time or simpleperf evidence before adding more
  entry-count-driven micro-fastpaths.
- Current kernel-transition speed note:
  `docs/research/20260520-134854-a64-kernel-extern-fastpaths.md`.
  The stable capture `scratch\thor-debug\20260520-134646-*` keeps Blue Dragon
  guest execution moving through 75 seconds and cuts speed-profile
  guest-to-host deltas versus the IRQL-only baseline by inlining selected
  high-frequency kernel exports. The guarded spinlock inline attempt
  `scratch\thor-debug\20260520-134410-*` black-idled and was removed.
- Focused PPC dumps show the graphics interrupt callback at `8246DBB0` and draw
  wait function `8246B408`; token-kick experiments prove token movement alone
  does not satisfy the game.
- Runtime swap tracing now shows real PM4 packets, `DRAW_INDX_2`, texture
  creation/loading, runtime `GPU swap trace: VdSwap` calls, `PM4_XE_SWAP`, and
  Vulkan `IssueSwap`. `VdSwap` in import/symbol listings is not runtime call
  proof; only explicit `GPU swap trace:` runtime lines count.
- Forced-presenting a known format-6 resolve can visibly change the Android
  surface, so the presenter is not the current wall. Next probes should link
  render target state, resolve output, candidate format, and official
  frontbuffer clearing/population.
- The visible OSD badge now reports `aX360e A64 backend research`.
- Strategy as of 2026-05-18 14:12 EDT: stop using Thor as the only unit test.
  Run a broad x64-to-ARM64 conversion pass first, then use Thor/Blue Dragon as
  milestone evidence.

## ARM64 Conversion Audit Loop

- Do not interpret "all HIR opcodes have a case label" as "the backend is
  ported." It only proves switch coverage.
- Before more game-specific fixes, batch the x64-to-ARM64 conversion work:
  host-to-guest and guest-to-host thunks, resolve thunk, breakpoint support,
  exception/signal recovery, code-cache indirection and metadata, source maps,
  unwind info where available, native scalar/control/memory lowering, native
  vector/pack/unpack lowering, and differential tests.
- After the 2026-05-19 translation report, broad ARM64 passes should include
  cheap runtime counters before more one-off Thor fixes: guest function/block
  entries, helper calls by opcode/category, guest-to-host transitions,
  code-cache lookup/link/miss/recompile stats, and top Blue Dragon guest PCs.
- Use Thor runs as milestone checkpoints after broad backend batches, not as
  the only unit test.
- Current conversion audit:
  `docs/research/20260518-140436-arm64-x64-conversion-audit.md`.

## ARM64 Fork Audit Decision

- Latest audit: `docs/research/20260517-183520-xenia-arm64-fork-audit.md`.
- Current working-fork reality check:
  `docs/research/20260518-142302-arm64-xenia-working-forks.md`.
- Some ARM64 Xenia-derived projects appear to work in limited senses:
  `aenu1/ax360e` is the strongest Android ARM64/Vulkan evidence, and
  XeniOS/xenia-mac are Apple ARM64 evidence. None is a proven drop-in path for
  Blue Dragon on Thor.
- The helper-backed ARM64 mini-JIT scaffold has been removed from the active
  backend. Use the donor A64 backend as the only forward ARM64 path unless a
  future comparison explicitly needs old-history archaeology.
- Prioritize correctness coverage and differential tests first; add native
  hot-path lowering where CPU slowness alone could cause watchdogs.
- Primary AArch64 JIT source to study/port: `has207/xenia-edge`, branch `edge`, because it has the most current xbyak_aarch64 A64 backend shape, CMake wiring, POSIX code cache, and broad HIR opcode table coverage.
- Primary Android app-side reference: `aenu1/ax360e`, branch `main`, because it shows a real Android package with nested Xenia Canary, A64 backend, Android CMake glue, SAF/document-file access, Android HID, Adreno options, and `libadrenotools`.
- AArch64 assembler dependency: `third_party/xbyak_aarch64` is pinned to Fujitsu `xbyak_aarch64` commit `59c6a7e7368cd2dbb07c46abe83239ace4d58849`, matching the `has207/xenia-edge` submodule gitlink observed during the audit. License: Apache-2.0.
- Secondary provenance and portability references: `wmarti/xenia-mac`, `Wunkolo/xenia`, and `joetheshmoe/xenia-winarm`.
- Treat `edilsongg/xenia-arm64` and `fillips67fissile/xenia-arm64-backend` as Wunkolo duplicates unless a future diff proves unique work.
- Do not import from `SbossTreeSitty`, `Xenia-creator2025/Xenia-emulator-android`, or the `ProyectoXenia` repos for JIT work.
- Before copying any third-party code, verify the specific file license, preserve attribution, and keep this fork clearly marked as unofficial research.
- First import slice: `docs/research/20260518-143943-arm64-upstream-import-slice.md`.
  - Adapted Xenia Edge A64 `BRK #0` breakpoint / illegal-instruction handler
    behavior into the current `arm64` backend.
  - Adapted aX360e Android build hardening: frame pointers, unwind tables,
    16 KiB max page-size linker option, and disabled native heap pointer
    tagging.
  - Post-import Blue Dragon regression capture `scratch/thor-debug/20260518-144723-*`
    shows no native crash, but still hits the frame-0 D3D watchdog with drained
    ring pointers. Do not claim game progress from this slice.
  - Next backend parity gaps are Edge-style host-to-guest/guest-to-host/resolve
    thunks, code-cache indirection/commit metadata, and data/unwind placement.
  - Do not wholesale-copy aX360e SAF/audio/HID/AdrenoTools or the full Edge
    emitter until each batch has a clear build boundary and attribution note.
- Second Edge import slice:
  `docs/research/20260518-145131-arm64-code-cache-edge-brk-fill.md`.
  - The ARM64 code cache fills unwritten generated-code memory with `BRK #0`,
    matching Xenia Edge A64 debug guard behavior.
  - This is debug hardening only; it is not the full Edge code-cache,
    indirection, unwind, or thunk import.
  - Thor validation capture `scratch/thor-debug/20260518-145425-*` on commit
    `9be7323b8` shows no new native crash and the same Blue Dragon D3D watchdog.
- Third Edge-shaped backend slice:
  `docs/research/20260518-151702-arm64-thunk-cache-scaffold.md`.
  - ARM64 now has x64/Edge-shaped thunk slots, a resolve helper, sparse encoded
    indirection table metadata, `CommitExecutableRange`, `AddIndirection64`,
    `PlaceData`, unwind placeholder metadata, and generated-code commit marks.
  - The conversion audit's backend/code-cache capability-name gaps are closed,
    but this was later superseded by the hard donor A64 backend swap.
  - Thor validation capture `scratch/thor-debug/20260518-152107-*` shows the
    indirection table allocated on device with no new native crash, and Blue
    Dragon still stops at the known D3D frame-0 watchdog. Do not claim title
    progress from this slice.
- Fourth Edge-shaped backend slice:
  `docs/research/20260518-152759-arm64-generated-transition-thunks.md`.
  - ARM64 now generates host-to-guest, guest-to-host, and resolve thunks with
    `xbyak_aarch64`, places them in the ARM64 code cache, sets the default
    indirection target to the resolve thunk, commits the force-return range,
    and logs generated thunk addresses at startup.
  - This is not yet the full Edge A64 ABI. Backend context, stackpoint/vector
    save details, unwind data, exception recovery, and native emitter call
    routing are still incomplete.
  - Thor validation capture `scratch/thor-debug/20260518-152749-*` shows the
    generated thunks on device with no new native crash, and Blue Dragon still
    stops at the known D3D frame-0 watchdog. Do not claim title progress from
    this slice.
- First concrete aX360e memory-layout import:
  `docs/research/20260518-154350-ax360e-fixed-high-memory-layout.md`.
  - Added opt-in `mmap_address_high`, default `0`, to try aX360e-style fixed
    guest memory at `mmap_address_high << 32`.
  - When set, ARM64 code cache tries the matching high window:
    indirection at `((mmap_address_high + 2) << 32) | 0x80000000` and code at
    `((mmap_address_high + 2) << 32) | 0xA0000000`.
  - Use `tools/thor/thor_xenia_debug.ps1 -MmapAddressHigh 8` for Thor
    experiments. Validation capture `scratch/thor-debug/20260518-154338-*`
    proves fixed guest memory, fixed code cache, and fixed thunk addresses on
    Thor with no new native crash.
  - Blue Dragon still stops at the known D3D frame-0 watchdog; do not claim
    title progress from this slice.
- Second concrete aX360e/Edge runtime import:
  `docs/research/20260518-155736-ax360e-guest-trampolines.md`.
  - Added aX360e `BitMap::AcquireFromBack`, backend context lifecycle hooks,
    `GuestTrampolineGroup`, `CreateGuestTrampoline`, and `FreeGuestTrampoline`.
  - Thread contexts now reserve a 256-byte backend prefix before `PPCContext`,
    matching the A64 backend family assumption.
  - ARM64 now allocates an executable guest trampoline host pool, emits the
    aX360e/Edge `movz/movk` + `br x9` trampoline sequence, flushes instruction
    cache, and publishes 64-bit trampoline targets through the ARM64
    indirection table.
  - Validation capture `scratch/thor-debug/20260518-155837-*` proves the
    trampoline pool on Thor:
    `trampolines=000000775BD04000-000000775BF24000`, with no new native crash.
  - This slice does not wire kernel/runtime callback paths to backend-created
    trampolines yet. Blue Dragon still stops at the known D3D frame-0 watchdog;
    do not claim title progress from this slice.
- Third concrete aX360e/Edge runtime import:
  `docs/research/20260518-160807-ax360e-arm64-backend-context.md`.
  - Added `Arm64ReserveHelper`, `Arm64BackendStackpoint`,
    `Arm64BackendContext`, `Clock::GetGuestTickCountPointer`, and ARM64
    backend context lifecycle methods.
  - `Arm64Backend::SetGuestRoundingMode` now caches FPCR state and writes host
    FPCR only on ARM64 builds, while still updating PPC `fpscr` state.
  - Validation captures `scratch/thor-debug/20260518-160908-*` and
    `scratch/thor-debug/20260518-160947-*` show no new native crash and the
    same Blue Dragon D3D frame-0 watchdog.
  - Do not enable full stackpoint synchronization or switch thunks to `x19`
    backend context until the real A64 function prolog/epilog path lands.
- Hard aX360e A64 backend swap:
  `docs/research/20260518-164150-ax360e-a64-hard-swap.md`.
  - Removed the active helper-backed `arm64` mini-JIT implementation files.
  - Imported the donor A64 backend to `src/xenia/cpu/backend/a64` and kept
    `src/xenia/cpu/backend/arm64/arm64_backend.h` as a wrapper alias.
  - Added donor `CodeCacheBase`, ARM64 platform feature helpers, and HIR
    compatibility helpers/opcodes needed by the donor emitter.
  - Current quick shims: donor tracer logging maps to this fork's logging API;
    donor `DEFINE_int64` cvars use supported cvar types; Capstone AArch64 maps
    to this tree's ARM64 naming; POSIX unwind uses the reserved unwind buffer;
    the donor XexModule info-cache MMIO recorder is stubbed until imported.
  - Native core build passes for Android `arm64-v8a` and `x86_64`.
  - Next proof needed: FullDeploy to Thor, launch Blue Dragon with
    `-MmapAddressHigh 8`, then capture logcat/screenshot and fix the first
    runtime crash or missing sequence.
- Blue Dragon current GPU wall:
  `docs/research/20260518-223723-blue-dragon-format-six-present-probe.md`.
  - Blue Dragon now runs long Thor captures with thousands of `VdSwap` /
    `XE_SWAP` events and no native fatal, AndroidRuntime crash, GPU hang, or
    ARM64 fallback lines in the latest runs.
  - The official `VdSwap` frontbuffers `1CA1C000` and `1CDB4000` are still
    zero at swap in checksum probes.
  - Forced and scored presentation prove Android/Vulkan can display guest
    resolve memory, so the remaining wall is not the APK surface or Vulkan
    presenter.
  - `-VulkanPresentScoredResolveRequiredFormat 6` follows dynamic format-6
    resolves and currently presents a white clear-like `720x720` source
    (`1D88F000+00385000`, first sampled value `00FFFFFF`).
  - Use the required-format knob only as a comparison probe. It is not a
    compatibility fix. The next GPU work should correlate render-target
    state, clear/mask state, draws, resolves, and candidate content before
    sweeping more addresses.
- Blue Dragon draw/resolve proof:
  `docs/research/20260518-230320-blue-dragon-draw-state-flat-resolves.md`.
  - `vulkan_present_scored_resolve_reject_clear_like=true` now rejects repeated
    or low-variation resolve candidates instead of selecting white or dark
    clear-like surfaces.
  - With clear-like rejection enabled, all sampled scored present candidates in
    the latest 800-sample Thor run were rejected as clear-like. That means
    source selection is not the immediate wall.
  - `vulkan_trace_draw_state=true` proves real Vulkan draw calls execute with
    rasterization, pixel shaders, color writes, and `normalized_color_mask=000F`.
  - Resolved shared-memory output after those draws remains zero or flat /
    clear-like, while official `VdSwap` frontbuffers remain zero.
  - Next GPU lane: instrument the Vulkan render-target dump / resolve boundary
    so we can prove whether host render targets are already flat, or whether
    pixels are lost while dumping EDRAM or copying to shared memory.
- Blue Dragon EDRAM dump and FSI probe:
  `docs/research/20260518-231925-blue-dragon-edram-dump-and-fsi-probe.md`.
  - `vulkan_trace_edram_checksum=true` can read back the host-render-target
    EDRAM dump span before the shared-memory resolve copy, then reopen the
    Vulkan submission for the normal resolve.
  - Latest evidence shows EDRAM-after-dump is already zero or near-uniform for
    the Blue Dragon present candidates, and the later shared-memory resolve
    preserves that flat content.
  - The AYN Thor Adreno driver does not expose fragment shader sample or pixel
    interlock, so `render_target_path_vulkan=fsi` falls back to `fbo`.
  - The next lane is host render-target image / draw-output correctness:
    prove whether the RT image itself is flat before dump, or whether the dump
    shader / format path reads it incorrectly.
- Blue Dragon raster/dump-state proof:
  `docs/research/20260518-233120-blue-dragon-raster-dump-state.md`.
  - The visible fullscreen/composite draw family has sane scissor rectangles
    for 640x360, 320x180, 160x90, 80x45, and 1280x720 passes.
  - Those textured draws are not obviously depth-killed; they reach
    `pixel=true`, `ps_writes=1`, and `normalized_color_mask=000F`.
  - Dump routing matches the expected render-target keys, formats, rectangles,
    and dispatch sizes, but EDRAM/shared-memory samples remain flat.
  - Next GPU lane: trace texture inputs and shader bindings for the textured
    passes, especially `used_textures=0000007F` on the 1280x720 pass and
    `used_textures=00000001` on the downscale/composite passes.
- Blue Dragon texture-input proof:
  `docs/research/20260518-234356-blue-dragon-texture-input-trace.md`.
  - The visible 1280x720 fullscreen draw reaches `pixel=true`, writes all color
    channels, and uses pixel texture mask `0000007F`; all seven traced fetches
    have valid Vulkan texture bindings and loaded unsigned texture objects.
  - Downscale/composite draws also have valid pixel texture bindings; no traced
    used fetch reports `valid=false`, `has_unsigned=false`, or `scaled=true`.
  - The resolved output is still flat or clear-like, so the wall moved from
    texture descriptor plumbing to source texture population, pixel shader
    translation/constants, or the earlier render pass that produces the sampled
    sources.
  - Next GPU lane: split source data from shader output with active-texture
    source checksums and a targeted shader/constant trace for the visible
    fullscreen pixel shader hashes.
- Blue Dragon texture-source checksum proof:
  `docs/research/20260518-235501-blue-dragon-texture-source-checksum.md`.
  - `vulkan_trace_texture_source_checksum=true` is routed through Android and
    the Thor script and can read back active pixel texture source ranges before
    a draw samples them.
  - The visible fullscreen shaders have valid source ranges, but the large
    render-chain sources are already flat or clear-like before the draw.
  - The later `used_textures=000001FF` shader sees non-clear packed DXT source
    data, but the resolved output remains clear-like.
  - Next GPU lane: trace shader constants and shader-source semantics for the
    visible fullscreen pixel shader family before changing render-target code.
- Blue Dragon shader-constant proof:
  `docs/research/20260519-000221-blue-dragon-shader-constant-trace.md`.
  - `vulkan_trace_shader_constants=true` is routed through Android and the Thor
    script and logs active pixel shader constant maps and values.
  - The visible fullscreen pixel shaders have present, finite-looking constants
    where expected; they do not depend on bool or loop constants.
  - This moves the immediate wall past descriptor setup, source-memory readback,
    and gross constant upload.
  - Next GPU lane: dump and inspect Xenos microcode / translated SPIR-V for the
    visible fullscreen shader hashes to see which fetches and constants really
    drive exported color.
- Blue Dragon shader-dump and presenter proof:
  `docs/research/20260519-001514-blue-dragon-shader-dump-presenter-proof.md`.
  - `dump_shaders` is now routeable from ADB extras, and
    `tools/thor/thor_xenia_debug.ps1 -DumpShaders auto` creates/pulls a
    timestamped shader dump directory alongside the capture.
  - Vulkan now dumps translated SPIR-V binaries when `dump_shaders` is enabled;
    Xenos ucode binary/text dumps are produced by the existing shader-analysis
    path.
  - The `0ABADD9DA4373CBA` composite shader's non-clear DXT fetch 7 is gated
    out in the observed draw by `c26.x = 2.0`, so the final chain is mostly
    sampling already-flat render targets.
  - `vulkan_debug_solid_guest_output=true` produces a visible magenta frame on
    Thor, proving the Android/Vulkan presenter is alive. The black-frame wall is
    upstream of presentation.
- Blue Dragon targeted vertex-fetch proof:
  `docs/research/20260519-003650-blue-dragon-targeted-vertex-fetch-trace.md`.
  - `vulkan_trace_vertex_fetch_checksum=true` is routed through Android and the
    Thor script, with `vulkan_trace_vertex_fetch_shader_filter` for
    shader-hash-focused captures.
  - The final `5CF1EE2E3C82546B` / `9567C79307ACC6F5` fullscreen blit has sane
    position and UV attributes, and its vertex color multiplier is not zero.
  - The 1280x720 source feeding that final pass is already zero or clear-like
    before the blit resolves to `1D14C000` and the official frontbuffers.
  - The next GPU lane added targeted shader filters for upstream draw-state,
    shader-constant, and texture-source probes.
- Blue Dragon targeted shader-filter proof:
  `docs/research/20260519-005540-blue-dragon-targeted-shader-filter-trace.md`.
  - `vulkan_trace_draw_shader_filter`,
    `vulkan_trace_shader_constants_shader_filter`, and
    `vulkan_trace_texture_source_shader_filter` are routed through Android and
    the Thor script.
  - `05775DE8A2B0B3F5` writes nonzero clear-like `1DC14000` content, then
    `0ABADD9DA4373CBA` samples that source and resolves `1DC14000` back to
    zero.
  - Later post passes and the final `9567C79307ACC6F5` blit sample already-zero
    inputs, so the first concrete zeroing point is upstream of final blit and
    presentation.
  - Blue Dragon also successfully opens `D:\pack\!necessity.ipk`, then probes
    missing loose `D:\!necessity\font\...` paths and uses `cache:\pack`; audit
    VFS/cache behavior before assuming a pure shader translator bug.
- Blue Dragon pixel-output override proof:
  `docs/research/20260519-012358-blue-dragon-pixel-output-override-probe.md`.
  - `vulkan_debug_pixel_shader_output_filter` and
    `vulkan_debug_pixel_shader_output_mode` are routed through Android and the
    Thor script for research-only shader export overrides.
  - Forcing `0ABADD9DA4373CBA` to magenta makes the `1DC14000` resolve
    nonzero (`first_sample=FF03F0FF`), proving that shader/color export can
    write the target.
  - A following copy/draw/resolve sequence to `raw_dest_base=1DC14000` turns the
    target back to zero before `9567C79307ACC6F5` samples it for the final
    blit.
  - The current GPU wall is therefore the post-`0ABADD9DA4373CBA` zeroing
    writer, not Android presentation, final-blit vertex data, or the targeted
    shader's ability to export nonzero color.
  - Next GPU lane: tag copy/resolve trace lines with recent draw sequence and
    shader hashes, then identify the exact writer that clears `1DC14000`.
- Blue Dragon post-`0ABADD9DA4373CBA` shader-chain proof:
  `docs/research/20260519-014135-blue-dragon-post-abadd-shader-chain.md`.
  - Vulkan copy trace lines now include copy sequence, recent draw sequence,
    recent vertex shader hash, and recent pixel shader hash.
  - Forced `0ABADD9DA4373CBA` writes `1DC14000` nonzero, but the following
    `57B736C8B5D4E953` pass writes it back to zero.
  - Forced `57B736C8B5D4E953` writes `1DC14000` nonzero and shows a visible
    debug frame, but the following `B02CC5F55AD0D140` pass writes it back to
    zero.
  - Forced `B02CC5F55AD0D140` writes `1DC14000` nonzero; the final
    `9567C79307ACC6F5` pass samples it, resolves `1D14C000` nonzero, and then
    writes an official frontbuffer nonzero.
  - The older generic frontbuffer checksum reads CPU guest memory and can be
    misleading for Vulkan shared-memory state. Prefer Vulkan resolve,
    texture-source, copy-state, and swap shared-memory checksums for present
    proof.
  - The current GPU wall is the unforced `B02CC5F55AD0D140` shader pass or its
    texture/constant/boolean state, especially format 7 `2_10_10_10`, DXT
    input, branch booleans, log/exp/NaN handling, and export clamping.
- Blue Dragon title exp-adjust proof:
  `docs/research/20260519-022333-blue-dragon-title-exp-adjust-probe.md`.
  - B02 raw unsigned sample output is nonzero, but the post-processed fetch
    output is black because the active fetch path applies a `-16` result
    exponent adjustment to values already normalized by the temporary
    `2_10_10_10` fallback.
  - `vulkan_debug_texture_fetch_disable_exp_adjust=true` reached the visible
    `press START` title prompt in `scratch\thor-debug\20260519-022127-*`.
  - Keep this cvar default-off and research-only. This proof was superseded by
    the dword3 result exponent fix below.
- Blue Dragon dword3 result exponent fix:
  `docs/research/20260519-133516-blue-dragon-dword3-title-fix.md`.
  - The SPIR-V translator was using fetch constant dword 4 for result exponent
    adjustment. That word carries LOD/gradient exponent bias in the same bit
    range.
  - The title path now reads result exponent adjustment from dword 3.
  - Validated title captures: `scratch\thor-debug\20260519-132943-*` and
    post-push `scratch\thor-debug\20260519-141908-*`.
  - Validated cvar state: `disable_fetch_exp_adjust=false`,
    `force_signed_2101010_unorm=true`.
- Blue Dragon nop HID sequence proof:
  `docs/research/20260519-144858-blue-dragon-nop-hid-sequence.md`.
  - `hid_nop_button_sequence` supports entries like
    `start@45000:3000;a@68000:3000`.
  - `scratch\thor-debug\20260519-144021-*` reached the language menu.
  - `scratch\thor-debug\20260519-144529-*` reached the opening scene after
    language selection.
  - This is scripted research input only; it is not real Android controller
    mapping yet.
- Blue Dragon performance triage:
  `docs/research/20260519-151305-blue-dragon-performance-triage.md`.
  - Trace-heavy proof runs are not speed samples.
  - Trace-off run `scratch\thor-debug\20260519-150821-*` stayed alive in the
    opening sequence.
  - Live `top -H` showed main guest CPU and XMA decoder ahead of GPU command
    thread CPU use.
- Xbox 360 to Thor Max translation report:
  `docs/research/20260519-153016-xbox360-thormax-translation-report.md`.
  - Blue Dragon title/language/opening proof is real progress, but not a
    compatibility claim.
  - The immediate speed wall is CPU/A64 dispatch, helper fallbacks,
    guest-to-host transitions, XMA/audio cost, and debug overhead first; GPU is
    second unless a focused capture proves otherwise.
  - Next probes should produce cheap counters for guest functions/blocks,
    helper categories, code-cache links/misses, indirect branches, recompiles,
    and top guest PCs in trace-off runs.
  - Use `apu=nop` or `xma_fast_silence` only as comparison lanes, never as the
    main correctness path.
- A64 speed profile counters:
  `docs/research/20260519-162000-a64-speed-profile-counters.md`.
  - New default-off cvars:
    `arm64_speed_profile_interval_ms`,
    `arm64_speed_profile_top_functions`, and
    `arm64_speed_profile_min_delta`.
  - First profiled Blue Dragon run
    `scratch\thor-debug\20260519-161600-*` built, deployed, stayed alive, and
    produced top-function summaries without fatal, device-lost, GPU-hung, or
    AndroidRuntime crash lines.
  - Final interval was dominated by direct guest calls and top deltas in
    `__savegprlr_29`, `__restgprlr_29`, `8248B040`, and known draw-wait
    function `8246B408`. Next CPU optimization should inspect whether the A64
    backend can safely inline/special-case the PPC save/restore compiler
    helpers.
- A64 GPR/LR helper inline:
  `docs/research/20260519-170744-a64-gprlr-helper-inline.md`.
  - `a64_inline_gprlr_helpers` is routed through Android and the Thor script
    and is default-on in the A64 backend.
  - `__savegprlr_*` is inlined as big-endian guest stack stores for GPRs plus
    the 32-bit LR slot from `r12`.
  - `__restgprlr_*` is inlined as big-endian guest stack loads, but must compare
    the restored LR to the current guest return address. Equal LR returns
    through the current epilog; mismatch LR falls back to the normal A64
    tail-jump/code-cache path.
  - The naive "always epilog" restore shortcut is wrong; it stayed alive but
    blocked the visible Blue Dragon route in `scratch\thor-debug\20260519-170818-*`.
  - Corrected validation `scratch\thor-debug\20260519-171625-*` reached the
    visible opening scene, had no crash/device-loss markers, and removed
    `__savegprlr_*` / `__restgprlr_*` from the top profile.
  - Next speed work should inspect `8246B408` / `8248B040` and consider FPR/VMX
    helper-family inlining.
- Blue Dragon speed-loop noise budget:
  `docs/research/20260519-184650-blue-dragon-speed-loop-noise-budget.md`.
  - Current fast-lane command should explicitly include
    `-A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true`
    when comparing Blue Dragon opening-scene speed.
  - `arm64_blue_dragon_draw_wait_probe_stride` exists and defaults to `1`.
    Larger power-of-two strides are an A/B probe; stride `4096` was not a speed
    win in the first Thor run.
  - `arm64_blue_dragon_draw_wait_inline_tick_step` exists as a research-only
    wait accelerator, but step `1` was not a speed win. Keep it off unless that
    exact variable is under test.
  - `gpu_unknown_register_log_budget` and
    `xboxkrnl_nt_create_file_fail_log_budget` are routed through Android and
    the Thor script. Blue Dragon speed defaults set both to `0` so warning logs
    do not drown profiler captures.
  - `MountCache true` is now testable because speed defaults no longer force it
    off, but the first 110s A/B did not improve the current Blue Dragon route.
  - Latest quiet validation `scratch\thor-debug\20260519-184120-*` reached the
    opening scene with zero unknown-register lines, zero `NtCreateFile failed`
    lines, and `8246B408` still dominating the final A64 speed profile.
- Blue Dragon draw-wait fastpath:
  `docs/research/20260519-192038-blue-dragon-draw-wait-fastpath.md`.
  - `arm64_blue_dragon_draw_wait_fastpath` is routed through Android and
    `tools/thor/thor_xenia_debug.ps1`, but is default-off and title-specific.
  - The direct body for `8246B408` compiles to about `448` bytes versus the
    generic `1632`-byte body and can reach the visible opening scene when paired
    with the real KTHREAD clock probe.
  - `-Arm64BlueDragonDrawWaitInlineTickStep 1` and
    `-Arm64BlueDragonDrawWaitProbeStride 4096` both parked on black-screen
    routes, so do not treat either as a speed default.
  - The useful fastpath currently moves the bottleneck into guest-to-host clock
    updates. Next work should make guest uptime cheap in generated A64 or move
    the update closer to the original PPC load-site semantics.
- Blue Dragon host-counter time fastpath:
  `docs/research/20260519-193313-blue-dragon-host-counter-time.md`.
  - `arm64_blue_dragon_draw_wait_fastpath_host_counter_time` is routed through
    Android and the Thor script, default-off.
  - It reads `CNTVCT_EL0` / `CNTFRQ_EL0` in generated A64, subtracts
    `A64BackendContext::host_uptime_millis_base`, and writes the result to
    `KTHREAD+0x58` without a native clock thunk.
  - Best proof so far:
    `scratch\thor-debug\20260519-192732-*` plus follow-up screenshot
    `scratch\thor-debug\20260519-193236-screenshot.png` reached rendered
    Blue Dragon opening/game content on AYN Thor.
  - Current Blue Dragon command should include
    `-Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitProbe true -MountCache true`.
  - The remaining wall is still `8246B408`, now as generated A64 busy-wait
    cost rather than per-entry native clock thunk cost.
- Blue Dragon wait-yield and timeout probes:
  `docs/research/20260519-200005-blue-dragon-wait-yield-timeout-probes.md`.
  - New default-off/neutral knobs are routed through Android and the Thor
    script: `arm64_blue_dragon_draw_wait_fastpath_native_yield_stride`,
    `arm64_blue_dragon_draw_wait_fastpath_native_sleep_us`, and
    `arm64_blue_dragon_draw_wait_fastpath_timeout_ms` (`5000` preserves the
    current predicate).
  - `-Arm64BlueDragonDrawWaitFastpathNativeYieldStride 8192` was stable but did
    not remove the `8246B408` wall.
  - `-Arm64BlueDragonDrawWaitFastpathNativeSleepUs 100` parked on black screen;
    treat sleep as a negative-control probe until the wait producer is known.
  - `-Arm64BlueDragonDrawWaitFastpathTimeoutMs 1000` was stable but did not
    visibly beat the host-counter route at the 180s/270s checkpoints.
  - Current safe Blue Dragon route remains host-counter fastpath, no sleep,
    default timeout, and `-MountCache true`.
- A64 FPR/VMX helper inline:
  `docs/research/20260519-204252-a64-fpr-vmx-helper-inline.md`.
  - `a64_inline_fpr_helpers` and `a64_inline_vmx_helpers` are routed through
    Android and `tools/thor/thor_xenia_debug.ps1`; both are now default-on in
    the A64 backend after split and combined Thor validation.
  - The Thor script exposes split knobs
    `-A64InlineFprHelpers true|false` and `-A64InlineVmxHelpers true|false`.
    `-A64InlineFprVmxHelpers` is only a legacy alias that sets both when the
    split knobs are absent. The script defaults the split knobs to `true`
    because existing Android configs can persist older `false` values even
    after the C++ defaults change.
  - The helper inline path must do guest stack offset math before applying
    membase. Subtracting after `AddGuestAddressToMembase` parked Blue Dragon on
    black screen in the pre-fix probes.
  - VMX helper inlining must align the guest effective address to 16 bytes and
    preserve the `r11 = -16` side effect from the compiler helper stubs.
  - Current Blue Dragon speed command does not need explicit FPR/VMX helper
    flags when launched through `tools/thor/thor_xenia_debug.ps1` unless
    overriding the script defaults, but it should still include
    `-A64InlinePpcThreadFieldLeafHelpers true
    -Arm64BlueDragonDrawWaitProbe true
    -Arm64BlueDragonDrawWaitFastpath true
    -Arm64BlueDragonDrawWaitFastpathHostCounterTime true
    -MountCache true`.
  - Validation capture `scratch\thor-debug\20260519-203701-*` reached the
    Microsoft Game Studios opening scene and removed `__savefpr`,
    `__restfpr`, `__savevmx`, and `__restvmx` from late top profile entries.
  - The remaining wall is still CPU/A64 around `8246B408`; do not pivot to
    broad GPU work until speed captures show GPU Commands or GPU VSync as the
    top wall.
- Blue Dragon draw-wait caller sampler:
  `docs/research/20260519-211650-blue-dragon-draw-wait-caller.md`.
  - The sampler switches are routed through Android and the Thor script:
    `-Arm64BlueDragonDrawWaitCallerProfile true`,
    `-Arm64BlueDragonDrawWaitCallerProfileStride N`, and
    `-Arm64BlueDragonDrawWaitCallerProfileBudget N`.
  - Keep this sampler default-off and use it only in narrow evidence captures;
    broad compiled-call tracing was too invasive for this hot wait route.
  - Capture `scratch\thor-debug\20260519-210937-*` found all 80 sampled
    `8246B408` callers at LR `8246E6A4`, resolved as `sub_8246E618+8C`.
  - Focused dump `scratch\thor-debug\20260519-211237-*` reached the Blue Dragon
    title / press START state and showed `8246E618` as a caller loop:
    setup `8246B258`, repeated `8246B408(wait_state)`, progress checks through
    `[r31+0x2A10]`, `[r31+0x2A1C]`, and `r30`, then cleanup `8246B288`.
  - Next speed target is the caller loop or its producer progress path, not
    only the already-shortened `8246B408` callee. Inspect `8246B258` and
    `8246B288` before replacing the whole caller body.
- Blue Dragon inline draw-wait at hot caller:
  `docs/research/20260519-213330-blue-dragon-inline-draw-wait-caller.md`.
  - `arm64_blue_dragon_draw_wait_inline_in_caller` is routed through Android and
    `tools/thor/thor_xenia_debug.ps1` as
    `-Arm64BlueDragonDrawWaitInlineInCaller true|false`; keep it default-off.
  - It requires the base `8246B408` fastpath, only fires inside current guest
    function `8246E618`, and only inlines direct call target `8246B408`.
  - This is safer than replacing all of `8246E618` because setup `8246B258` is
    tiny but cleanup `8246B288` performs timing/stat accounting and may call a
    callback at `[object+0x330C]`.
  - Inline speed capture `scratch\thor-debug\20260519-212544-*` reached the
    loading spinner and removed `8246B408` from top profile entries, exposing
    hot functions `827294CC`, `8272A3A4`, `8272A8E8`, `826C5620`, `826BF770`,
    and critical-section / IRQL helpers.
  - Live route follow-up `scratch\thor-debug\20260519-213255-*` reached the
    Microsoft Game Studios opening scene with inline-in-caller enabled, but this
    is not yet a proven speed win. Use same-route A/B before making it a
    default.
- Blue Dragon hot helper fastpaths:
  `docs/research/20260519-214300-blue-dragon-hot-helper-fastpaths.md`.
  - `826BF770` is byte-copy / `memcpy` shaped; default-off
    `arm64_blue_dragon_memcpy_fastpath` is routed through Android and the Thor
    script as `-Arm64BlueDragonMemcpyFastpath true|false`.
  - `826C5620` is ASCII case-insensitive string-compare shaped and is a likely
    next default-off helper fastpath candidate.
  - `8273EF84`, `8273EF74`, `8273F7B4`, and `8273F7C4` are single-`sc 2`
    syscall/import thunks into HLE.
  - `827294CC` is a compact jump-table dispatch helper; `8272A3A4` and
    `8272A8E8` look like byte/bitstream packing or decompression loops.
  - The current A64 top-function profiler counts function entries, not elapsed
    time. After a function-body fastpath, a function may stay high in the list
    because it is called often. Do not judge body-fastpath wins by entry count
    alone; use same-route time-to-scene, better timing counters, or simpleperf.
- Thor simpleperf speed lane:
  `docs/research/20260519-215706-thor-simpleperf-speed-lane.md`.
  - `tools/thor/thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture`
    supports `-Simpleperf true`, `-SimpleperfStartSecond`,
    `-SimpleperfSeconds`, `-SimpleperfFrequency`, `-SimpleperfEvent`,
    `-SimpleperfCallGraph`, and `-SimpleperfPercentLimit`.
  - The script records with Android `simpleperf --app`, pulls `perf.data` into
    ignored `scratch/thor-debug`, and writes a host-side report using the NDK
    `simpleperf.exe` plus the unstripped Android `libxenia-app.so` symbol dir.
  - Long capture `scratch\thor-debug\20260519-215347-*` at 105s-117s showed
    about 34% self in generated guest code, about 8% native compile/resolve,
    about 4% GPU commands, and about 3% XMA/audio while the screen remained on
    the loading spinner.
  - The overlapping A64 entry profile points the next direct guest fastpath at
    `826C5620` first, with `827294CC`, `8272A3A4`, `8272A8E8`, and
    `826BF770` still in the hot set.
- Blue Dragon `826C5620` stricmp body replacement failed:
  `docs/research/20260519-220900-blue-dragon-stricmp-fastpath-failed.md`.
  - A temporary default-off whole-function A64 replacement for `826C5620`
    produced black-screen idle/stall captures instead of the loading spinner.
  - Capture `scratch\thor-debug\20260519-220047-*` stalled by 130s; revised
    capture `scratch\thor-debug\20260519-220549-*` still stalled by 75s after
    preserving extra volatile-looking registers.
  - Do not re-add this as a body fastpath until a return-state probe proves
    exact PPC-visible effects, especially condition-register fields and caller
    register dependencies.
  - Next experiment should sample the normal generated `826C5620` return state
    or move to `827294CC`, `8272A3A4`, and `8272A8E8`.
- Blue Dragon exact-state `826C5620` stricmp fastpath:
  `docs/research/20260519-223000-blue-dragon-stricmp-exact-fastpath.md`.
  - `arm64_blue_dragon_stricmp_return_profile` is routed through Android and
    `tools/thor/thor_xenia_debug.ps1` as
    `-Arm64BlueDragonStricmpReturnProfile`, with stride and budget switches.
  - Return-state capture `scratch\thor-debug\20260519-221800-*` proved the
    helper updates meaningful CR state; `cr0`, `cr1`, `cr5`, `cr6`, and `cr7`
    matter for an exact body replacement.
  - `arm64_blue_dragon_stricmp_fastpath` was reintroduced as an exact-state
    version that writes `r3`, `r4`, `r5`, `r6`, `r9`, and CR field bytes 0..2
    for `cr0`, `cr1`, `cr5`, `cr6`, and `cr7`, while leaving SO bytes intact.
  - Fastpath capture `scratch\thor-debug\20260519-222409-*` reached the Blue
    Dragon loading spinner with no searched fatal markers. Keep the fastpath
    default-off until simpleperf or same-route timing proves a real speed win.
- Blue Dragon `827294CC` jump-table helper fastpath:
  `docs/research/20260519-224000-blue-dragon-jump-table-fastpath.md`.
  - `arm64_blue_dragon_jump_table_fastpath` is routed through Android and
    `tools/thor/thor_xenia_debug.ps1` as
    `-Arm64BlueDragonJumpTableFastpath`.
  - The helper writes `r25`, `r26`, `r12`, `r0`, and `ctr`, does the big-endian
    branch-table load from `0x827294EC + ((r10 << 2) & 0xFFFFFFFC)`, then uses
    the normal A64 tail-call path.
  - Capture `scratch\thor-debug\20260519-223650-*` reached the loading spinner
    with no searched fatal markers. `827294CC` code size dropped from 356 to
    308 bytes, but Blue Dragon still did not reach title by 95s.
  - The next speed lane should inspect `8272A3A4` and `8272A8E8` or add
    body-time/generated-code profiling, because entry counts alone now overstate
    the cost of tiny high-frequency helpers.
- Blue Dragon Thor debug lane as of 2026-05-20:
  - `arm64_speed_profile_thread_snapshot` logs per-XThread `last_fn`,
    `last_ret`, LR/CTR/R1/R3/R4 from the A64 backend context on speed-profile
    intervals. It retries the processor debug lock briefly; if still blocked,
    the skip line includes `last_global_owner_sys_tid` plus PPC global-lock
    owner breadcrumbs.
  - `tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`
    reapplies `MANAGE_EXTERNAL_STORAGE` via appops after install, and the debug
    APK declares `android.permission.MANAGE_EXTERNAL_STORAGE`. If a capture
    reports `funcs=0` forever, check storage appops before debugging A64.
  - `tools\thor\thor_xenia_debug.ps1` defaults `mount_cache=true`. This removes
    `ResolvePath(cache:\pack) failed - device not found`, but the latest
    cache-mounted run still black-idles by the 45s interval with
    `entry_delta=0`; the next correctness target is wait/event/cache-route HLE,
    not another blind helper splice.
  - Blue Dragon has now reached the visible `press START` title screen on the
    AYN Thor Max, but this is still a research milestone, not compatibility.
    Proof captures:
    - `scratch\thor-debug\20260520-005304-*`: wait-traced 45s run, title
      screenshot, A64 thread snapshots active.
    - `scratch\thor-debug\20260520-010150-*`: no wait trace, 45s live capture,
      title screenshot.
    - `scratch\thor-debug\20260520-011006-*`: title-capture mode with thread
      snapshots, title screenshot, same APK SHA
      `EA375B75215C12AC84EB5E121C7FC0AC191B189EAD9BBB97F333CC49B2372EF7`.
  - Title is timing-sensitive. Several same-cvar captures black-idled before
    the title loop, while snapshot-enabled runs continued into the title. Treat
    the next bug as route determinism around the cache/file/wait phase,
    especially `cache:\pack\item_rec.*` and the XThread wait set.
  - Use `tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonTitleCapture`
    for title proof. It avoids auto START/A input and avoids the forced Vulkan
    signed 10:10:10:2 fallback used by the speed lane. Its default screenshot
    behavior is final-only; timed screenshots are available via
    `-TitleScreenshotSeconds` but can perturb timing, so use them only when the
    experiment explicitly needs multi-frame proof.
  - `arm64_speed_profile_thread_snapshot_on_idle` is the lower-impact black-idle
    probe. It waits until A64 counters have shown activity, then logs one
    thread snapshot only if a later speed-profile interval goes flat. Prefer it
    over always-on snapshots when trying to reproduce the no-title black-idle
    path without changing early timing.

## Codex Hooks / Automation

- Research note: `docs/research/20260519-133516-codex-hooks-blue-dragon-loop.md`.
- Current research refresh:
  `docs/research/20260521-190747-codex-goal-loop-research.md`.
- Project hooks live in `.codex/hooks.json` and must be reviewed/trusted by the
  Codex environment before they run.
- `.codex/hooks/blue_dragon_stop.ps1` is passive by default. It now supports
  the generic config file `scratch\thor-debug\codex-goal-loop.json` and keeps
  the legacy `continue-until-blue-dragon-title.flag` path for compatibility.
- Use `tools\thor\thor_codex_goal_loop.ps1` to enable, disable, reset, or
  inspect the loop. The current Blue Dragon speed/opening loop command is:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonOpeningSpeed`.
- For the user's "do not stop researching until Blue Dragon works at full
  speed" instruction, use the dedicated bounded full-speed command:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonFullSpeed -MaxAttempts 48`.
  Read `.agents/skills/xenia-blue-dragon-full-speed-autopilot/SKILL.md` first
  for the continuation contract, then
  `.agents/skills/xenia-working-fast-autonomy/SKILL.md` for the concrete
  worker loop used by each follow-up.
  The proof marker is
  `scratch\thor-debug\latest-blue-dragon-full-speed-proof.json`.
- Full-speed proof means a quiet Thor speed capture with Blue Dragon Disc 1
  launched from the known SD-card path, visible title/opening/gameplay route
  evidence, commit/APK hash/cvars/capture paths recorded, no searched fatal
  markers, and sustained at least 30 FPS over a representative 180-second route
  segment. Track 60 FPS as a stretch target when the game itself allows it.
- The Stop hook must stay bounded. Default max is 12 Stop-hook attempts, after
  which it should stop and summarize the blocker, capture paths, and next
  experiment instead of running forever.
- Prefer a Codex thread automation only for scheduled later wakeups. Use the
  project Stop hook for active-session bounded continuation, and use
  `.agents/skills/xenia-codex-goal-loop/SKILL.md` when setting either up.

## Android ARM64 Risk Register

- CPU backend: the active AArch64 path is now a donor aX360e/Edge A64 backend
  import. It builds, but runtime correctness on Thor is unproven after the hard
  swap.
- JIT memory: Android executable memory and cache coherency must be tested on device.
- Guest memory layout: verify fixed mappings and any 32-bit guest assumptions on Android.
- Vulkan: the manifest requires Vulkan, but runtime feature probing still needs Thor Max logs.
- Input: normal Android launcher game launches now default to `hid=android`
  instead of `hid=nop`, so the Thor built-in pad is exposed as XInput
  controller 1 for handheld play tests. Keep `hid=nop` only for scripted
  START/A research automation and speed captures.
- Input update: Android has a first-pass `hid=android` bridge for Thor
  controller 1. It maps Android gamepad keycodes, sticks, triggers, and hat
  axes to XInput user 0. On the Thor Max, `dumpsys input` reports the built-in
  pad as `Odin Controller` vendor `0x2020`, product `0x0111`, with `X/Y`,
  `Z/RZ`, `HAT_X/HAT_Y`, and `BRAKE/GAS` axes. 2026-05-24 validation:
  `ApkShellDeploy` passed on serial `c3ca0370`, `LaunchBlueDragon` with
  `-HidDriver android -LogLevel 2` logged the Android HID active-as-XInput
  info line, and synthetic Android START/A keyevents produced native
  `Android HID: key` down/up rows with no fatal markers.
- Input follow-up: `docs/research/20260524-122605-thor-controller-device-aware-input.md`
  fixes Thor/Odin controller event filtering after the launcher default fix.
  `EmulatorActivity` now forwards recognized D-pad/gamepad key codes even when
  Android reports them as keyboard/virtual-source events, accepts the
  `Odin Controller` device fallback for motion, retries joystick/gamepad axis
  ranges, and logs the first few bridge events as `XeniaInput`. Native Android
  HID also maps `BUTTON_C -> X` and `BUTTON_Z -> Y` for Thor keylayout
  compatibility. `FullDeploy` and `ApkShellDeploy` passed on `c3ca0370`, and
  synthetic `DPAD_UP`, `BUTTON_A`, and `BUTTON_C` events reached both Java and
  native Android HID logs.
- Speed lane update: `LaunchBlueDragonSpeedCapture` now enables the validated
  Blue Dragon A64 speed pack by default, unless a flag is explicitly overridden
  on the command line. This prevents new captures from accidentally measuring
  the old `8246B408` draw-wait wall. Short validation capture
  `scratch/thor-debug/20260520-111324-*` reached Blue Dragon `press START` at
  20 seconds without manual speed flags. Keep `xma_fast_silence` opt-in only.
- A64 offset-lowering update:
  `docs/research/20260520-125511-a64-offset-lowering-speed.md`.
  The safe generic win is `AddGuestMemoryOffset`: skip redundant `mov w0,w0`,
  return immediately for zero offsets, encode small constant offsets directly,
  and encode page-aligned shifted immediates when possible. Thor captures
  `scratch/thor-debug/20260520-124856-*` and
  `scratch/thor-debug/20260520-125303-*` stayed in `EmulatorActivity` with no
  searched fatal markers; `8272A3A4` stayed at `code_size=14612` versus the
  older speed-pack baseline around `15952`. The shifted-immediate case did not
  further shrink the current Blue Dragon hot blocks, but is still a correct
  generic lowering.
- Do not retry broad wrapped-immediate `ADD`/`SUB` lowering without per-op
  semantic proof. The rejected experiment in
  `scratch/thor-debug/20260520-124314-*` shrank `8272A3A4` to `14532`, but the
  emulator process exited cleanly around 41 seconds and the script then fell
  into launcher/file-picker noise. Code-size shrink alone is not enough.
- Wrapped-immediate follow-up:
  `docs/research/20260520-144829-a64-wrapped-addi-bisect.md`. aX360e does not
  have a better implementation for this exact `ADD`/`SUB` path; its A64 donor
  code still materializes large constants. A broad I32/I64 rewrite
  (`scratch/thor-debug/20260520-143752-*`) and an `ADD_I32`-only rewrite
  (`scratch/thor-debug/20260520-144309-*`) both reproduced the black/idle
  route signature with no fatal marker. The restored donor/default backend
  (`scratch/thor-debug/20260520-144638-*`) resumed healthy entry deltas through
  60 seconds. Do not reintroduce wrapped `ADD`/`SUB` immediate lowering as a
  default path until a lowering audit names the guest functions and HIR
  instructions rewritten and a Thor route proof reaches the same later active
  intervals.
- Narrow wrapped-immediate success:
  `docs/research/20260520-151030-a64-add-i64-wrapped-immediate-fastpath.md`.
  The audit proved the first useful Blue Dragon candidate budget was `ADD_I64`
  with wrapped small-negative constants. The route-proven default-on lowering is
  only `ADD_I64 reg, reg, -N -> SUB #N` for `N <= 4095`, guarded by
  `arm64_add_i64_wrapped_imm_fastpath` and rollback flag
  `-Arm64AddI64WrappedImmFastpath false`. Do not generalize this to `ADD_I32`
  or `SUB` without a separate audit and Thor route proof.
- I64 logical-immediate lowering:
  `docs/research/20260520-152237-a64-i64-logical-immediate-lowering.md`.
  aX360e/Edge donor comparison showed the 32-bit logical paths already used
  immediate-aware helper lowering, while 64-bit `AND` / `AND_NOT` / `OR` /
  `XOR` still materialized every constant. The local A64 sequence now emits
  direct A64 logical immediates for encodable I64 masks and preserves the old
  scratch-register fallback otherwise. Thor proof
  `scratch/thor-debug/20260520-152048-*` stayed active through 60 seconds and
  shrank `8272A3A4` from `12772` to `12652` bytes versus the prior default
  proof.
- AND_NOT logical-immediate follow-up:
  `docs/research/20260520-152733-a64-and-not-logical-immediate-lowering.md`.
  `AND_NOT_I8`, `AND_NOT_I16`, and `AND_NOT_I32` with a constant second
  operand now lower through the existing 32-bit `and_imm` helper as
  `src1 & ~const`. Thor proof `scratch/thor-debug/20260520-152601-*` stayed
  active through 60 seconds, but the current hottest functions did not shrink
  further, so treat this as a generic codegen cleanup rather than a measured
  Blue Dragon speed win.
- Immediate-lowering audit:
  `docs/research/20260520-154650-a64-immediate-lowering-audit.md`.
  Use `-Arm64ImmediateLoweringAudit true
  -Arm64ImmediateLoweringAuditFunction 0x8272A3A4
  -Arm64ImmediateLoweringAuditBudget 256` to log logical-immediate choices for
  one guest function. `tools/thor/thor_xenia_debug.ps1` converts high guest
  addresses to signed 32-bit `--ei` values for Android extras; do not work
  around this by passing decimal unsigned addresses. The first `8272A3A4` audit
  (`scratch/thor-debug/20260520-154135-*`) saw 68 rows, all
  `logical_imm yes action logical-imm`, with zero `mov+reg` logical misses.
  Do not spend the next `8272A3A4` speed pass on more logical-immediate cleanup
  unless a new audit names a miss.
- Zero logical-immediate cleanup:
  `docs/research/20260520-155700-a64-zero-logical-immediate-fastpath.md`.
  Global audit `scratch/thor-debug/20260520-154815-*` showed 46 `mov+reg`
  rows in the first 256 logical-immediate audit rows, including 44
  `OR_I32 imm 0` cases. `OR/XOR reg, 0` now lowers as identity, and
  `AND reg, 0` lowers as zero. Post-change audit
  `scratch/thor-debug/20260520-155154-*` moved those 44 `OR_I32` rows to
  `action identity`; quiet proof `scratch/thor-debug/20260520-155321-*`
  stayed active through 60 seconds. The remaining first-budget misses were two
  `AND_I64` masks, `0x6001007D` and `0xEF`; do not change them without a
  separate audit/proof.
- Zero-select cleanup:
  `docs/research/20260520-161130-a64-zero-select-fastpath.md`. The hot
  `8272A3A4` HIR has repeated variable-shift fixups shaped like
  `select shift_too_large, 0, shifted_value`. Integer `SELECT_I8/I16/I32/I64`
  now uses `wzr` / `xzr` directly for zero constants instead of materializing
  zero into a scratch register before `csel`. First proof
  `scratch/thor-debug/20260520-160530-*` shrank `8272A3A4` to `12540` but
  later idled; repeat proof `scratch/thor-debug/20260520-160757-*` stayed
  active through 70 seconds with no searched fatal markers. Treat this as a
  route-proven codegen cleanup, not a visible title-screen proof.
- Unsigned zero-compare fold:
  `docs/research/20260520-161650-a64-unsigned-zero-compare-fold.md`.
  The Blue Dragon hot dumps contain many CR flag rows such as
  `compare_ult value, 0`; A64 now folds unsigned zero-bound integer compares
  that are always true or false. Proof
  `scratch/thor-debug/20260520-161344-*` stayed active through 70 seconds,
  shrank `8272A3A4` to `12432`, and shrank `8272A8E8` to `5552`. This is still
  CPU/codegen progress only because the screenshot was black.
- Compare-to-context store peephole:
  `docs/research/20260520-163450-a64-compare-store-context-peephole.md`.
  Zero byte/halfword context/local stores now use `wzr` directly, and A64 now
  fuses the specific HIR pair `COMPARE_ULT x, 0` or `COMPARE_UGT 0, x`
  followed by a single-use `store_context` into a direct zero context store.
  The zero-store-only proof `scratch/thor-debug/20260520-162727-*` was safe but
  did not shrink `8272A3A4`; the peephole proof
  `scratch/thor-debug/20260520-163134-*` reached the Blue Dragon Voice Language
  menu, had no searched fatal markers, and shrank `8272A3A4 12432 -> 12332`
  and `8272A8E8 5552 -> 5520`. Keep this as a proven generic cleanup, but the
  route remains CPU/A64 and XMA heavy rather than playable-speed.
- Rejected UGT/EQ context-pair peephole:
  `docs/research/20260520-164105-a64-ugt-eq-context-pair-nohit.md`.
  A local uncommitted matcher tried to share one compare between adjacent
  `COMPARE_UGT x, 0` and `COMPARE_EQ x, 0` context stores. It built and
  reached the Blue Dragon Voice Language menu in
  `scratch/thor-debug/20260520-163945-*`, but clean hot function sizes stayed
  unchanged (`8272A3A4 12332`, `8272A8E8 5520`), so the code was reverted
  before commit. Add an audit counter before revisiting this exact shape.
- UGT/EQ CR branch peephole:
  `docs/research/20260520-170621-a64-ugt-eq-cr-branch-peephole.md`.
  A narrower successor is now proven: it matches `COMPARE_UGT` plus
  same-operand `COMPARE_EQ` only when the stores target adjacent PPC CR `GT`
  and `EQ` bytes and the EQ value is used only by the store plus an immediate
  branch. It emits one `cmp`, two `cset`/`strb` pairs, and direct `b.eq` /
  `b.ne`. Proof `scratch/thor-debug/20260520-170433-*` had no searched fatal
  markers and shrank clean code size from `8272A3A4 12332 -> 12296` and
  `8272A8E8 5520 -> 5508`. This is safe CPU/codegen progress, not a playable
  speed breakthrough.
- Rejected generic compare-branch fusion:
  `docs/research/20260520-171256-a64-compare-branch-black-idle.md`.
  A local uncommitted matcher for integer `COMPARE_*` followed by a single-use
  branch built and deployed, but `scratch/thor-debug/20260520-171056-*`
  black-idled after the early burst (`entry_delta=0` for the final intervals)
  with no clean hot-function shrink beyond the already committed UGT/EQ CR
  peephole. The code was reverted before commit. Do not re-add without a hit
  audit, condition-polarity tests, and an idle-state explanation.
- Edge reservation helper import:
  `docs/research/20260520-173242-edge-reservation-helper-import.md`.
  Refreshed donors on 2026-05-20: aX360e `main` stayed at
  `2b4c889e8f849d07ede83334fa0b323f5c647828`, while Xenia Edge `edge` moved to
  `eee166febd27dfdffc556c4474e1aa49f2b4ccc5`. The latest Edge A64 delta is
  mostly trace plumbing plus a global PPC reservation helper path, not a hidden
  large opcode-table win. This fork now has Edge-style
  `TryAcquireReservationHelper`, `ReservedStore32Helper`, and
  `ReservedStore64Helper`, gated by `arm64_global_reservation_helpers`
  (default `false`) and exposed in the Thor script as
  `-Arm64GlobalReservationHelpers true`. Do not make it default-on for Blue
  Dragon: the default-on experiment `scratch/thor-debug/20260520-172518-*`
  black-idled after the early burst. Treat this as opt-in PPC sync
  infrastructure, not a speed win.
- PowerPC-to-ARM64 public source harvest:
  `docs/research/20260520-180132-powerpc-to-arm64-source-harvest.md`.
  Useful online references exist, but none is a drop-in Xenon-to-Thor backend.
  Dolphin `JitArm64` is the closest direct PPC-to-AArch64 JIT reference for
  pinned state registers, GPR/FPR/CR register caching, branch/CTR/LR lowering,
  and block linking. QEMU TCG is the best reference for translation-block
  chaining, helper global read/write metadata, PPC CR globals, and
  reservation/atomic semantics. RPCS3 PPU/LLVM/AArch64 is a useful second
  backbone for function/module analysis and LLVM hot-function experiments. IBM
  PowerPC branch docs are the sanity check for CR/LR/CTR and BO/BI branch
  semantics. Treat Dolphin/QEMU/RPCS3 implementation code as GPL-family design
  reference unless the repo deliberately changes import/licensing strategy.
  Next speed sprint should be a measured `8272A3A4` state-traffic reduction
  sprint: count context loads/stores, CR traffic, LR/CTR traffic, helper calls,
  exits, endian swaps, and dispatcher returns, then add a Dolphin-style cache
  for CR bytes and hot GPR context slots.
- A64 context-traffic audit:
  `docs/research/20260520-182253-a64-context-traffic-audit.md`.
  The Thor launcher now exposes `-Arm64ContextTrafficAudit true`,
  `-Arm64ContextTrafficAuditFunction 0x8272A3A4`, and
  `-Arm64ContextTrafficAuditBudget 4`, and `EmulatorActivity.java` forwards
  the cvars to native. The first real `8272A3A4` audit measured
  `context_loads=255`, `context_stores=442`, `ppc_stores=252 GPR + 183 CR`,
  85 context barriers, and top CR stores at `0xA3C..0xA3E` (CR6). Top GPR
  slots are `0x108`, `0x118`, `0x028`, `0x110`, `0x0F8`, `0x0D8`, `0x078`,
  and `0x070`. A generic exact `LT/GT/EQ` CR triplet peephole is route-clean
  and reached the Blue Dragon Voice Language menu in
  `scratch/thor-debug/20260520-182128-*`, but `8272A3A4 code_size` stayed
  `12544`; do not count it as a measured hot-path win. Next target is a CR
  shape hit audit plus a narrow GPR context-cache experiment, not GPU work.
- A64 state-cache / CR branch negative result:
  `docs/research/20260521-153300-a64-context-cache-cr-branch-negative.md`.
  The fallthrough context-value cache found zero `8272A3A4` load hits
  (`loads/hits=255/0`, `fallthrough_preserves=0`). Broadening CR
  compare/branch fusion across `context_barrier`, and eliding CR stores before
  a fused branch, both caused Blue Dragon guest crashes. Keep
  `arm64_context_value_cache_fallthrough`,
  `arm64_cr_compare_branch_across_context_barrier`, and
  `arm64_cr_store_elide_for_fused_branch` default-off, and keep Blue Dragon
  presets forcing them off unless explicitly overridden. When touching CR
  triplet lowering, preserve the interleaved `cset`/`strb` order because HIR
  compare values can share one host register.
- Range-aware context promotion foundation:
  `docs/research/20260521-180835-context-promotion-range-aware.md`.
  The pass now records full context byte ranges for load reuse and block-local
  store DSE. Blue Dragon proof `scratch/thor-debug/20260521-180212-*` reached
  the opening sky/dragon-wing scene, but `82282490 code_size` stayed `87168`;
  this is scaffolding for a real state cache, not the state cache itself. Avoid
  using runtime `82282490` context-audit captures as speed evidence for this
  route because `scratch/thor-debug/20260521-175626-*` black-idled before body
  time activated.
- r1 live-in state-cache probe:
  `docs/research/20260522-153742-r1-livein-state-cache-probe.md`.
  The default-off `arm64_context_promotion_gpr_livein_r1` path is route-clean
  but not a speed win yet. The 2026-05-22 Thor capture
  `scratch/thor-debug/20260522-152727-*` had no searched fatal markers, but
  replaced only `16/107` `r[1]` loads in `82282490` despite the offline
  barrier-preserving report predicting `56` replaceable first loads. Keep it
  default-off and do not enable it in Blue Dragon presets. Follow-up
  `docs/research/20260522-154921-r1-livein-branch-preserve.md` found the likely
  gap: `branch_true` / `branch_false` are volatile HIR opcodes, and the probe
  was killing state at conditional branches. NativeCore now builds with branch
  state preserved and better dirty-reason counters. Next work should deploy an
  audited capture and verify the runtime probe reaches roughly the expected
  `50+` replacement range before a quiet speed run. That audit is now complete:
  `docs/research/20260522-160205-r1-livein-audit-capture.md` shows
  `loads_replaced=64/107`, `branch_preserves=153`, and no searched fatal
  markers in `scratch/thor-debug/20260522-155827-*`. Keep the toggle
  default-off. Quiet same-APK A/B
  `docs/research/20260522-162135-r1-livein-quiet-ab.md` did not prove a speed
  win: control `scratch/thor-debug/20260522-161306-*` reached the opening
  sky/wing route with `82282490` body-time rows, while live-in-on
  `scratch/thor-debug/20260522-161710-*` stayed at loading, emitted no
  `82282490` body-time rows after activation, and had no fatal markers. Do not
  repeat the exact A/B unchanged; if revisited, do one control-sandwich route
  confirmation or move to another `82282490` hotpath.
- Audio: Android currently uses 5 ms paced silent nop audio for bring-up. This
  is enough to satisfy early XACT driver registration, but not a real Android
  audio backend.
- Current Blue Dragon speed lane:
  `docs/research/20260524-165127-a64-edge-variant-design-audit.md`.
  New source audit tool `tools/thor/thor_a64_edge_variant_design_audit.ps1`
  confirms the normal entry is a singleton: host entry, compiled direct calls,
  unresolved resolution, the guest-address indirection table, and
  `A64Function::machine_code()` all point to one normal entry per guest
  function. For hot edge `82282490:82282598 -> 82287788`, an edge-specific
  carrier cannot safely preload a spare register, replace `82287788`'s normal
  machine-code pointer, or change the global indirection slot. Next work should
  be a default-off compile/runtime counter-only probe for exact edge-variant
  eligibility, payload materialization, normal-entry fallback, child-call kill,
  and variant-miss counts before any generated behavior change.
- Parallel Vulkan design lane:
  `docs/research/20260524-165737-mature-vulkan-port-speed-design.md`.
  External emulator evidence from Dolphin, RPCS3, PCSX2, Khronos, Android, and
  mobile GPU guidance says mature Vulkan ports win by measuring GPU-side costs
  directly: pipeline cache misses, shader/pipeline compile stalls, barriers,
  render passes, readbacks, texture uploads/copies, descriptor churn, present
  waits, and frame pacing. Do not pivot Blue Dragon away from A64 while Thor
  captures show Main Thread as the wall, but do add PCSX2-style GPU counters
  and pipeline-cache/readback/barrier/present diagnostics before any broad
  Vulkan rewrite.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-163338-a64-state-carrier-abi-audit.md`.
  New source audit tool `tools/thor/thor_a64_state_carrier_abi_audit.ps1`
  confirms the current A64 direct-call ABI is `guest_return_only_in_x0`, the
  existing `f[1]` carrier is only a callee-local `82287788` stack slot, and a
  broader `82282490 -> 82287788` payload is blocked without either a deliberate
  call-ABI extension or an edge-specific compiled entry/variant with
  normal-entry fallback. Do not run another quiet speed A/B or patch generated
  behavior from the current state-carrier evidence. Next work should add
  edge-specific ABI/variant design counters, or prove exact `fpscr` CFG
  writeback/flush rules before any behavior patch.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-145241-state-carrier-followup-plan.md`.
  New offline planner `tools/thor/thor_hir_state_carrier_followup_plan.ps1`
  parses the latest route-clean state-carrier counter row and joins it with
  prior static audits. It keeps `f[1]` as a broader design lane only:
  `3477646` reads and zero fallbacks, but `50.10%` helper-preserved /
  `49.90%` child-spanning traffic and prior stack-slot / pair-entry evidence
  make another narrow patch the wrong next move. It keeps `fpscr` audit-only:
  `3640919` dirty writes, `1182090` required writebacks, and `2924556` call
  kills require exact CFG writeback rules. Do not patch generated behavior
  yet. Next work should design a broader `82282490 -> 82287788` parent/callee
  carrier with compile-time/runtime counters, or prove exact CFG writeback
  rules before any quiet speed A/B.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-143436-state-carrier-audit-interval-fix.md`.
  Route-clean Thor capture `scratch/thor-debug/20260524-143436-*` proved the
  state-carrier audit lane after `tools/thor/thor_xenia_debug.ps1` began
  auto-setting `Arm64SpeedProfileIntervalMs=15000` for row-producing speed
  profiler captures. It reached the visible opening sky / dragon-wing route on
  APK SHA `5A80DD15AF4471FABE16E7370D6CA5EB25FC8582993F0208BE98DB33EFCFA2FD`
  with a clean fatal-marker search and logged final counters:
  `f1_read=3477646`, `f1_helper_read=1742466`, `f1_child_read=1735180`,
  `f1_child_call=576589`, `f1_fallback=0`, `fpscr_read=3640919`,
  `fpscr_dirty_write=3640919`, `fpscr_required_writeback=1182090`,
  `fpscr_call_kill=2924556`, and `fpscr_fallback=0`. Final body-time kept
  `82282490` dominant at `61133316`; Main Thread was still about one full core
  and GPU Commands was low. Treat this as audit evidence, not a quiet FPS
  verdict. Do not repeat the no-interval/log-level-only captures and do not
  patch from this row directly. Next work should be an offline parent/callee
  `82282490 -> 82287788` state-carrier design, especially a broader `f[1]`
  carrier with explicit helper, child-call, barrier, exit, exception, and
  fallback rules, or a CFG-aware fpscr dirty-carrier audit before any speed A/B.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-141502-state-carrier-audit-route-capture.md`.
  Its route-clean no-row result was only partly caused by `log_level=0`; the
  follow-up `scratch/thor-debug/20260524-142827-*` proved that the missing
  `arm64_speed_profile_interval_ms` also blocked the profiler rows.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-133027-8228252c-state-carrier-design-audit.md`.
  New deterministic tool `tools/thor/thor_hir_state_carrier_design_audit.ps1`
  joins parent span HIR, callee HIR, and dynamic edge rows to print exact seed,
  kill, and writeback requirements for `82282490:8228252C-822825C4` and
  `82282598 -> 82287788`. It classifies `f[1]` as a medium-risk
  `read_only_entry_seed` (`loads=10`, `stores=0`, dynamic upper `16912720`),
  `fpscr` as a very-high-risk `dirty_cfg_carrier` (`loads=26`, `stores=26`,
  writebacks at `82287ED4`, `82287EDC`, `82287EE4`, `82288220`), `r[3]` as a
  high-risk mutable cache candidate, and `lr` as blocked call-link state. Do
  not patch behavior yet. Next useful output is a default-off carrier-design
  counter/probe that reports dynamic seed hits, reads, dirty writes, writebacks,
  kills, and fallbacks before any speed A/B.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-131709-8228252c-state-carrier-design-constraints.md`.
  The broader state-carrier design pass keeps `8228252C-822825C4` as the top
  body-backed local target (`approx_exclusive=2876500`) but blocks another
  narrow generated-code patch. The hot edge `82282598 -> 82287788` has
  `calls_total=1691272`; parent `82282490` stores `r[3]`, `f[1]`, `fpscr`,
  and `lr` before or at the call, and callee `82287788` loads all four.
  `f[1]` is read-only but the standalone carrier/pair-entry ROI is too small
  after the previous A/B missed speed proof. `fpscr` has much larger access
  upper bound, but needs CFG-aware dirty writebacks at call-visible PCs
  `82287ED4`, `82287EDC`, `82287EE4`, and `82288220`; `r[3]` is mutable and
  `lr` is call-link state. Do not patch behavior next from a local PC fold,
  standalone `f[1]` thunk, or `fpscr` shortcut. The next useful output is a
  deterministic CFG/interprocedural state-carrier audit that prints seed, kill,
  and writeback requirements for `8228252C-822825C4` and `82282598 -> 82287788`.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-121503-82282490-branch-loop-aggregate-audit.md`.
  The broader branch-loop audit closes the standalone `822824B8 <-> 822825F4`
  branch/GPR lane. Re-running the dynamic slice ranking and branch-state tool
  across `82282490-822824B8`, `822824B8-822824E8`, and
  `822825F4-82282600` shows the loop head plus tail totals `2133128`
  local-exclusive ticks and four CR predicate stores, but only three
  fallthrough-only GPR reload opportunities, all in `822824B8-822824E8`.
  The nearby entry/setup slice adds only `249802` ticks and no reload win. Do
  not patch CR store/barrier fusion or a narrow branch-local GPR carrier next.
  The next useful output should return to higher-traffic CFG-aware or
  interprocedural state-carrier design around `8228252C-822825C4` and the hot
  `82282490 -> 82287788` state round-trip, with exact call/helper/barrier/exit
  flush rules.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-120704-822824b8-branch-state-audit.md`.
  New deterministic tool `tools/thor/thor_hir_branch_state_audit.ps1` audits
  branchy HIR spans for context stores consumed by branch predicates and
  reloads that would need fallthrough-only carriers. On
  `82282490:822824B8-822824E8`, it reports `body_ticks_total=1099164`,
  `class_stores=cr:9,gpr:7`, `class_loads=gpr:4`, three branch predicates
  stored to context before `context_barrier` / branch, and only three GPR
  reload opportunities (`r[11]` twice and `r[31]` once), all crossing branch
  barriers. The loop tail `822825F4-82282600` is similar CR predicate-store
  work and has no reload opportunity. Do not patch `822824B8-822824E8` next:
  broad CR store/compare/barrier fusion is a known Blue Dragon negative lane,
  and the GPR upper bound is too small for another standalone carrier probe.
  Next useful output is either a broader CFG-aware branch/GPR carrier audit
  across the `822824B8 <-> 822825F4` loop and nearby branchy spans, or a return
  to higher-traffic CFG-aware/interprocedural state-carrier design.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-115538-8228252c-lane-closure-next-target.md`.
  The broader `82282490:8228252C-822825C4` span remains the largest local
  target (`approx_exclusive=2876500`, `store_context=27`, `load_context=14`,
  `mul_add=3`, `stvewx=3`), but the immediate narrow lanes are now closed:
  candidate-dead call-boundary stores have no HIR DCE payoff, `f[1]` pair
  entry only saves the seed context load after the stack-slot carrier A/B
  missed, `fpscr` needs CFG-aware dirty writebacks, all-three `stvewx` was not
  speed-positive, and the three-PC `MUL_ADD_V128` fastpath was mixed. Do not
  patch `8228252C-822825C4` behavior next unless the slice is explicitly a
  broader CFG-aware/interprocedural state-carrier design. The later branch-loop
  aggregate audit has now closed the `822824B8` local detour, so do not spend
  another immediate slice on that CR/GPR branch lane.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-114614-82287788-fpscr-dirty-cache-audit.md`.
  New deterministic tool `tools/thor/thor_hir_fpscr_dirty_cache_audit.ps1`
  parses filtered HIR, pairs `fpscr` (`+2628`) load/update/store rows, and
  classifies store-to-next-load transitions by whether they are strict local
  wins, CFG/PHI or stack-carrier wins, external-visibility writebacks, or exit
  flushes. For `82287788` on the hot `82282490 -> 82287788` edge, it reports
  `loads=26`, `stores=26`, `paired_load_store_updates=26`, dynamic upper
  `loads=43973072`, `stores=43973072`, but `strict_same_window=0`,
  `cfg_phi_or_stack_carrier_required=24`, and
  `external_visibility_flush_required=2`. Required call writeback PCs include
  `82287ED4`, `82287EDC`, `82287EE4`, and `82288220`. The parent `82282490`
  check is worse: `loads=12`, `stores=12`, and all 12 transitions require
  external call visibility. Do not implement an `fpscr` dirty-cache behavior
  patch next; the win needs a CFG-aware dirty carrier with exact call/exit
  writebacks, not a Blue Dragon-only peephole. Return to broader
  `8228252C-822825C4` state/vector/FPR work or first build a generic dirty
  state-carrier audit.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-113615-82287788-pair-entry-roi.md`.
  `tools/thor/thor_hir_function_pair_variant_audit.ps1` now supports
  `-CompareExistingStackCarrier`. The pair-specific `f[1]` entry remains
  statically clean for `82282490:82282598 -> 82287788`, but against the
  already-tested stack-slot carrier it only removes the first context load per
  callee call: `existing_seed_context_loads=1`, `existing_reuse_loads=9`,
  `pair_incremental_context_loads=1`, and
  `pair_incremental_static_upper=1691272`. Do not implement a standalone
  pair-entry behavior patch next; the ABI/prolog/thunk complexity is too large
  for this incremental upside after the broader `f[1]` carrier A/B missed
  speed proof. Move to a broader `8228252C-822825C4` state-cache/callee-variant
  design or an `fpscr` dirty-cache audit with exact writeback points.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-112944-82287788-function-pair-variant-audit.md`.
  New tool `tools/thor/thor_hir_function_pair_variant_audit.ps1` audits a
  pair-specific callee entry/thunk candidate for the hot `82282490`
  `82282598 -> 82287788` edge. For `f[1]` (`+296`), it reports parent seed
  store present at `82282594`, callee `loads=10`, `stores=0`,
  `replaceable_loads=10`, `unsafe_loads=0`, and
  `static_replace_upper=16912720` when child calls at `82287854` and
  `82287ED4` are treated as preserved from earlier `821CE028` proof. The
  `fpscr` control is blocked (`loads=26`, `stores=26`,
  `variant_shape=blocked_by_callee_writes`). Do not patch behavior yet:
  `A64Emitter::Call` currently passes guest return in `x0`, so a real speed
  experiment needs a default-off pair-specific thunk/entry seed or a
  compile-time caller-seeded callee-slot proof, not another local
  `load_context` peephole. Keep normal `82287788` entry semantics unchanged.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-111940-8228252c-state-forwarding-plan.md`.
  `tools/thor/thor_hir_interproc_state_roundtrip_audit.ps1` now prints a
  forwarding plan for live direct-call state. For `82282490` call PC
  `82282598 -> 82287788`, the dynamic edge is hot
  (`calls_total=1691272`, `body_ticks_total=5653971`). The plan classifies
  `f[1]` (`+296`) as the only medium-risk read-only candidate, with
  `static_load_upper=16912720` and no callee stores, but the previous
  stack-slot carrier A/B already missed speed proof. Treat the next useful
  lane as a function-pair/callee-variant design audit for `f[1]`, not another
  unchanged local reload probe. Keep the parent context store visible unless a
  visibility model proves it can move. `fpscr` is larger
  (`static_load_upper=43973072`, `static_store_upper=43973072`) but very
  high-risk because it is mutable; do not patch it without exact dirty
  writeback at calls, barriers, exits, exceptions, and all readers. `r[3]` is
  mutable/high-risk, and `lr` is call-link state; do not forward either from
  this evidence.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-110545-blue-dragon-f1-carrier-fastpath-ab.md`.
  The default-off `arm64_blue_dragon_f1_carrier_fastpath` cvar now seeds a
  function-local stack-slot carrier for `82287788` `f[1]` (`+296`) at
  `82287798` and reuses it only at the audited `f[1]`-preserving PCs. NativeCore
  and FullDeploy passed. Audit capture `scratch/thor-debug/20260524-105424-*`
  reached the visible opening sky/dragon-wing route on APK SHA
  `A2A59F7777F77B52526ABF5E401194A4A7490D71B4F7A878ED8099B0CA7557A9` with a
  clean fatal-marker search and final counters
  `seed=223805/688280` and `reuse=443749/1365365`. Quiet same-APK A/B control
  `scratch/thor-debug/20260524-105813-*` versus fastpath-on
  `scratch/thor-debug/20260524-110156-*` was route-clean but did not prove
  speed: control reached a later opening frame, `82287788` code size changed
  `35568 -> 35572`, and `82282490` stayed the real wall. Keep both
  `arm64_blue_dragon_f1_carrier_fastpath` and
  `arm64_blue_dragon_f1_carrier_audit` default-off and out of presets. Do not
  repeat this exact f[1] carrier A/B unchanged. Next useful lane is broader
  `8228252C-822825C4` parent/callee state/vector/FPR reduction, especially a
  design that reduces the live `82282490 -> 82287788` state round-trip without
  hiding guest-visible `r[3]`, `f[1]`, `fpscr`, or `lr` state.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-104145-blue-dragon-f1-carrier-runtime-audit.md`.
  The default-off `arm64_blue_dragon_f1_carrier_audit` cvar counts the
  `82287788` `f[1]` load sites that the replacement probe later targeted.
  Capture `scratch/thor-debug/20260524-103438-*` reached the visible route with
  clean fatal-marker search and counters `total=755910/2159587`,
  `helper_preserved=379058/1081707`, and
  `child_preserved=376852/1077880`, proving dynamic opportunity but not speed.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-102113-821ce028-f1-clobber-audit.md`. The focused
  child audit resolves the main unknown-call blocker from the `82287788`
  `f[1]` carrier lane. Re-running
  `tools/thor/thor_hir_fpr_carrier_safety_audit.ps1` on `821CE028` offset
  `296` reports `target_loads=0`, `target_stores=0`, and
  `decision=no_target_context_access_observed`. The only calls are the
  `call_indirect.6` return paths at `821CE1CC` and `821CE1D4`. Hotpath report
  confirms the only FPR traffic is `f[0]` (`+288`), with no `f[1]` (`+296`) or
  `fpscr` (`+2628`) traffic. Treat `821CE028` as `f[1]`-preserving for the
  narrow `82287788` carrier lane, but do not patch `821CE028` or call it a
  speed target. Next useful lane is a default-off `82287788` runtime carrier
  audit/probe that treats `__savegprlr_28`, `__restgprlr_28`, and direct calls
  to `0x821CE028` as `f[1]`-preserving and counts dynamic replacement
  opportunities for all 10 static `f[1]` loads before quiet A/B.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-101421-82287788-f1-carrier-safety-audit.md`.
  `tools/thor/thor_hir_fpr_carrier_safety_audit.ps1` classifies whether
  `82287788` `f[1]` loads can be served by a safe carrier. For offset `296`,
  it reports `target_loads=10`, `target_stores=0`, `helper_whitelist=2`, and
  `unknown_call_blocked=8`. Source review of `TryEmitGprLrHelperCall` shows
  `__savegprlr_28` / `__restgprlr_28` touch GPR/LR state but not FPR, VMX, or
  `fpscr`, so the first two `f[1]` loads are plausible helper-whitelist
  candidates. The remaining eight loads cross real child calls to
  `0x821CE028` at `82287854` and `82287ED4`, so do not carry `f[1]` across
  them without a separate clobber/use proof. Do not implement a speed patch
  from this audit alone; next useful lane is a default-off dynamic counter for
  helper-whitelist versus unknown-call-blocked `f[1]` hits, or a focused
  `821CE028` `f[1]` clobber/use audit. Keep `fpscr` out of this lane.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-100409-82287788-callee-local-promotion-audit.md`.
  `tools/thor/thor_hir_callee_local_promotion_audit.ps1` splits filtered HIR
  into strict promotion windows broken by labels, context barriers, calls,
  branches, and returns. For callee `82287788`, `f[1]` has `loads=10`,
  `stores=0`, `strict_redundant_loads=0`, `cross_boundary_reload_after_load=9`,
  and `risky_call_crossings=2`; `fpscr` has `loads=26`, `stores=26`,
  `strict_wins=0`, and `cross_boundary_reload_after_store=25`. Do not
  implement a blind callee-local context cache. Keep `f[1]` as the best
  candidate, but the next experiment must be a default-off CFG/function-pair
  carrier audit/probe that proves call-clobber and flush rules for
  `82282490 -> 82287788`. Treat `fpscr` as mutable/high-risk until a source
  review proves exact dirty update forwarding.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-095137-82282598-82287788-state-roundtrip-audit.md`.
  `tools/thor/thor_hir_interproc_state_roundtrip_audit.ps1` now joins
  parent/callee filtered HIR with dynamic call-edge rows for a direct-call
  boundary. For `82282490` call PC `82282598 -> 82287788`, the dynamic edge is
  hot (`calls_total=1691272`, `body_ticks_total=5653971`). Parent `82282490`
  stores `r[3]`, `f[1]`, `fpscr`, and `lr` before or at the call, and callee
  `82287788` loads all of them (`f[1] loads=10`, `fpscr loads=26/stores=26`).
  Do not skip these live stores. The next useful lane is a default-off,
  Blue-Dragon/function-pair carrier or callee-local promotion probe for
  `82282490 -> 82287788`, with explicit helper/exit/exception/alias flush rules
  and route proof before any quiet A/B.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-094213-hir-dead-state-store-dce-audit.md`.
  `tools/thor/thor_hir_dead_state_store_dce_audit.ps1` now answers whether
  moving a target `store_context` suppression into HIR would recursively delete
  upstream work through `DeadCodeEliminationPass`. For exact
  `82282490:8228252C-822825C4` `BlueDragonCallBoundaryDead` stores, the answer
  is no: `target_store_context=13` and `dead_assignments=0`. Do not implement a
  HIR-level version of the same `13`-store suppression. The unsafe all-span
  upper bound exposes `22` dead assignments, mostly the live `f[1]` / `fpscr`
  call-argument path before `82282598 -> 82287788`; focused `82287788` HIR
  proves the callee loads `f[1]` and `fpscr`, so do not skip those stores. Next
  useful lane is an interprocedural argument/state-roundtrip audit for
  `82282598 -> 82287788`, focused on whether live `f[1]` / `fpscr` can be
  carried or promoted without hiding guest-visible PPC state.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-093001-blue-dragon-call-boundary-store-suppression-probe.md`.
  `arm64_blue_dragon_call_boundary_state_suppress_dead_stores` now exists,
  default-off, and suppresses only the `13` route-proven candidate-dead
  Blue Dragon `82282490` stores before `82282598 -> 82287788`. Route-safety
  capture `scratch/thor-debug/20260524-091813-*` reached the visible opening
  sky/dragon-wing route with clean fatal-marker search and proved
  `suppressed=dead=9007635`; live-in stores stayed separate at `live=2771580`.
  Quiet same-APK A/B (`20260524-092153-*` control and `20260524-092523-*`
  suppression-on) was route-clean and shrank `82282490` code size only
  `86828 -> 86768`, but did not prove route-wide speed. Keep the cvar
  default-off and out of presets. Do not repeat this exact A/B next; move to a
  broader `8228252C-822825C4` state/vector/FPR reduction or larger state-cache /
  lowering change backed by fresh offline evidence.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-085451-blue-dragon-call-boundary-state-runtime-audit.md`.
  The default-off `arm64_blue_dragon_call_boundary_state_audit` counter is
  route-clean on `scratch/thor-debug/20260524-084805-*`, with visible opening
  sky/dragon-wing proof and a clean fatal-marker search. Runtime totals exactly
  match the static audit: `675279` dynamic boundary hits, `13` candidate-dead
  stores per hit (`dead=8778627`) and `4` live-in stores per hit
  (`live=2701116`). This is audit proof only, not speed proof, because the
  counter instrumentation grows generated code. Next speed lane is a separate
  default-off, Blue-Dragon/function/PC-gated store-suppression probe for only
  the `13` candidate-dead sites. Do not skip the `4` live-in stores (`r[3]`,
  `f[1]`, `fpscr`, `lr`), and prove route safety before any quiet A/B.
- Previous Blue Dragon speed lane:
  `docs/research/20260524-082324-blue-dragon-mul-add-v128-fastpath-ab.md`.
  `arm64_blue_dragon_mul_add_v128_fastpath` now exists, default-off, and is
  gated to Blue Dragon function `82282490`, PCs `82282568/8228256C/82282570`,
  and Thor hardware denormal-input flushing. `NativeCore` and `FullDeploy`
  passed, and route-safety capture `scratch/thor-debug/20260524-081212-*`
  reached the visible opening sky/dragon-wing route with clean fatal-marker
  search. Quiet same-APK A/B (`20260524-081601-*` control and
  `20260524-081929-*` fastpath-on) was route-clean and shrank `82282490` code
  size `86828 -> 85424`, but did not prove route-wide speed. Keep the fastpath
  default-off and out of presets. Next useful speed lane is broader
  `8228252C-822825C4` local-span state/vector/FPR reduction, not another
  immediate narrow FMA skip.
- Previous Blue Dragon audit lane:
  `docs/research/20260524-080156-blue-dragon-mul-add-v128-audit-capture.md`.
  The deployed runtime audit initially produced no rows because Android did not
  forward `arm64_blue_dragon_mul_add_v128_audit` to native; the missing
  `EmulatorActivity.java` extra bridge is now fixed. Rerun
  `scratch/thor-debug/20260524-075713-*` reached the visible opening
  sky/dragon-wing route with clean fatal-marker search on APK SHA
  `7BBAB603A9931918867E9AAE0869193F3A2049329ED7FB448D36C4E66B97EE85`. Final
  counters for the three `82282490` `MUL_ADD_V128` PCs were hot:
  `total=722256/2131533`, `pc82282568=240752/710511`,
  `pc8228256C=240752/710511`, and `pc82282570=240752/710511`, while
  `sw_flush_path`, `input_denorm`, `output_denorm`, `nan_entry`, and
  `nan_lane` all stayed `0/0`. Next patch should be default-off,
  Blue-Dragon/function/PC-gated, and limited to those three PCs; prove route
  safety before any quiet same-APK speed A/B.
- UI/app flow: Android launcher has a first-pass game file picker, but user experience, permissions, errors, and direct path handling still need hardening.
- Build dependencies: submodules are not initialized after a fresh clone.

## Code Style And Safety

- Follow `docs/style_guide.md`.
- Prefer small, reviewable changes with a worklog entry for each session.
- Do not run destructive Git commands.
- Do not bypass DRM, distribute copyrighted game content, or add workflows that assume pirated games.
- Use legally owned Xbox 360 content only.
- Never claim a game works without a device log, build hash, settings, and a reproducible launch path.
- For Blue Dragon or any retail game, document only the user's legally owned local dump path and never commit game files, keys, screenshots containing private account data, or copyrighted content.

## Useful Commands

```powershell
git status --short --branch
git submodule update --init --recursive
python xenia-build setup --target_os=android
python xenia-build premake --target_os=android
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullApk
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell
```

No-space path workaround used successfully on 2026-05-17:

```powershell
subst X: "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor"
cd X:\android\android_studio_project
.\gradlew.bat :app:assembleGithubDebug
subst X: /D
```

Thor debug loop:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode FindContent
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchBlueDragon
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode Capture
```

Ghidra native import, after pointing to a real Ghidra install:

```powershell
$env:GHIDRA_HOME = "C:\path\to\ghidra"
powershell -ExecutionPolicy Bypass -File tools\thor\ghidra_headless_import.ps1
```

## Resolved User Answers

- Device is the Thor Max tier; exact storage size is not important for current debugging.
- First retail target is the user's legally owned Blue Dragon Disc 1 on the SD card.
- The user accepts adding an AArch64 emitter dependency such as VIXL or AsmJit after research.
