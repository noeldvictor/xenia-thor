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
- Harness refinement: use
  `.agents/skills/xenia-continual-harness-refiner/SKILL.md` when recent slices
  repeat stale lanes, the heartbeat/Stop-hook prompt is stale, or a capture
  changes the next target.

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
experiment ledger. If the problem is stale process rather than missing data,
run the continual-harness refiner window and update the local harness before
another Thor run.

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

Latest evidence supersedes the stale `82490030` and broad `822824F0` lanes:
`docs/research/20260524-052524-82281d28-call-edge-split.md` says the larger
`82281D28` lane is not a local patch target yet. Capture
`scratch/thor-debug/20260524-052031-*` reached the visible opening sky/wing
route with clean fatal search. With the HIR/block dump removed, body rows were
`82282490=30214411`, `82281D28=10212419`, `82287788=3195362`,
`82282388=3020644`, `82282410=2902097`, `821CE028=674464`, and
`826BF770=563064`. Dynamic edges show `8228233C -> 82281D28` at
`body_ticks_total=7350722` and `calls_total=69445`, about `72.0%` of
`82281D28` body time. Reported direct edges are about `89.1%`, leaving only
about `1115427` ticks of approximate parent-exclusive work. Do not patch local
`82281D28` generated code or rerun the exact HIR/block capture unchanged. Next
useful worker output is recursive/SCC attribution plus body-backed `82282490`
local-span work, or a mapper improvement that recovers `8228233C` static span
context before any local `82281D28` patch.

Tooling update:
`docs/research/20260524-053502-call-path-recursive-heuristic.md` adds
recursive-edge coverage and parent-exclusive estimates to
`tools/thor/thor_hir_call_path_report.ps1`. Validation reports `82281D28` at
`89.08%` dynamic direct-edge body and `71.98%` self-recursive body, `82282490`
at `84.08%` direct-edge body and `71.44%` self-recursive body, and old
`8227F1D8` at `98.48%` child-edge body to `82490030`. Before a local codegen
patch on a call-heavy block, run this report. Prefer body-backed local/exclusive
`82282490` spans such as `8228252C-822825C4` for the next speed patch.

Current local-span ranking:
`docs/research/20260524-054204-82282490-exclusive-candidate-ranking.md` adds a
`Local Exclusive Candidate Ranking` to
`tools/thor/thor_hir_dynamic_block_slice_report.ps1`. The current ranking keeps
`8228252C-822825C4` first (`approx_exclusive=2876500`,
`exclusive_pct=33.72`) and `822824F0-82282528` second
(`approx_exclusive=2021174`, `exclusive_pct=57.72`). Do not repeat the exact
narrow `stvewx` stack-lane fold A/B for `82282580/82282584`; it did not prove
speed.

Latest top-span comparison:
`docs/research/20260524-055108-82282490-top-span-comparison.md` says not to
patch generated-code behavior from the comparison alone. `8228252C-822825C4`
is still the larger absolute local target (`approx_exclusive=2876500`), but CR
fusion/elision is rejected, the exact `stvewx` lane-fold A/B already missed
speed proof, and the remaining state/vector/FPR surface needs more provenance.
`822824F0-82282528` is smaller but cleaner (`approx_exclusive=2021174`,
`exclusive_pct=57.72`) and has an exact `lvlx/lvrx` to
`load_vector_shl + permute` to `v[0]` shape. The next useful worker output
should be focused provenance for that `822824F0` vector-load join, or a
deterministic tool/report that proves why a default-off function/span-gated
codegen experiment is safe.

Latest vector-load provenance:
`docs/research/20260524-060827-822824f0-vector-load-provenance.md` and
`tools/thor/thor_hir_vector_load_join_audit.ps1` close the local-only
`822824F0` patch temptation. The span is still real local work
(`body_ticks_total=3501617`, child edge `82274DB0=1480443`, approximate
exclusive `2021174`), but it is not a self-contained vector join:
`82282520 lvlx` produces `v[13]` from `r30 + 0x14`, `82282528 lvrx` produces
`v[0]` from `r30 + 0x20` with a zero path, and the first `vor vr0,vr13,vr0`
join is `8228254C` in the next span. The next useful worker output should be a
cross-span provenance report through at least `8228254C`, or a better-proven
return to the larger `8228252C-822825C4` state/vector/FPR target. Do not patch
local `822824F0-82282528` generated-code behavior yet.

