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

## Current Blue Dragon Target

Latest capture:
`docs/research/20260525-184957-edge-f1-kill-taxonomy.md`.
The default-off `arm64_blue_dragon_edge_variant_audit` now has route-clean,
behavior-neutral taxonomy counters for exact edge
`82282490:82282598 -> 82287788`: `eligible_calls=710511`,
`normal_fallbacks=710511`, `payload_materializations=0`,
`active_f1_reads=2121019`, and `active_call_kills=1781437`. Taxonomy counters
were `helper_preserved_calls=720290`, `child_preserved_calls=350636`,
`return_exit_calls=710511`, and `unknown_call_kills=0`. Active `f[1]` read
sites were `82287798=710511`, `82287828=350636`, `82287CF8=350031`,
`82287D10=350031`, `82287D8C=350031`, and `82287F1C=9779`; four audited read
PCs were zero. Active kill sites were `8228778C=710511`,
`82287854=350636`, `82287ED4=0`, `82287EDC=700732`,
`82287EE4=9779`, and `82288220=9779`.

Latest offline design:
`docs/research/20260525-190544-edge-payload-storage-design.md` adds
`tools/thor/thor_a64_edge_payload_storage_design.ps1`. It confirms the next
worker slice is a default-off, counter-only
`arm64_blue_dragon_edge_payload_storage_audit` skeleton for the same edge.
Keep `payload_materializations_allowed=0`, `behavior_changed=0`, normal-entry
fallback, no normal machine-code replacement, and no global indirection-slot
change. Count `f[1]` first, keep `fpscr` in the separate CFG-writeback lane,
keep `r[3]` count-only, and leave `lr` on the normal PPC call/return path. Do
not run a quiet speed A/B or materialize payload state until route-clean counter
rows prove useful hit volume and manageable flush pressure.

Latest capture:
`docs/research/20260525-193245-edge-payload-storage-audit-capture.md`.
The default-off `arm64_blue_dragon_edge_payload_storage_audit` skeleton now has
route-clean Thor proof for exact edge `82282490:82282598 -> 82287788`.
Behavior remains unchanged: `payload_materializations_allowed=0`,
`behavior_changed=0`, normal-entry fallback preserved, no normal machine-code
replacement, and no global indirection-slot change. The capture reached visible
opening sky / dragon-wing on APK SHA
`9DD345DCD8C404E0BDE50D3C67F72EF1CA105A2C9A4A3F7554462EF17BA567DF` with a
clean fatal-marker search.

Counters show useful volume but high lifetime pressure:
`eligible_edge_calls=910159`, `f1_active_reads_covered=2717039`,
`f1_unknown_kills=0`, `fpscr_dirty_writes=2849260`,
`fpscr_required_writebacks=922692`, `r3_mutable_writes=2718234`,
`context_barrier=11561589`, and `unknown_call=0`. Do not materialize payload
state or run a quiet speed A/B yet. The next worker slice should be a
counter-only payload lifetime/segment audit for the same edge, measuring usable
`f[1]` / `r[3]` reads before the first barrier, external visibility point,
return/exit, or required `fpscr` writeback.

Previous residual register-cache capture:
`docs/research/20260525-170307-arm64-register-cache-residual-audit-capture.md`
closed the immediate `r1` / `r11` behavior lane. Do not implement the stale
same-block `r1` clean-load replacement, `r11` dirty caching, store elision, or
a quiet speed A/B from that lane.

Hardware acceleration note:
`docs/research/20260525-171305-thor-hardware-acceleration-menu.md` is the
current Thor menu. Use NEON/dot/I8MM/CRC/LSE only after opcode/source/counter
proof, and push only Xenos-like bulk graphics transforms to Adreno after a
measured graphics-work audit finds CPU-side graphics cost. Do not move branchy
PPC/JIT state traffic to Vulkan compute while Main Thread remains the wall.

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

Latest lane closure:
`docs/research/20260525-170307-arm64-register-cache-residual-audit-capture.md`
records the FullDeploy plus route-clean Thor capture for the default-off
`arm64_guest_state_register_cache_residual_audit`. It reached the visible
opening sky / dragon-wing route on commit `85c422d2b`, APK SHA
`18097BA09F0C50596DED67D4251E4401FC7D458AA5EF7AAC7365B362D14CD19A`, with a
clean fatal-marker search. The residual counters show no post-promotion hit
volume: `candidate_loads=174`, `candidate_stores=139`,
`clean_hits_possible=0`, `dirty_hits_possible=0`, `normal_fallback=313`, and
per-slot `r1=107/11/0/0/118`, `r11=67/128/0/0/195`. Do not patch `r1` or
`r11` behavior from this lane. The next productive output should be a
caller-local or side-table edge-variant payload-storage audit/skeleton for
`82282490:82282598 -> 82287788`, keeping normal-entry fallback and counters
before any behavior change.