Previous evidence:
`docs/research/20260524-050931-82281d28-focused-capture.md` shows `82281D28`
is the current larger lane, but not yet a local patch target. Capture
`scratch/thor-debug/20260524-050427-*` reached the visible opening sky/wing
route with clean fatal-marker search. Final body rows were
`82281D28=65346716`, `82282490=31615045`, `82282388=18965773`,
`82282410=17323669`, `82287788=3277477`, and `821CE028=716173`. Internal block
body-time is dominated by `8228233C` (`body_ticks_total=46564347`,
`ticks_per_entry=707`), but the warning-level HIR log starts at `82282398`,
so exact span mapping for that hot block is incomplete. Visible HIR after the
missing region shows a call to `0x826BF770` and recursive `0x82281D28`, and the
large `82282388` / `82282410` body rows suggest child/recursive work. Do not
patch local `82281D28` generated code yet. Next useful worker output is a
route-stabilized `82281D28` call-edge split with delayed body-time comparators
`82281D28,82282388,82282410,82282490,826BF770,82287788,821CE028`.

Previous evidence:
`docs/research/20260524-045602-821ce028-focused-capture.md` shows the
`821CE028` child is route-clean but too small for the next immediate codegen
patch. Capture `scratch/thor-debug/20260524-045022-*` reached the visible
opening sky/wing route with clean fatal search. Final body rows were
`82282490=37190996`, `82281D28=7446911`, `82287788=4992080`, and
`821CE028=2523574`. Top local block `821CE048-821CE1CC` is fully local at
`607573` body ticks and has a repeated VMX compare/dot/CR reduction shape.
Do not patch CR store/barrier or broad VMX-dot; if staying on `821CE028`, do
only an offline/function-gated `r10` state-traffic audit first. Prefer the
larger next target: filtered HIR plus delayed body/block-time for `82281D28`.

Previous evidence:
`docs/research/20260524-044223-822877bc-span-reduction-audit.md` shows the
body-backed `822877BC-82287B38` parent span is mostly inclusive child work, not
a safe local generated-code patch target. The new span auditor reports
`body_ticks_total=1173620`, while `82287788 -> 821CE028` accounts for
`1147798` body ticks over `340310` calls, leaving only `25822` approximate
parent-exclusive ticks (`2.2%`). The parent dump still has CR/state/vector
noise, but do not patch it first. Next useful worker output is a route-stable
filtered HIR plus delayed body/block-time capture for `821CE028`, with
`82282490` and `82287788` kept as comparators.

Older evidence:
`docs/research/20260524-042555-82287788-focused-callee-split.md` follows the
`8228252C -> 82287788` child lane. Both captures reached the visible opening
sky/wing route with clean fatal-marker searches. The lower-noise call-edge run
keeps `82282490` dominant at `31584286` body ticks and shows `82287788` as a
secondary local/codegen lane at `3659269` body ticks, with only one meaningful
dynamic child edge: `82287788 -> 821CE028` at `1147798` body ticks over
`340310` calls. The top local span is `822877BC-82287B38`, with
`store_context=186`, `load_context=145`, `context_barrier=15`,
`lvlx/lvrx/vor/stvx`, `lvx128:6`, `vmaddfp:6`, `vspltw:6`, constant
`extract=6`, and `splat=10`. Keep CR compare/barrier fusion, CR-store elision,
and broad VMX-dot toggles default-off; those lanes have prior crash/negative
evidence. Next useful output is a focused `822877BC-82287B38`
state/vector-reduction audit, or a default-off function/span-gated patch only
after that audit proves a safer lowering.

Older evidence:
`docs/research/20260524-040404-blue-dragon-stvewx-lane-fastpath.md` closes the
first `8228252C` lane-fold probe. The default-off A64 `EXTRACT_I32` fastpath
for only `82282580 -> lane 0` and `82282584 -> lane 1` is route-clean and
audited: `scratch/thor-debug/20260524-035227-*` reached the visible opening
sky/wing route, had clean fatal-marker search, and ended with
`fastpath=446272/1338814 fallback=0/0`. Quiet same-APK A/B did not prove a
speed win: control `scratch/thor-debug/20260524-035623-*` reached
`Microsoft Game Studios Presents`, while fastpath-on
`scratch/thor-debug/20260524-035944-*` reached an earlier sky/wing frame.
Keep `arm64_blue_dragon_stvewx_stack_lane_fastpath` and audit default-off, and
do not repeat the exact A/B unchanged. The next useful worker slice is larger
body-backed work in `8228252C-822825C4`, especially the `8228252C -> 82287788`
child path or broader state/vector/FPR traffic.

`docs/research/20260524-034103-8228252c-stvewx-lane-provenance.md` proves the
first two `stvewx` dynamic extract lanes in `8228252C-822825C4`:
`82282580 -> lane 0` from `r1 + 0x50` and `82282584 -> lane 1` from
`r1 + 0x54`, under normal PPC 16-byte stack-pointer alignment. `82282588`
stays unknown because its address comes from `r6 + 0x8`. The next worker slice
may implement a default-off Blue-Dragon/function/PC-gated A64 `EXTRACT_I32`
fastpath for only those two PCs, with audit/counters before any quiet speed
A/B.

`docs/research/20260524-033116-8228252c-a64-codegen-audit.md` adds a concrete
A64 lowering audit for `8228252C-822825C4`. The slice still has body total
`8530471`, child edge `8228252C -> 82287788` body `5653971`, and approximate
exclusive local work `2876500`, but the local generated-code floor is now
clear: `store_context:27`, `load_context:14`, `extract:9`, `splat:6`,
`stvewx:3`, and `vmaddfp:3`. The three `stvewx` PCs are `82282580`,
`82282584`, and `82282588`; the first two look like stack-address stores from
`r1 + constant`, while the third still needs `r6` provenance proof. The next
worker slice should prove those lane assumptions offline or make a default-off,
function/block-gated lane-folding probe for only the two proven `r1` stores.

`docs/research/20260524-032005-82282490-dynamic-block-slice-report.md` adds the
dynamic-block map requested by the previous slice. The top local target is now
`8228252C-822825C4`: body total `8530471`, edge body `5653971`, approximate
exclusive `2876500`, and vector/FPR/state traffic (`vspltw:6`, `lvx128:4`,
`stvewx:3`, `vmaddfp:3`, `store_context:27`, `load_context:14`,
`extract:9`, `splat:6`). The next worker slice should run an A64
generated-code audit for this exact span before writing a default-off
function/block-gated vector/store or state-traffic experiment.

`docs/research/20260524-031017-82282490-exclusive-callee-attribution.md` now
narrows the recursive-wall lane. Route-clean same-run block body-time plus
call-edge timing `scratch/thor-debug/20260524-030450-*` shows dynamic
direct-call edges are `84.08%` of final `82282490` body ticks, with
`822825E0 -> 82282490` alone at `72194274` ticks (`71.44%` of final
`82282490` body time and `98.32%` of the `822825E0` block body). This is
mostly callee-body work, not caller-side call/prolog overhead. Do not rerun
that exact capture unchanged. The next useful worker slice is a dynamic
block-slice report or equivalent HIR/PPC mapping for block-body guests such as
`8228252C`, followed by a default-off function/block-gated state-traffic or
lowering experiment from body-backed local spans.