Latest lane closure:
`docs/research/20260525-160247-arm64-guest-state-register-cache-audit-capture.md`
records the route-clean Thor capture for the default-off
`arm64_guest_state_register_cache_audit`. Use `0x82282490` for the guest-PC
function filter; the old bare `82282490` spelling produced a route-clean run
with no audit rows. Corrected capture `scratch/thor-debug/20260525-155824-*`
reached the visible opening sky/dragon-wing route with a clean fatal-marker
search and logged behavior-neutral rows:
`candidate_loads=778`, `candidate_stores=285`,
`clean_hits_possible=357`, `dirty_hits_possible=247`,
`normal_fallback=1063`, `estimated_spill_pressure=2`,
`payload_materializations_allowed=0`, and `behavior_changed=0`.
Per-slot totals looked like `r1` was the safer first behavior lane, but
`docs/research/20260525-161334-r1-clean-load-source-gate.md` supersedes that:
the audit runs before `PromoteBlock`, and normal `PromoteBlock` already handles
same-block clean-load replacement. The residual scaffold in
`docs/research/20260525-162250-arm64-guest-state-register-cache-residual-audit.md`
is the current next step. Do not add store elision, `r11` dirty caching, payload
materialization, or a quiet speed A/B until the residual capture is route-clean
on Thor.

Previous lane closure:
`docs/research/20260524-184918-82287788-fpscr-cfg-writeback-plan.md`
adds `tools/thor/thor_hir_fpscr_cfg_writeback_plan.ps1`. It turns the
`82287788` `fpscr` dirty-cache lane into a no-behavior counter patch contract:
`fpscr_static_loads=26`, `fpscr_static_stores=26`,
`dynamic_context_access_upper=87946144`, `strict_same_window=0`,
`cfg_carrier_required=24`, `external_visibility_required=2`, and required call
writebacks at `82287ED4`, `82287EDC`, `82287EE4`, and `82288220`. Do not patch
`fpscr` behavior or run a quiet speed A/B yet. The next productive slice should
be a default-off `arm64_blue_dragon_fpscr_cfg_writeback_audit` counter-only
skeleton with normal entry behavior unchanged and zero payload materialization,
or a return to caller-local edge-variant storage for
`82282490:82282598 -> 82287788`.

Previous lane closure:
`docs/research/20260524-182632-a64-edge-variant-storage-plan.md`
adds `tools/thor/thor_a64_edge_variant_storage_plan.ps1`. It source-checks the
normal-entry singleton, guest-address-only indirection, direct-call normal
machine-code path, counter-only edge probe, and prior variant-storage design
requirement. For `82282490:82282598 -> 82287788`, the final route-clean counter
row still has `eligible_calls=675279`, `normal_fallback_share=100.00%`,
`indirection_fallbacks=675279`, `variant_misses=675279`,
`payload_materializations=0`, `storage_missing=1`, and `call_kills=1695703`
(`2.51` kills per eligible call). Do not patch generated behavior or run a
quiet speed A/B from the counter patch. The next productive slice should be a
default-off `caller-local_or_side-table_skeleton_counter_only` or a
`cfg_fpscr_writeback_audit_no_behavior_change`.

Previous lane closure:
`docs/research/20260524-181117-a64-edge-variant-counter-probe.md` adds the
default-off `arm64_blue_dragon_edge_variant_audit` path. The route-clean Thor
capture proved the hot edge is active and still entirely normal-entry /
indirection fallback, with zero payload materializations. Treat it as audit
evidence only, not a speed A/B target.

Previous lane closure:
`docs/research/20260524-165127-a64-edge-variant-design-audit.md`
adds `tools/thor/thor_a64_edge_variant_design_audit.ps1`. It source-checks the
normal entry and code-cache contracts and reports `normal_entry_singleton=true`,
`indirection_key=guest_address_only`, `machine_code_slot=single_per_guest_function`,
`resolve_path=normal_machine_code_only`, `current_call_abi=guest_return_only_in_x0`,
and `edge_variant_without_global_entrypoint=caller_local_or_side_table_required`.
For `82282490:82282598 -> 82287788`, do not patch generated behavior, replace
the normal `82287788` entry, change the global indirection slot, or run a
quiet speed A/B yet.

Previous lane closure:
`docs/research/20260524-163338-a64-state-carrier-abi-audit.md`
adds `tools/thor/thor_a64_state_carrier_abi_audit.ps1`. It source-checks the
A64 direct-call path and reports `current_direct_call_abi=guest_return_only_in_x0`,
`existing_f1_carrier_scope=callee_local_stack_slot`, and
`parent_to_callee_payload_status=blocked_without_call_abi_extension_or_edge_specific_entry`.
Do not make another quiet speed A/B from the current state-carrier evidence.
The next productive slice should add edge-specific ABI/variant design counters
or prove exact `fpscr` CFG writeback/flush rules before any behavior patch.

Previous lane closure:
`docs/research/20260524-145241-state-carrier-followup-plan.md`
adds `tools/thor/thor_hir_state_carrier_followup_plan.ps1`, an offline planner
over the latest route-clean Thor state-carrier row. It confirms `f[1]` is
dynamic but still not a narrow-patch lane (`3477646` reads, zero fallbacks,
`50.10%` helper-preserved, `49.90%` child-spanning, previous stack-slot A/B
not speed-positive, pair-entry seed-only too small). It also keeps `fpscr`
design-only (`3640919` dirty writes, `1182090` required writebacks,
`2924556` call kills). Do not patch generated behavior yet. Next work should
design a broader `82282490 -> 82287788` parent/callee carrier with
compile-time/runtime counters, or prove exact CFG writeback rules before any
quiet speed A/B.

Previous lane closure:
`docs/research/20260524-143436-state-carrier-audit-interval-fix.md`
records the route-clean Thor state-carrier audit after fixing
`tools/thor/thor_xenia_debug.ps1` to default
`Arm64SpeedProfileIntervalMs=15000` when row-producing speed-profiler
instrumentation is requested. Capture `scratch/thor-debug/20260524-143436-*`
reached the visible opening sky / dragon-wing route on APK SHA
`5A80DD15AF4471FABE16E7370D6CA5EB25FC8582993F0208BE98DB33EFCFA2FD`, had a
clean fatal-marker search, and logged the intended rows. Final counters:
`f1_read=3477646`, `f1_helper_read=1742466`, `f1_child_read=1735180`,
`f1_child_call=576589`, `f1_fallback=0`, `fpscr_read=3640919`,
`fpscr_dirty_write=3640919`, `fpscr_required_writeback=1182090`,
`fpscr_call_kill=2924556`, `fpscr_fallback=0`. This is audit evidence, not
quiet FPS proof. Do not repeat the no-interval or log-level-only captures and
do not patch behavior directly from this row. Next work should be an offline
parent/callee `82282490 -> 82287788` state-carrier design, especially a
broader `f[1]` carrier with explicit helper, child-call, barrier, exit,
exception, and fallback rules, or a CFG-aware fpscr dirty-carrier audit before
any speed A/B.

Previous lane closure:
`docs/research/20260524-141502-state-carrier-audit-route-capture.md`
is route-clean but data-blocked; `scratch/thor-debug/20260524-142827-*` proved
that `log_level=1` alone was not enough without a speed-profiler interval.

Previous lane closure:
`docs/research/20260524-133027-8228252c-state-carrier-design-audit.md`
adds deterministic tool `tools/thor/thor_hir_state_carrier_design_audit.ps1`.
Run it before any new `8228252C-822825C4` carrier behavior patch. It joins the
parent span, the `82282598 -> 82287788` callee HIR, and same-run edge rows,
then prints seed/kill/writeback requirements. Current output says `f[1]` is a
medium-risk read-only entry seed but too small alone, `fpscr` is the large
very-high-risk dirty candidate with writebacks at `82287ED4`, `82287EDC`,
`82287EE4`, and `82288220`, `r[3]` is mutable/high-risk, and `lr` is blocked.
The next useful output is a default-off counter/probe for the carrier plan, not
a behavior patch or speed A/B.

Previous lane closure:
`docs/research/20260524-131709-8228252c-state-carrier-design-constraints.md`
returns the sprint from closed narrow probes to a broader state-carrier design
problem. `8228252C-822825C4` remains the top body-backed local target
(`approx_exclusive=2876500`) and contains the hot `82282598 -> 82287788` edge
(`calls_total=1691272`). Parent `82282490` stores `r[3]`, `f[1]`, `fpscr`,
and `lr`; callee `82287788` loads all four. Do not patch another local PC fold,
standalone `f[1]` pair-entry thunk, or `fpscr` dirty-cache shortcut next:
`f[1]` has only one seed-load incremental ROI after the stack-slot carrier
A/B missed, `fpscr` needs exact call-visible dirty writebacks, `r[3]` is
mutable, and `lr` is call-link state. The next useful output is a deterministic
CFG/interprocedural state-carrier audit listing seed, kill, and writeback
requirements before any new behavior patch.