`docs/research/20260524-025544-a64-entry-exit-profiler.md` now answers the
stackpoint/prolog question from the recursive `82282490` lane. Route-clean
capture `scratch/thor-debug/20260524-025000-*` shows generated
prolog/epilog/stackpoint attribution is tiny: `82282490` entry/exit was
`1.47` ticks/call and `0.93%` of body total; `82281D28` was `1.547`
ticks/call and `1.56%` of body total. Keep
`a64_enable_host_guest_stack_synchronization` default-on and treat the prior
stack-sync-off result as code-size evidence only. Do not run another unchanged
stack-sync or entry/exit A/B. The current best lane is exclusive attribution
inside `82282490` body/recursive child work, or fresh body-backed state-traffic
reduction.

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
That `822824F0` audit now exists and shows a mixed CR/call/vector/FPR block:
two calls, five barriers, `3` `mul_add`, `6` splats, `9` extracts, and three
`stvewx` stores. Next useful worker slice is lower-noise per-block body-time
attribution for `82282490`, or an A64 `stvewx` / `extract` / `splat` codegen
audit, before writing a peephole.
The A64 vector-shape audit now exists:
`docs/research/20260522-165526-82282490-vector-shape-audit.md`. Current result
on `82282490`: `extract=22`, `extract_dynamic=6`, `splat=152`,
`extract_then_splat=16`, `stvewx=6`, `stvewx_store1=6`,
`stvewx_dynamic_extract=6`, `mul_add=6`, `permute=381`,
`load_vector_shl=73`, and `load_vector_shr=64`. `822824F0` is the only
dynamic-hot vector block in the old block profile and carries `3` `stvewx`
dynamic extract/store shapes. Next useful worker slice is lower-noise
per-block body-time attribution for `82282490`, separating `822824F0` from
other entry-hot blocks before a default-off codegen peephole.
That profiler now exists:
`docs/research/20260522-171725-82282490-block-body-time-profiler.md`.
`arm64_speed_profile_block_body_time` is default-off; launch with
`-Arm64SpeedProfileBlockBodyTime true` and parse with
`tools/thor/thor_hir_block_mix_report.ps1`. The 17:09 capture
`scratch/thor-debug/20260522-170927-*` reached the visible opening route with no
searched fatal markers. Body-time says the next target is not `822824F0`:
`822825E0` dominates (`body_total=34726883`, peak `14525259`, peak
`ticks_per_entry=61`), then `822825C8` (`body_total=3216407`, peak `1041116`,
peak `ticks_per_entry=500`). `822824F0` is only third by body time
(`body_total=1280491`, peak `554835`, peak `ticks_per_entry=1`). Next useful
worker slice is no longer a stale `stvewx` peephole. The call-path audit now
exists: `docs/research/20260522-173542-82282490-call-path-audit.md`.
`tools/thor/thor_hir_call_path_report.ps1` found the charged body-time wall is
inclusive call work: `822825E0 -> 0x82282490` has
`charged_body_total=34726883`, and `822825C8 -> 0x8227FEE8` has
`charged_body_total=3216407`. A targeted `8227FEE8` dump in
`scratch/thor-debug/20260522-172738-*` idled before route progress and emitted
no callee dump. The default-off call-edge profiler now exists:
`docs/research/20260522-181040-a64-call-edge-profiler.md`, cvar
`arm64_speed_profile_call_edge_filter`, Thor flag
`-Arm64SpeedProfileCallEdgeFilter`. It found and fixed an `x9` callee-address
clobber in the first capture. Fixed edge capture
`scratch/thor-debug/20260522-175951-*` and same-APK control
`scratch/thor-debug/20260522-180335-*` both black-idled before `82282490`, so no
dynamic edge rows existed yet. The later call-edge audit-only note
`docs/research/20260522-190502-call-edge-audit-only-and-edge-proof.md`
changes that read. The old edge-only idle was missing the route-stabilizing
delayed `82282490` body-time filter. New default-off
`arm64_speed_profile_call_edge_audit_only` logs compile-time direct-call slot
counts without emitting generated edge counters. Audit-only plus delayed
body-time reached opening and logged `blocks=164`, `direct_call_edges=60`,
`instrumentation=0`; audit-only without delayed body-time black-idled before
`82282490`. Real call-edge profiling plus delayed body-time reached opening and
emitted dynamic edge rows. `822825E0 -> 82282490` dominates
(`body_ticks_total=21299726`, peak `7315115`), followed by
`822825C8 -> 8227FEE8` (`body_ticks_total=4515376`, peak `1933191`). Keep both
call-edge cvars default-off, but use `-Arm64SpeedProfileBodyTimeFilter` with
`82282490` and `-Arm64SpeedProfileBodyTimeAfterMs 120000` for future edge
captures on this route. Next useful slice is a focused `8227FEE8` HIR/body-time
capture or a recursive child-path audit from the proven edge rows.
Focused `8227FEE8` body-time evidence now exists:
`docs/research/20260522-193049-8227fee8-focused-capture.md`. Route-clean
capture `scratch/thor-debug/20260522-191427-*` reached visible opening with no
searched fatal markers and measured `8227FEE8` at
`body_ticks_total=3650429`, peak delta `1408271`, peak `ticks_per_entry=738`,
and code size `49804`; `82282490` remained dominant at
`body_ticks_total=25866736`. HIR was missing because filtered dumps were
info-level under the stable `log_level=1` lane; `-LogLevel 0` black-idled. The
filtered dump path now logs at warning level and Thor metadata records
`disassemble_function_filter`, but first patched filtered run
`scratch/thor-debug/20260522-192429-*` black-idled before route progress. Next
worker slice should not rerun that exact capture unchanged; prove patched APK
route safety with a no-filter control, do a control-sandwiched filtered capture,
or switch to offline guest/Ghidra analysis for `8227FEE8`.
The no-filter patched control is now also a black-idle:
`docs/research/20260522-230909-patched-route-control-black-idle.md` and
`scratch/thor-debug/20260522-230518-*`. It used no disassembly filter, delayed
body-time `82282490,8227FEE8`, and APK SHA
`89086669EF6CC19A028049FBF5957827CF7CFA97C85F0083021A739B9C16FFAF`, but
counters went flat from `23:06:05`, body-time activated with no target body
rows, and final screenshot was black. The idle snapshot could not acquire the
processor debug lock and reported `last_global_owner_sys_tid=21741`.
Idle owner attribution and patched route recheck now exist:
`docs/research/20260522-232945-a64-idle-owner-attribution.md`. The A64 idle
snapshot skip line now logs a lock-free native-TID hint (`owner_hint`, guest
thread ID, handle, and state) when the processor debug lock is busy, while
normal thread snapshots include `native=...`. Patched capture
`scratch/thor-debug/20260522-232133-*` reached visible opening with APK SHA
`E92DAC2CB4E7080C196DB9656305F372DC20C189E7697A2FCCD47D1E12DA3FA3` and no
searched fatal markers; the owner-hint path did not fire because the route did
not black-idle. Next worker slice should run a control-sandwiched filtered
`8227FEE8` capture with the delayed body-time route stabilizer. If it reaches
opening, use the warning-level HIR dump for codegen audit; if it black-idles,
inspect `owner_hint` before changing generated-code behavior.
The control-sandwiched filtered `8227FEE8` lane now has usable HIR but still
needs block attribution:
`docs/research/20260522-234847-8227fee8-filtered-hir-sandwich.md`.
Filtered capture `scratch/thor-debug/20260522-233545-*` reached visible
opening, emitted `8227FEE8` OptHIR at warning level, and had no searched fatal
markers. `8227FEE8` was secondary (`body_ticks_total=4125095`,
`ticks_per_entry=264`, code size `49804`) behind `82282490`
(`body_ticks_total=26728115`, `ticks_per_entry=120`). Its HIR is mostly
GPR/CR state traffic and barriers (`store_context=1426`,
`load_context=896`, `context_barrier=332`, `branches=284`, `calls=78`), plus
static `lvlx/lvrx/stvlx/stvrx` vector-load/store shapes in blocks `82280A68`
and `82280E1C`. Post-control `scratch/thor-debug/20260522-234038-*`
black-idled with clean fatal search and `owner_hint=miss` for
`last_global_owner_sys_tid=7347`. Next worker slice should run delayed
`8227FEE8` block body-time with
`-Arm64SpeedProfileBlockFilter 8227FEE8` and
`-Arm64SpeedProfileBlockBodyTime true` before any codegen experiment.
That delayed block body-time run black-idled before useful target rows:
`docs/research/20260523-001018-a64-owner-thread-id-attribution.md`.
`scratch/thor-debug/20260522-235449-*` had no searched fatal markers but no
`8227FEE8` body/block rows and a black screenshot. The idle owner line reported
`last_global_owner_sys_tid=14186` and `owner_hint=miss`. A diagnostic patch now
adds `last_global_owner_thread_id`, guest thread-ID/handle hint lookup, and
`owner_hint_source` / `owner_hint_sys_tid` fields. NativeCore and FullDeploy
passed; patched APK SHA is
`962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5`. Short
validation `scratch/thor-debug/20260523-000506-*` stayed active at the loading
spinner with clean fatal search, so the new owner fields are not exercised yet.
That repeat is now route-clean:
`docs/research/20260523-122718-8227fee8-block-body-time-route-clean.md`.
`scratch/thor-debug/20260523-122223-*` reached the visible opening sky/wing
route with clean fatal search and produced delayed `8227FEE8` body/block rows.
Final `8227FEE8` body row: `body_ticks_total=6914659`,
`body_ticks_delta=1871176`, `entries_delta=2041`, `ticks_per_entry=916`,
`code_size=71868`. Dynamic body-time inside `8227FEE8` is not the stale static
vector-heavy blocks; it is call-heavy block `822809F4`
(`body_total=1937201`, peak `665665`) calling `0x8227F1D8` and `0x8247BE20`.
Next worker slice should split those callees with delayed body-time and
`-Arm64SpeedProfileCallEdgeFilter 8227FEE8`, then choose a default-off
function-gated codegen experiment from the callee evidence.
That split now exists:
`docs/research/20260523-124029-8227fee8-callee-call-edge-split.md`.
Route-clean capture `scratch/thor-debug/20260523-123406-*` reached the visible
opening sky/wing route with no searched fatal markers. Final body-time made
`8227F1D8` the child target (`body_ticks_total=3714635`,
`ticks_per_entry=84`, `code_size=20180`), while `8247BE20` stayed tiny
(`body_ticks_total=27747`, `ticks_per_entry=2`, `code_size=796`). Dynamic
edge rows show `822809F4 -> 8227F1D8` at `body_ticks_total=2031295`, peak
delta `1137492`, and peak `ticks_per_call=216`. The next useful worker slice
is filtered `8227F1D8` HIR plus delayed body/block-time; no codegen patch
should start until that report names the actual hot shape.
That report now exists:
`docs/research/20260523-131338-8227f1d8-filtered-hir-block-body-time.md`.
Route-clean capture `scratch/thor-debug/20260523-130934-*` emitted
`8227F1D8` OptHIR and measured final `8227F1D8 body_ticks_total=4238549`.
Internal block body-time is dominated by block `8227F1D8`
(`body_ticks_total=4152240`, peak `1764204`), whose only direct calls are
`0x82490030` and `0x826BFC7C`. The next useful worker slice is
`-Arm64SpeedProfileCallEdgeFilter 8227F1D8` with delayed body-time for
`8227F1D8,82490030,826BFC7C`, not a local peephole.
That split now exists:
`docs/research/20260523-153235-8227f1d8-call-edge-split.md`.
Route-clean capture `scratch/thor-debug/20260523-152754-*` reached the visible
opening route with no searched fatal markers. Final dynamic rows identify
`8227F1D8 -> 82490030` as the child wall: parent
`8227F1D8 body_ticks_total=4117139`, child
`82490030 body_ticks_total=4007328`, edge `body_ticks_total=4054641`,
`calls_total=32107`, and `ticks_per_call=72`. `826BFC7C` did not show as a
meaningful dynamic row. The next useful worker slice is filtered `82490030`
HIR plus delayed body/block-time with `8227F1D8` as parent comparator.
The first filtered `82490030` attempt is a negative route-stability result:
`docs/research/20260523-154117-82490030-filtered-capture-black-idle.md`.
Capture `scratch/thor-debug/20260523-153726-*` black-idled before opening with
clean fatal search, no `82490030` HIR/body/block rows, and a black final
screenshot. The idle line reported a busy processor debug lock with
`last_global_owner_thread_id=F80002E8`, `owner_hint=hit`, and
`owner_hint_state=zombie`. Do not repeat that exact filtered run unchanged.
Next useful worker slice is a no-disassembly delayed body-time control for
`8227F1D8,82490030`; if that also black-idles, improve zombie owner/native TID
attribution before changing generated code.
That no-disassembly control also black-idled:
`docs/research/20260523-155012-82490030-control-black-idle.md`.
Capture `scratch/thor-debug/20260523-154626-*` had no disassembly filter and
used delayed body-time for `8227F1D8,82490030`, but still produced no target
body rows and ended black with a clean fatal search. The counters dropped to
`entry_delta=0` by `15:47:15` and stayed flat. The idle skip line again points
at `last_global_owner_thread_id=F80002E8`, `owner_hint=hit`,
`owner_hint_source=thread_id_or_handle`, and `owner_hint_state=zombie`; it also
logged `last_global_owner_sys_tid=20126` while `owner_hint_sys_tid=946810032`
looks stale. The next useful worker slice is not another unchanged 82490030
capture and not a codegen patch. Improve zombie owner/native TID attribution,
preferably by reporting whether `last_global_owner_sys_tid` is a live Android
thread under `/proc/<pid>/task/<tid>` and by separating that live native owner
from stale XThread hint fields.
That attribution patch is implemented and validated:
`docs/research/20260523-160650-a64-owner-native-tid-attribution.md`.
`Processor::OnThreadNativeStarted()` updates thread debug hints with the real
Linux `gettid()` after XThread startup, and the idle skip log now reports
`/proc/self/task` liveness for owner and hint TIDs. Capture
`scratch/thor-debug/20260523-160357-*` reproduced the black-idle on APK SHA
`2E3D88F46BB709AA3A869634C24219FBBA0568695C1F4902693132701CF9EBE5`: the hint
now matches by `system_tid` (`28245`), but that native task is gone and the
cached XThread state is `zombie`. The next useful worker slice is diagnostic
global critical-region ownership/lifetime attribution around the processor
debug lock, not another unchanged 82490030 capture and not generated-code
behavior.
That owner-source attribution patch now exists:
`docs/research/20260523-232053-global-critical-owner-source-attribution.md`.
It adds owner sequence, owner age, and owner source fields to the A64 idle skip
line and tags `Acquire`, `AcquireDirect`, `TryAcquire`, `XThread::LockApc`,
and `ObjectTable::LookupObject`. `NativeCore` and `FullDeploy` passed with APK
SHA `D68ED877D6C265420CD6853AB6C108C18F17E23911C2A1135016797D345C4823`.
Short validation `scratch/thor-debug/20260523-231636-*` stayed active for
100 seconds, ended at the loading overlay, and had a clean fatal search but no
idle-snapshot skip line. The next worker slice should run a longer
route-stabilized attribution capture; if it black-idles, inspect
`global_lock_owner_seq`, `global_lock_owner_age_ms`, and
`global_lock_owner_source`. If it stays active and reaches the visible route,
resume measured hotpath profiling rather than repeating stale `82490030`
captures.
The first long follow-up reproduced black-idle:
`docs/research/20260523-232908-global-owner-source-black-idle.md`.
Capture `scratch/thor-debug/20260523-232432-*` reported
`global_lock_owner_source='Acquire'`, `global_lock_owner_age_ms=25053`,
`global_lock_count=0`, dead native owner liveness, and zombie `owner_hint_state`.
That proves the next useful detail is a callsite tag. The follow-up patch in
`docs/research/20260523-234203-processor-thread-lifecycle-owner-tags.md` adds
`Acquire(source)` and labels processor thread lifecycle methods. `NativeCore`
and `FullDeploy` passed; APK SHA
`862F86C44625B460A5BAB8528E25AB4E946F52CDB30137D7479D24AC3BD50FCB`. Short
validation `scratch/thor-debug/20260523-233953-*` stayed active for 100 seconds
at loading with a clean fatal search and no idle-snapshot skip line. Next worker
slice should run a longer tagged-lifecycle attribution capture; if it black-idles,
use the specific `global_lock_owner_source` before changing lock behavior.
The longer tagged-lifecycle capture identified and fixed the blocker:
`docs/research/20260523-235800-object-release-outside-global-lock.md`.
Capture `scratch/thor-debug/20260523-234500-*` reproduced black-idle with
`global_lock_owner_source='Processor::OnThreadDestroyed'`. The fix defers
`object->Release()` in `ObjectTable::ReleaseHandle` and `RemoveHandle` until
after the object table/global critical-region lock is dropped. `NativeCore` and
`FullDeploy` passed with APK SHA
`FE5CBCF23E832807E51547D66387C3680DF6DB4B4802C9DB5BB64ACB46B8489B`.
Validation `scratch/thor-debug/20260523-235417-*` reached the visible sky/wing
opening route with clean fatal search, no idle-snapshot skip line, and body-time
rows for `8227F1D8` and `82490030`. Next worker slice should resume measured
hotpath profiling from the restored route unless fresh evidence reopens the
thread-destroy lock issue.
The next profiling split is now complete:
`docs/research/20260524-001152-82490030-call-edge-split.md`.
`scratch/thor-debug/20260524-000643-*` reached visible opening with clean fatal
search and no idle owner lines. Dynamic call-edge totals from `82490030` put
`82486178` first (`body_ticks_total=2302621`, `calls_total=33218`), followed
by `82485DD8` (`804023`) and `82486018` (`679426`). The next worker slice is a
filtered `82486178` HIR plus delayed body/block-time capture with `82490030`,
`82485DD8`, and `82486018` as comparators. Do not make a local `82490030`
codegen change first.
The `82486178` fastpath probe is now complete:
`docs/research/20260524-005156-82486178-vmx-copy-loop-fastpath.md`.
`arm64_blue_dragon_vmx_copy_loop_fastpath` is route-clean but remains
default-off. Inner-loop fastpath-on `scratch/thor-debug/20260524-004420-*`
reached visible opening with `82486178 body_ticks_total=2050839`; same-APK
control `scratch/thor-debug/20260524-004802-*` reached visible opening with
`82486178 body_ticks_total=1727468`. Do not repeat that exact A/B unchanged.
Next useful worker slice should inspect `82485DD8` and/or `82486018` with fresh
body/HIR/block evidence before another codegen shortcut.
The `82485DD8` word copy-loop fastpath probe is now a positive candidate:
`docs/research/20260524-011500-82485dd8-word-copy-loop-fastpath.md`.
`arm64_blue_dragon_word_copy_loop_fastpath` is route-clean and the profiled
same-APK A/B lowered `82485DD8` from `2307804` off to `695645` on, while
`82485E70` dropped from `1122726` off to `75310` on. Keep the toggle
globally default-off, but the quiet same-APK A/B in
`docs/research/20260524-013145-82485dd8-word-loop-quiet-ab.md` confirmed the
route-wide win for the Blue Dragon speed preset: `82490030` went from
`3923146` off to `3573598` on and `82485DD8` went from `1010794` off to
`774505` on, with matching visible opening screenshots and clean fatal search.
Do not combine it with the `82486178` VMX copy-loop toggle yet.
The follow-up preset rebaseline
`docs/research/20260524-014050-word-loop-preset-rebaseline.md` confirmed the
speed pack sends the word-loop toggle and moved the next target back to
`82282490`: final body-time `82282490=25383515`, `82281D28=7175068`,
`82490030=3480696`, `82486178=1358411`, `82485DD8=618538`, and
`82486018=457433`. The next worker slice should run delayed `82282490` block
body-time under the current preset, not more stale `82490030` child work.

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