Previous lane closure:
`docs/research/20260524-121503-82282490-branch-loop-aggregate-audit.md`
closes the branch-loop detour. Across `82282490-822824B8`,
`822824B8-822824E8`, and `822825F4-82282600`, the loop head plus tail total
`2133128` local-exclusive ticks and four CR predicate stores, but only three
fallthrough-only GPR reloads, all in `822824B8-822824E8`. Entry/setup adds only
`249802` ticks and no reload opportunity. Do not patch CR store/barrier fusion
or a narrow branch-local GPR carrier next. Return to higher-traffic CFG-aware
or interprocedural state-carrier work, especially around `8228252C-822825C4`
and the hot `82282490 -> 82287788` state round-trip with exact flush rules.

Previous lane closure:
`docs/research/20260524-120704-822824b8-branch-state-audit.md` adds
`tools/thor/thor_hir_branch_state_audit.ps1`. For
`82282490:822824B8-822824E8`, the audit confirms local body time
(`body_ticks_total=1099164`) but closes the standalone patch: branch predicates
are also CR context stores across barriers, and the only GPR wins are three
fallthrough-only reloads (`r[11]`, `r[11]`, `r[31]`). The loop tail
`822825F4-82282600` has the same CR predicate-store shape and no reload
opportunity. Do not patch CR store/barrier or a narrow branch-local GPR carrier
next. Either broaden the branch-state audit across the `822824B8 <-> 822825F4`
loop and nearby branchy spans to prove aggregate GPR-carrier upside, or return
to higher-traffic CFG-aware/interprocedural state-carrier design.

Previous lane closure:
`docs/research/20260524-115538-8228252c-lane-closure-next-target.md`
closes the immediate narrow `82282490:8228252C-822825C4` lanes. The span still
has the best local absolute candidate score (`approx_exclusive=2876500`) and
heavy state/vector/FPR traffic, but the isolated sub-lanes have already missed
or need a broader carrier design: 13 candidate-dead pre-call stores have no HIR
DCE payoff, `f[1]` pair-entry only saves the seed context load after the
stack-slot carrier A/B missed, `fpscr` needs exact CFG/call writebacks,
all-three `stvewx` did not prove speed, and the three-PC `MUL_ADD_V128`
fastpath was mixed. Do not spend the next slice on another narrow
`8228252C-822825C4` PC-fold. The later branch-loop aggregate audit has now
closed the fresh `822824B8` detour too, so prefer a broader CFG-aware or
interprocedural state-carrier audit before any codegen behavior patch.

Previous lane closure:
`docs/research/20260524-114614-82287788-fpscr-dirty-cache-audit.md` adds
`tools/thor/thor_hir_fpscr_dirty_cache_audit.ps1`. It shows `fpscr` is too
cross-boundary for the next local speed patch: `82287788` has `loads=26`,
`stores=26`, and a large dynamic access upper, but `strict_same_window=0`;
24 store-to-next-load transitions require CFG/PHI or a stack carrier, and 2
require external call visibility with writebacks at `82287ED4`, `82287EDC`,
`82287EE4`, and `82288220`. Parent `82282490` is worse: all 12 transitions
cross external call visibility. Do not patch `fpscr` dirty caching next.
Return to broader body-backed `8228252C-822825C4` state/vector/FPR work, or
first build a generic CFG-aware dirty-state carrier audit.

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

Latest cross-span consumer audit:
`docs/research/20260524-062037-822824f0-cross-span-consumer-audit.md` extends
the same tool to print vector consumer rows. `822824F0-82282574` now shows the
full local chain: `lvlx/lvrx`, `vsldoi`, four stack-ish `lvx128` loads, `vor`,
three constant-lane `extract+splat` pairs, and three `vmaddfp` lowerings,
before lookahead reaches the sensitive `stvewx` store region. Keep
local-only `822824F0` peepholes closed. The next useful worker output is a
generated-instruction estimate/source-review for this exact chain, especially
`MUL_ADD_V128`, `PERMUTE_V128`, `LOAD_VECTOR_SHL`, and `EXTRACT/SPLAT`, then a
comparison against `8228252C-822825C4` before any default-off codegen patch.

Latest source-reviewed span estimate:
`docs/research/20260524-063109-82282490-span-instruction-estimate.md` adds a
`Source-Reviewed A64 Floor Estimate` to
`tools/thor/thor_hir_a64_codegen_audit.ps1`. It keeps `8228252C-822825C4` as
the better next local target by absolute exclusive time and estimated density:
`approx_exclusive=2876500`, `estimated_floor_total=192`, and
`approx_exclusive_ticks_per_estimated_instr=14981.77`, versus
`822824F0-82282574` at `approx_exclusive=2021174`,
`estimated_floor_total=164`, and `12324.23`. Do not patch local-only
`822824F0` vector codegen from this. Next worker output should be either a
default-off `MUL_ADD_V128` audit/source lane for `82282568/8228256C/82282570`,
or stronger `82282588` dynamic-`EXTRACT_I32` / `stvewx` provenance before
another lane-fold experiment.

Latest `82282588` stvewx provenance:
`docs/research/20260524-064118-82282588-stvewx-provenance.md` updates
`tools/thor/thor_hir_stvewx_lane_audit.ps1` with `-ProvenanceStartGuest` and
context-store forwarding. The provenance-aware audit proves
`82282588 -> lane 2` via `r6 = r1 + 0x50`, then `r6 + 0x8`, so the measured
span now has all three dynamic extract lanes proven: `82282580 -> lane 0`,
`82282584 -> lane 1`, and `82282588 -> lane 2`. Do not repeat the old
two-site A/B unchanged. If patching next, make a new default-off all-three-site
`EXTRACT_I32` fastpath with audit counters, then route-clean proof before a
quiet A/B. If it misses, switch to `MUL_ADD_V128` cost.

Latest all-three `stvewx` fastpath audit:
`docs/research/20260524-065602-blue-dragon-all-three-stvewx-fastpath.md`
extends the existing default-off
`arm64_blue_dragon_stvewx_stack_lane_fastpath` to all three proven sites.
`NativeCore` and `FullDeploy` passed, and capture
`scratch/thor-debug/20260524-065132-*` reached visible opening sky/dragon-wing
on APK SHA
`8A3DA22B2208AA67DE13C07383490F3BEBA14E2DC1538AAB5CE08FD035626771` with a
clean fatal-marker search. Audit counters ended at
`fastpath=722256/2008221 fallback=0/0`. Treat this as route-clean correctness
only, not speed proof, because audit counters and block body-time were enabled.
Keep the cvar default-off. Next worker output should be a quiet same-APK A/B
or control sandwich with audit off; if it is inconclusive again, move to
`MUL_ADD_V128` cost.

Latest all-three `stvewx` quiet A/B:
`docs/research/20260524-071045-blue-dragon-all-three-stvewx-quiet-ab.md`.
Control `scratch/thor-debug/20260524-070253-*` and fastpath-on
`scratch/thor-debug/20260524-070613-*` used the same commit/APK with audit,
body-time, block body-time, and disassembly off. Both reached matching opening
sky/dragon-wing frames with clean fatal-marker searches. The fastpath shrank
`82282490` code size by `120` bytes, but final `82282490` total entries were
identical at `183498`, final deltas were close, and final Main Thread stayed
around one full core. Treat this as no proven speed win. Keep the cvar
default-off. Next worker output should move to `MUL_ADD_V128` cost/source
audit for `82282568/8228256C/82282570`.

Latest `MUL_ADD_V128` source/cost audit:
`docs/research/20260524-072228-mul-add-v128-source-cost-audit.md` updates
`tools/thor/thor_hir_a64_codegen_audit.ps1` with a dedicated source-cost
section. Both `822824F0-82282574` and `8228252C-822825C4` hit the same three
`vmaddfp` PCs: `82282568`, `8228256C`, and `82282570`. `8228252C-822825C4`
is still the larger local target (`approx_exclusive=2876500`,
`mul_add_v128=3`). Do not patch behavior yet. The current lowering is heavy:
VMX FPCR mode, optional denormal flushes, scratch source saves, `fmla`, PPC
NaN fast-path/repair, optional output flush, and dest copy. x64 also warns
against assuming host FMA is semantically equivalent because its fused path is
disabled for tests. Next worker output should be a default-off runtime audit
for those three PCs, counting denormal flush need, NaN-fixup entry/per-lane
repair, FPCR mode switches, and source/dest alias copies before a shortcut.

Latest `MUL_ADD_V128` runtime audit patch:
`docs/research/20260524-074442-blue-dragon-mul-add-v128-runtime-audit.md`
adds default-off cvar `arm64_blue_dragon_mul_add_v128_audit`, gated to Blue
Dragon guest function `82282490` and PCs `82282568`, `8228256C`, and
`82282570`. It logs total/per-PC calls, FPCR switches, software flush-path
eligibility, input/output denormal sightings, NaN-fixup entry and repaired
lanes, and source/dest vector copies. `tools/thor/thor_xenia_debug.ps1` now
accepts `-Arm64BlueDragonMulAddV128Audit`; presets keep it default-off.
`NativeCore` passed. Next worker output should be `FullDeploy` plus a
route-stabilized delayed `82282490` body-time speed capture with the audit
enabled, then either a guarded fastpath if expensive paths are cold or broader
`8228252C-822825C4` state/vector/FPR work if they are hot.

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

Latest current worker target:
`docs/research/20260524-113615-82287788-pair-entry-roi.md`.
The function-pair audit now has `-CompareExistingStackCarrier`. The pair-entry
`f[1]` candidate is clean, but the incremental win over the already-tested
stack-slot carrier is only one context load per call
(`pair_incremental_static_upper=1691272`) because the stack-slot carrier
already replaces nine reloads. Do not spend the next worker slice implementing
a standalone `f[1]` pair-entry behavior patch. Move to broader
`8228252C-822825C4` state-cache/callee-variant design or an `fpscr`
dirty-cache audit with exact writeback rules.

Previous current worker target:
`docs/research/20260524-112944-82287788-function-pair-variant-audit.md`.
Use `tools/thor/thor_hir_function_pair_variant_audit.ps1` before implementing
a pair-specific callee entry or thunk. For `82282490:82282598 -> 82287788`,
the `f[1]` lane is statically clean for a specialized entry seed:
`loads=10`, `stores=0`, `replaceable_loads=10`, `unsafe_loads=0`, and
`static_replace_upper=16912720`. The `fpscr` control is blocked by callee
writes. The next worker slice should source-design a default-off
pair-specific thunk/entry seed or compile-time caller-seeded callee-slot proof
for `f[1]`; do not alter normal `82287788` entry semantics or try to squeeze
this through the generic `A64Emitter::Call` ABI without a design.

Previous current worker target:
`docs/research/20260524-111940-8228252c-state-forwarding-plan.md`.
The interprocedural audit now prints a forwarding plan for the live
`82282490 -> 82287788` direct-call state round-trip. The edge remains hot
(`calls_total=1691272`, `body_ticks_total=5653971`). `f[1]` is read-only in
the callee (`loads=10`, `stores=0`, `static_load_upper=16912720`) and is the
only medium-risk candidate, but the previous stack-slot fastpath did not prove
speed. The next worker slice should be a function-pair/callee-variant design
audit for `f[1]`, with parent context visibility preserved unless a visibility
model proves it can move. Do not patch `fpscr` yet despite the larger static
upper bound (`43973072` loads and stores): it is mutable and needs exact dirty
writeback at calls, barriers, exits, exceptions, and readers. Do not forward
`r[3]` or `lr` from this evidence.

Previous current worker target:
`docs/research/20260524-110545-blue-dragon-f1-carrier-fastpath-ab.md`.
The default-off `arm64_blue_dragon_f1_carrier_fastpath` probe is implemented
for the audited `82287788` `f[1]` load sites. It seeds a function-local stack
slot at `82287798` and reuses it only at the helper/`821CE028`-preserved PCs.
NativeCore and FullDeploy passed. Audit capture
`scratch/thor-debug/20260524-105424-*` reached visible opening sky/dragon-wing
with a clean fatal-marker search on APK SHA
`A2A59F7777F77B52526ABF5E401194A4A7490D71B4F7A878ED8099B0CA7557A9`.
Final counters were `seed=223805/688280` and `reuse=443749/1365365`; final
body-time still had `82282490` first and `82287788` cheap at
`body_ticks_total=3636251`, `ticks_per_entry=5`. Quiet same-APK A/B control
`scratch/thor-debug/20260524-105813-*` versus fastpath-on
`scratch/thor-debug/20260524-110156-*` was route-clean but did not prove speed:
control reached a later opening frame, `82287788` code size changed
`35568 -> 35572`, and the main wall stayed outside this narrow callee lane.
Keep `arm64_blue_dragon_f1_carrier_fastpath` and
`arm64_blue_dragon_f1_carrier_audit` default-off and out of presets. Do not
repeat this exact A/B unchanged. Next worker slice should move to broader
`8228252C-822825C4` parent/callee state/vector/FPR reduction with a fresh
offline audit before any Thor behavior patch.

Previous current worker target:
`docs/research/20260524-104145-blue-dragon-f1-carrier-runtime-audit.md`.
The default-off `arm64_blue_dragon_f1_carrier_audit` probe is implemented and
route-clean. NativeCore and FullDeploy passed. Capture
`scratch/thor-debug/20260524-103438-*` reached visible opening sky/dragon-wing
with a clean fatal-marker search on APK SHA
`514C6A8C0C14820BFE0ED48CF280D00371B30602AEEE5CD86AE06A82E05D139F`.
Final counters were `total=755910/2159587`,
`helper_preserved=379058/1081707`, and
`child_preserved=376852/1077880`; final body-time still had `82282490` first
and `82287788` high-entry but cheap at `body_ticks_total=3578711`,
`ticks_per_entry=5`. Keep the audit cvar default-off and out of presets. The
later fastpath A/B supersedes this as the current decision.

Previous current worker target:
`docs/research/20260524-102113-821ce028-f1-clobber-audit.md`. The `821CE028`
child call no longer needs to be treated as an unknown `f[1]` clobber for the
parent `82287788` carrier lane. Reusing
`tools/thor/thor_hir_fpr_carrier_safety_audit.ps1` on `821CE028` offset `296`
reports `target_loads=0`, `target_stores=0`, and
`decision=no_target_context_access_observed`; the only calls are return
`call_indirect.6` rows. Hotpath report shows only `f[0]` context traffic, not
`f[1]` or `fpscr`. Do not patch `821CE028`. Next worker slice should add a
default-off `82287788` runtime carrier audit/probe that treats
`__savegprlr_28`, `__restgprlr_28`, and direct calls to `0x821CE028` as
`f[1]`-preserving, then counts dynamic replacement opportunities for all 10
static `f[1]` loads before a quiet A/B.

Previous current worker target:
`docs/research/20260524-101421-82287788-f1-carrier-safety-audit.md`.
`tools/thor/thor_hir_fpr_carrier_safety_audit.ps1` now audits whether a target
FPR context value can be carried through helper/unknown calls. For `82287788`
offset `296` (`f[1]`), it found `target_loads=10`, `target_stores=0`,
`helper_whitelist=2`, and `unknown_call_blocked=8`. Source review says
`__savegprlr_28` / `__restgprlr_28` are GPR/LR-only helpers and should not
clobber FPR, but the only eligible loads are `82287798` and `82287828`.
The remaining `f[1]` loads cross real child calls to `0x821CE028`. Do not
implement a speed patch yet. Next worker slice should either add a default-off
runtime counter for dynamic helper-whitelist versus unknown-call-blocked
`f[1]` hits, or audit `821CE028` for `f[1]` clobber/use. Keep `fpscr` out of
this lane.

Previous current worker target:
`docs/research/20260524-100409-82287788-callee-local-promotion-audit.md`.
`tools/thor/thor_hir_callee_local_promotion_audit.ps1` now classifies strict
callee-local promotion windows. It breaks windows on labels, context barriers,
calls, branches, and returns. For callee `82287788`, `f[1]` is read-only
(`loads=10`, `stores=0`) but has `strict_redundant_loads=0`; its useful
`9` reload opportunities cross boundaries and `2` cross calls. `fpscr` is
mutable (`loads=26`, `stores=26`) and has no strict local wins. Do not build a
blind callee-local cache. Next worker slice should design or audit a
default-off CFG/function-pair `f[1]` carrier probe for `82282490 -> 82287788`
with explicit call-clobber, helper, exit, exception, barrier, and alias flush
rules. Treat `fpscr` as high-risk unless a source review proves exact dirty
update forwarding.

Previous current worker target:
`docs/research/20260524-095137-82282598-82287788-state-roundtrip-audit.md`.
`tools/thor/thor_hir_interproc_state_roundtrip_audit.ps1` now joins a parent
filtered HIR dump, callee filtered HIR dump, and optional dynamic call-edge rows
for direct-call state traffic. For `82282490` call PC `82282598 -> 82287788`,
the edge is hot (`calls_total=1691272`, `body_ticks_total=5653971`). Parent
`82282490` stores `r[3]`, `f[1]`, `fpscr`, and `lr` before or at the call, and
callee `82287788` loads all four tracked fields (`f[1] loads=10`,
`fpscr loads=26/stores=26`). Do not skip these live stores. Next worker slice
should design or audit a default-off Blue-Dragon/function-pair carrier or
callee-local promotion probe for `82282490 -> 82287788`, with explicit
helper/exit/exception/alias flush rules and route proof before any quiet A/B.

Previous current worker target:
`docs/research/20260524-094213-hir-dead-state-store-dce-audit.md`.
`tools/thor/thor_hir_dead_state_store_dce_audit.ps1` now tests whether moving
targeted `store_context` suppression into HIR would let
`DeadCodeEliminationPass` remove upstream work. Exact
`BlueDragonCallBoundaryDead` suppression for `82282490:8228252C-822825C4`
shows `target_store_context=13` and `dead_assignments=0`, so do not implement
a HIR-level repeat of the same `13`-store skip. Unsafe all-span removal shows
an upper bound of `22` dead assignments, mostly the live `f[1]` / `fpscr`
argument path before `82282598 -> 82287788`; focused `82287788` HIR proves the
callee loads and uses `f[1]` and `fpscr`. Next worker slice should audit
interprocedural live argument/state round-trips for `82282598 -> 82287788`
instead of another backend dead-store skip.

Previous current worker target:
`docs/research/20260524-093001-blue-dragon-call-boundary-store-suppression-probe.md`.
The exact `13`-site call-boundary store suppression probe is implemented and
route-clean, but not speed-proven. Route-safety capture
`scratch/thor-debug/20260524-091813-*` reached visible opening sky/dragon-wing
with clean fatal-marker search and proved `suppressed=dead=9007635` while
live-in stores stayed separate. Quiet same-APK A/B
`scratch/thor-debug/20260524-092153-*` control versus
`scratch/thor-debug/20260524-092523-*` suppression-on was route-clean and
shrunk `82282490` only `86828 -> 86768`; final route counters and thread
samples did not prove speed. Keep
`arm64_blue_dragon_call_boundary_state_suppress_dead_stores` default-off and
do not repeat the exact same A/B next. Next worker slice should find broader
`8228252C-822825C4` state/vector/FPR reduction or a larger state-cache/lowering
change from fresh offline evidence.

Previous worker target:
`docs/research/20260524-085451-blue-dragon-call-boundary-state-runtime-audit.md`.
`arm64_blue_dragon_call_boundary_state_audit` is now a default-off runtime
counter with Android/Thor plumbing. Capture
`scratch/thor-debug/20260524-084805-*` reached visible opening sky/dragon-wing
with clean fatal-marker search and proved the expected dynamic ratio:
`675279` boundary hits, `13` candidate-dead stores per hit
(`dead=8778627`), and `4` live-in stores per hit (`live=2701116`). This is
audit proof only, not speed proof. Next worker slice should implement a
separate default-off Blue-Dragon/function/PC-gated store-suppression probe for
only the `13` candidate-dead sites and prove route safety before any quiet A/B.
Do not skip the live-in `r[3]`, `f[1]`, `fpscr`, or `lr` stores.

Previous worker target:
`docs/research/20260524-082324-blue-dragon-mul-add-v128-fastpath-ab.md`.
The narrow three-PC `MUL_ADD_V128` fastpath is route-clean and code-size
positive but not speed-proven. It shrank `82282490` from `86828` to `85424`
bytes in quiet A/B, but final counters and thread samples were mixed. Keep
`arm64_blue_dragon_mul_add_v128_fastpath` default-off and out of presets. Next
worker slice should move to broader `8228252C-822825C4` local-span
state/vector/FPR reduction unless fresh evidence reopens this FMA lane.

Previous worker target:
`docs/research/20260524-080156-blue-dragon-mul-add-v128-audit-capture.md`.
The Android launch bridge for `arm64_blue_dragon_mul_add_v128_audit` is fixed
and route-clean capture `scratch/thor-debug/20260524-075713-*` produced real
runtime counters for `82282490` PCs `82282568`, `8228256C`, and `82282570`.
Final counters were `total=722256/2131533`, each PC `240752/710511`,
`fpcr_switch=240752/710511`, and zero observed software-flush, input-denormal,
output-denormal, NaN-entry, or NaN-lane paths. Next worker slice should
implement one default-off Blue-Dragon/function/PC-gated `MUL_ADD_V128`
fastpath for only those three PCs, then prove route safety before any quiet
same-APK A/B.

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
