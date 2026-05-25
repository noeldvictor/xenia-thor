---
name: xenia-blue-dragon-full-speed-autopilot
description: Use when Codex should automatically continue the xenia-thor Blue Dragon full-speed sprint across Stop-hook continuations and Codex Desktop heartbeats until AYN Thor reaches the full-speed proof marker or a concrete blocker is recorded.
---

# Xenia Blue Dragon Full-Speed Autopilot

This is the repo-local autopilot contract for the user's Blue Dragon goal. Use
it when the user says to keep going, continue automatically, do not stop until
full speed, or asks how Codex should keep the conversation alive.

## Non-Negotiables

- Work on `master` only.
- Keep this fork framed as an unofficial, unstable research experiment.
- Use only repo-local skills for new project-specific behavior.
- Keep the Stop hook bounded; never create an unbounded infinite loop.
- Update dated worklogs and research notes for every meaningful work slice.
- Commit and push validated progress often.
- Do not commit game files, extracted XEX files, keys, private screenshots, or
  raw copyrighted content.

## Arm The Conversation

First check whether the proof marker already exists:

```powershell
Test-Path scratch\thor-debug\latest-blue-dragon-full-speed-proof.json
```

If proof does not exist, arm the active-session Stop hook:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode EnableBlueDragonFullSpeed -MaxAttempts 48
```

Then check the hook config:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_codex_goal_loop.ps1 -Mode Status
```

The expected config is:

- `goal_name`: `blue-dragon-full-speed`
- `proof_path`: `scratch/thor-debug/latest-blue-dragon-full-speed-proof.json`
- `enabled`: `true`

## Heartbeat Use

Use the Codex Desktop heartbeat automation as a later-wakeup safety net, not as
the tight inner loop. The active project Stop hook should do the immediate
continuation when the chat is live.

The heartbeat id is:

```text
blue-dragon-full-speed-sprint
```

If the heartbeat exists, keep its prompt aligned with this skill. If the proof
marker exists, summarize proof and recommend deleting or pausing the heartbeat.

## Full-Speed Proof

Only stop the autopilot for success after writing:

```text
scratch/thor-debug/latest-blue-dragon-full-speed-proof.json
```

The proof JSON must include:

- commit hash;
- APK SHA-256;
- Blue Dragon launch path on the Thor SD card;
- capture directory;
- speed logcat path;
- screenshot path;
- launch mode and cvars;
- visible route label: `title`, `opening`, or `gameplay`;
- quiet speed-capture evidence;
- sustained FPS evidence for at least a representative 180-second segment;
- searched fatal-marker result.

Treat `>=30 FPS` as the first full-speed proof target for this route. Track
`60 FPS` as a stretch target when the title and route allow it.

## Each Continuation

At the start of every continuation:

1. Read `AGENTS.md`.
2. Read this skill, `.agents/skills/xenia-working-fast-autonomy/SKILL.md`,
   and `.agents/skills/xenia-codex-goal-loop/SKILL.md`.
3. Read the latest `docs/worklogs/YYYYMMDD.md`.
4. Check `scratch/thor-debug/codex-goal-loop.json`.
5. Check for the proof marker.
6. Run `git status --short --branch`.

If proof is absent, use
`.agents/skills/xenia-working-fast-autonomy/SKILL.md` as the worker loop for
the continuation, then pick exactly one next lane:

- use `.agents/skills/xenia-a64-speed-hotpath/SKILL.md` for FPS and generated
  CPU hot paths;
- use `.agents/skills/aarch64-snapdragon-jit-port/SKILL.md` for backend parity,
  thunks, code cache, generated code, or ARM64 lowering;
- use `.agents/skills/xbox360-xenon-ppc-architect/SKILL.md` for guest PPC,
  kernel/HLE, XEX imports, or Ghidra guest analysis;
- use `.agents/skills/xenia-vulkan-adreno-renderdoc/SKILL.md` for GPU,
  presenter, PM4, Vulkan, RenderDoc, or Adreno evidence;
- use `.agents/skills/xenia-thor-experiment-gate/SKILL.md` before risky
  behavior changes.
- use `.agents/skills/xenia-continual-harness-refiner/SKILL.md` when the
  autopilot is repeating stale work or a new capture requires prompt/skill/tool
  updates before the next slice.

## Current Best Next Move

Strategic override when the sprint is repeating narrow fastpaths:
`docs/research/20260525-143937-mature-a64-emulator-backend-patterns.md`
compares mature AArch64 emulator backends and updates the local rule: if the
latest Thor capture still shows Main Thread/A64 generated-code as the wall,
run the mature backend gate in
`.agents/skills/xenia-a64-speed-hotpath/SKILL.md` before another risky
title-specific codegen experiment. Keep Vulkan as a parallel evidence lane only
until GPU Commands/present/submit/pipeline stalls overtake the CPU/JIT wall.

Latest priority, superseding the older chronology below:
`docs/research/20260525-150348-82282490-82287788-register-cache-gap.md`
adds `tools/thor/thor_a64_register_cache_gap_report.ps1`. It applies the mature
A64 backend gate to the current `82282490 -> 82287788` wall: `82282490` has
`GPR=1108`, `VMX=528`, `CR=345`, `whole_gpr_loads=546`,
`whole_gpr_stores=562`, and `context_barriers=213`, while source exposes only
seven allocatable A64 GPRs (`x22-x28`) after fixed `x19` backend context,
`x20` PPC context, and `x21` membase. The hot edge still has
`calls_total=1691272`, but `r[3]`, `fpscr`, and `lr` are too visible/mutable
for a quick store/load deletion, and the route-safe `f[1]` carrier was not a
speed win. Do not implement another narrow context-load fold from this evidence.
Next useful slice is a default-off guest-state register-cache audit/design
skeleton or caller-local/side-table edge-variant payload storage with
normal-entry fallback and payload counters.

Freshest priority:
`docs/research/20260525-152958-a64-guest-state-cache-design.md` adds
`tools/thor/thor_a64_guest_state_cache_design.ps1`. The tool confirms there is
no structural guest-state register-cache skeleton yet and selects the
HIR/context-promotion boundary as the safest no-behavior insertion point. The
next useful code slice is a default-off counter-only
`arm64_guest_state_register_cache_audit`, function-filtered for the hot route,
with no store elision, no context-load replacement, and no payload
materialization. Count clean hit opportunities, dirty opportunities, misses,
flushes, external visibility kills, and spill-pressure estimates for clean
INT64 `r[1]` / `r[11]` before any behavior patch or quiet speed A/B.

Latest Thor audit capture:
`docs/research/20260525-160247-arm64-guest-state-register-cache-audit-capture.md`
records the FullDeploy plus route-clean Thor audit capture for
`arm64_guest_state_register_cache_audit`. Use `0x82282490` for the guest-PC
function filter; the old bare `82282490` spelling reached the route but emitted
no audit rows. Corrected capture `scratch/thor-debug/20260525-155824-*` used
commit `3b02ff758`, APK SHA
`4ED7B6FE876F5ADCB7D11F70BDC42AC4D7A1BF916D680E4B22BEA861069440D6`, reached
the visible opening sky/dragon-wing route, and had a clean fatal-marker search.
Audit rows were behavior-neutral with `payload_materializations_allowed=0` and
`behavior_changed=0`: `candidate_loads=778`, `candidate_stores=285`,
`clean_hits_possible=357`, `dirty_hits_possible=247`, `normal_fallback=1063`,
and `estimated_spill_pressure=2`. Per-slot totals favor a narrow `r1` lane:
`r1=433/11/322/4/444` and `r11=345/274/35/243/619` for
loads/stores/clean_hits/dirty_hits/fallback. Follow-up source gate
`docs/research/20260525-161334-r1-clean-load-source-gate.md` supersedes the
same-block `r1` prototype idea: the audit runs before `PromoteBlock`, and
`PromoteBlock` already rewrites same-block `LOAD_CONTEXT` to `ASSIGN` when a
prior in-block context value exists. Next useful slice is a default-off
post-`PromoteBlock` residual audit for `0x82282490`, with behavior unchanged,
to count remaining `r1` / `r11` load/store traffic, residual clean/dirty
opportunities, boundary flush reasons, and spill pressure before any behavior
patch or quiet speed A/B. The final capture remained CPU/JIT-heavy
(`82282490=31080764`, Main Thread `100%`, GPU Commands `7.6%`), so do not
pivot to broad Vulkan work.

Latest residual-audit scaffold:
`docs/research/20260525-162250-arm64-guest-state-register-cache-residual-audit.md`
implements default-off `arm64_guest_state_register_cache_residual_audit` plus
`arm64_guest_state_register_cache_residual_audit_function`, Android forwarding,
and Thor flags `-Arm64GuestStateRegisterCacheResidualAudit` /
`-Arm64GuestStateRegisterCacheResidualAuditFunction`. NativeCore and ApkShell
passed. Next useful slice is FullDeploy plus a route-clean Thor capture with
`-Arm64GuestStateRegisterCacheResidualAudit true` and
`-Arm64GuestStateRegisterCacheResidualAuditFunction 0x82282490`, keeping
delayed body-time comparators `82282490,82281D28,82287788`. Do not patch
generated behavior or run a quiet speed A/B until the residual row proves
material post-promotion `r1` / `r11` traffic.

Previous implemented scaffold:
`docs/research/20260525-154617-arm64-guest-state-register-cache-audit.md`
adds the default-off `arm64_guest_state_register_cache_audit` and Thor launch
plumbing. NativeCore and ApkShell passed. This remains counter-only:
normal fallback is unchanged and `payload_materializations_allowed=0`. The next
useful slice is a FullDeploy plus route-clean Thor audit capture scoped to
`82282490`, using `-Arm64GuestStateRegisterCacheAudit true` and
`-Arm64GuestStateRegisterCacheAuditFunction 0x82282490`; do not patch
register-cache behavior or run a quiet speed A/B until those audit rows prove
the hit volume and flush/spill pressure.

Previous priority:
`docs/research/20260524-184918-82287788-fpscr-cfg-writeback-plan.md`
adds `tools/thor/thor_hir_fpscr_cfg_writeback_plan.ps1`. It wraps the prior
dirty-cache audit and turns `fpscr` into an exact no-behavior counter contract:
`fpscr_static_loads=26`, `fpscr_static_stores=26`,
`dynamic_context_access_upper=87946144`, `strict_same_window=0`,
`cfg_carrier_required=24`, `external_visibility_required=2`, and required
writeback call PCs `82287ED4`, `82287EDC`, `82287EE4`, and `82288220`.
Do not patch `fpscr` behavior and do not run a quiet speed A/B yet. The next
useful slice is a default-off `arm64_blue_dragon_fpscr_cfg_writeback_audit`
counter-only skeleton with `normal_entry_behavior=unchanged` and
`payload_materializations_allowed=0`, or a return to caller-local edge-variant
storage for `82282490:82282598 -> 82287788`.

Previous priority:
`docs/research/20260524-182632-a64-edge-variant-storage-plan.md`
adds `tools/thor/thor_a64_edge_variant_storage_plan.ps1`. It joins the latest
route-clean edge-variant counter row, singleton-entry/indirection source audit,
interprocedural `f[1]`/`fpscr` evidence, and the `fpscr` dirty-cache audit. For
exact edge `82282490:82282598 -> 82287788`, source checks are all ok and the
dynamic row still says `eligible_calls=675279`,
`normal_fallback_share=100.00%`, `indirection_fallbacks=675279`,
`variant_misses=675279`, `payload_materializations=0`, `storage_missing=1`,
and `call_kills=1695703` (`2.51` kills per call). Do not patch generated
behavior yet and do not run a quiet speed A/B from the current counter patch.
The next useful slice is either a default-off
`caller-local_or_side-table_skeleton_counter_only` or a
`cfg_fpscr_writeback_audit_no_behavior_change`.

Previous priority:
`docs/research/20260524-181117-a64-edge-variant-counter-probe.md` adds the
default-off `arm64_blue_dragon_edge_variant_audit` path and Thor launcher flag.
Route-clean capture `scratch/thor-debug/20260524-180737-*` reached visible
opening sky/dragon-wing with a clean fatal-marker search on APK SHA
`25BEA1C91D7F9D6F57F808A1C8CC91F8115E485E3FDA491D2D81EB600B5F7B97`. It proved
the hot edge is active but only as audit evidence: no payload was materialized,
all calls used normal-entry/indirection fallback, and no generated behavior
changed. Do not run a quiet speed A/B from this patch.

Previous priority:
`docs/research/20260524-165127-a64-edge-variant-design-audit.md`
adds `tools/thor/thor_a64_edge_variant_design_audit.ps1`. It source-checks the
normal entry, direct-call, unresolved-call, code-cache, indirection,
`A64Function`, and `GuestFunction` contracts and reports
`normal_entry_singleton=true`, `indirection_key=guest_address_only`,
`machine_code_slot=single_per_guest_function`,
`resolve_path=normal_machine_code_only`, and
`edge_variant_without_global_entrypoint=caller_local_or_side_table_required`.
For exact edge `82282490:82282598 -> 82287788`, do not preload a spare
register, replace `82287788`'s normal machine-code pointer, or change the
global indirection slot.

Previous priority:
`docs/research/20260524-163338-a64-state-carrier-abi-audit.md`
adds `tools/thor/thor_a64_state_carrier_abi_audit.ps1`, a source audit of the
current A64 direct-call contract. It confirms the direct-call ABI is
`guest_return_only_in_x0`, the existing `f[1]` carrier is only a
callee-local `82287788` stack slot, and a broader `82282490 -> 82287788`
payload is blocked until there is either an explicit call-ABI extension or an
edge-specific compiled entry/variant with normal-entry fallback. Do not run a
quiet speed A/B or patch generated behavior from the current carrier evidence.
Next useful work is an edge-specific ABI/variant design audit with
compile-time/runtime counters, or exact `fpscr` CFG writeback proof.

Previous priority:
`docs/research/20260524-145241-state-carrier-followup-plan.md`
adds `tools/thor/thor_hir_state_carrier_followup_plan.ps1`, an offline planner
that parses the latest route-clean `A64 Blue Dragon state-carrier design audit`
row and joins it with the prior static audits. It confirms `f[1]` has real
volume (`3477646` reads, zero fallbacks), but the helper/child split is
`50.10%` / `49.90%` and previous stack-slot plus pair-entry evidence makes
another narrow `f[1]` behavior patch a bad next move. It also keeps `fpscr`
audit-only: `3640919` reads/dirty writes, `1182090` required writebacks, and
`2924556` call kills mean a shortcut needs exact CFG writeback rules first. Do
not patch generated behavior yet. Next useful slice should design a broader
`82282490 -> 82287788` parent/callee carrier with compile-time/runtime counters,
or prove exact CFG writeback rules before any quiet speed A/B.

Previous priority:
`docs/research/20260524-143436-state-carrier-audit-interval-fix.md`
records the route-clean Thor state-carrier audit after fixing the capture lane
to auto-enable `Arm64SpeedProfileIntervalMs=15000` when row-producing profiler
instrumentation is requested. Capture `scratch/thor-debug/20260524-143436-*`
reached the visible opening sky / dragon-wing route on APK SHA
`5A80DD15AF4471FABE16E7370D6CA5EB25FC8582993F0208BE98DB33EFCFA2FD`, had a
clean fatal-marker search, and logged the intended rows. Final counters:
`f1_read=3477646`, `f1_helper_read=1742466`, `f1_child_read=1735180`,
`f1_child_call=576589`, `f1_fallback=0`, `fpscr_read=3640919`,
`fpscr_dirty_write=3640919`, `fpscr_required_writeback=1182090`,
`fpscr_call_kill=2924556`, `fpscr_fallback=0`. Final body-time kept
`82282490` dominant at `61133316`, with `82281D28=13198164` and
`82287788=7477399`; Main Thread remained about one full core while GPU Commands
was low. Do not patch from this audit directly and do not repeat the
no-interval/log-level-only captures. Next useful slice is an offline
parent/callee state-carrier design for `82282490 -> 82287788`, especially a
broader `f[1]` carrier with explicit helper, child-call, barrier, exit,
exception, and fallback rules, or a CFG-aware fpscr dirty-carrier audit before
any speed A/B.

Previous priority:
`docs/research/20260524-141502-state-carrier-audit-route-capture.md`
is now understood as a route-clean but data-blocked capture: `log_level=0` was
one issue, and the follow-up `scratch/thor-debug/20260524-142827-*` proved the
other issue was missing `arm64_speed_profile_interval_ms`.

Previous priority:
`docs/research/20260524-133027-8228252c-state-carrier-design-audit.md`
adds `tools/thor/thor_hir_state_carrier_design_audit.ps1`. Use it before any
new state-carrier behavior patch. For `82282490:8228252C-822825C4` and
`82282598 -> 82287788`, it prints per-field seed, kill, and writeback
requirements. Current output classifies `f[1]` as a medium-risk
`read_only_entry_seed` (`loads=10`, `stores=0`, dynamic upper `16912720`),
`fpscr` as a very-high-risk `dirty_cfg_carrier` (`loads=26`, `stores=26`,
writebacks at `82287ED4`, `82287EDC`, `82287EE4`, `82288220`), `r[3]` as a
high-risk mutable cache candidate, and `lr` as blocked call-link state. Do not
patch behavior next. The next useful slice is a default-off carrier-design
counter/probe that reports dynamic seed hits, reads, dirty writes, writebacks,
kills, and fallbacks before any speed A/B.

Previous priority:
`docs/research/20260524-131709-8228252c-state-carrier-design-constraints.md`
keeps `8228252C-822825C4` first by local/exclusive body time
(`approx_exclusive=2876500`) but closes another narrow local-patch jump. The
hot `82282598 -> 82287788` edge has `calls_total=1691272`, and parent
`82282490` stores `r[3]`, `f[1]`, `fpscr`, and `lr` before or at the call;
callee `82287788` loads all four. `f[1]` is read-only, but pair-entry only
adds one seed-load win over the already-tested stack-slot carrier.
`fpscr` has the largest raw access upper, but needs CFG-aware dirty state plus
writebacks at call-visible PCs `82287ED4`, `82287EDC`, `82287EE4`, and
`82288220`. `r[3]` is mutable, and `lr` is call-link state. Do not patch a
local PC fold, standalone `f[1]` thunk, or `fpscr` shortcut next. The next
useful slice should add or run a CFG/interprocedural state-carrier design audit
that prints exact seed, kill, and writeback requirements for
`8228252C-822825C4` and `82282598 -> 82287788` before any behavior patch.

Previous priority:
`docs/research/20260524-121503-82282490-branch-loop-aggregate-audit.md`
closes the standalone branch-loop lane. The broader offline pass audited
`82282490-822824B8`, `822824B8-822824E8`, and `822825F4-82282600` together:
the loop head plus tail total `2133128` local-exclusive ticks with four CR
predicate stores, but only three fallthrough-only GPR reload opportunities, all
inside `822824B8-822824E8`. The entry/setup slice adds only `249802` ticks and
no reload win. Do not patch CR store/barrier fusion or a narrow branch-local
GPR carrier next. The next useful slice should return to higher-traffic
CFG-aware or interprocedural state-carrier work around `8228252C-822825C4` and
the hot `82282490 -> 82287788` state round-trip, with explicit
call/helper/barrier/exit/alias flush rules.

Previous priority:
`docs/research/20260524-120704-822824b8-branch-state-audit.md`
adds `tools/thor/thor_hir_branch_state_audit.ps1`. The fresh
`822824B8-822824E8` branch span is real local work
(`body_ticks_total=1099164`, `class_stores=cr:9,gpr:7`), but not a patch target
yet. The tool finds three branch predicates that were also stored to CR context
before `context_barrier` / branch, plus only three fallthrough-only GPR reload
opportunities (`r[11]`, `r[11]`, `r[31]`). The loop tail
`822825F4-82282600` has the same CR predicate-store shape and no reload
opportunity. Do not patch `822824B8-822824E8` next. Broad CR store/compare
fusion is a known negative lane for Blue Dragon, and the local GPR upper bound
is too small for a standalone carrier probe. Next useful slice should either
broaden this audit across the `822824B8 <-> 822825F4` loop and other branchy
local spans, or return to a higher-traffic CFG-aware/interprocedural
state-carrier design.

Previous priority:
`docs/research/20260524-115538-8228252c-lane-closure-next-target.md`
rechecks the broader `82282490:8228252C-822825C4` span after the recent
`fpscr`, `f[1]`, `stvewx`, and `MUL_ADD_V128` lanes. The span is still large
(`approx_exclusive=2876500`, `store_context=27`, `load_context=14`), but the
isolated safe-looking sub-lanes are closed for now: call-boundary dead stores
have no HIR DCE payoff, `f[1]` pair-entry ROI is only a seed load, `fpscr`
requires CFG-aware dirty writebacks, and the quiet `stvewx` / `MUL_ADD_V128`
A/B runs did not prove speed. Do not patch `8228252C-822825C4` behavior next
unless the slice is a broader CFG-aware or interprocedural state-carrier
design. The later branch-loop aggregate audit has now closed the
`822824B8` local detour, so avoid another immediate CR/GPR branch slice.

Previous priority:
`docs/research/20260524-114614-82287788-fpscr-dirty-cache-audit.md`
adds `tools/thor/thor_hir_fpscr_dirty_cache_audit.ps1`. The report closes the
tempting `fpscr` shortcut for now. For `82287788` on the hot
`82282490 -> 82287788` edge, `fpscr` has `loads=26`, `stores=26`, and
dynamic upper `loads=43973072` / `stores=43973072`, but there are
`strict_same_window=0` reload wins. The useful transitions are
`cfg_phi_or_stack_carrier_required=24` plus
`external_visibility_flush_required=2`, with call writebacks at `82287ED4`,
`82287EDC`, `82287EE4`, and `82288220`. A parent `82282490` check reports all
12 transitions require external call visibility. Do not patch `fpscr` dirty
caching next. Return to broader `8228252C-822825C4` state/vector/FPR work, or
first design a generic CFG-aware dirty carrier audit with exact call/exit
writebacks.

Previous priority:
`docs/research/20260524-113615-82287788-pair-entry-roi.md`
adds `-CompareExistingStackCarrier` to
`tools/thor/thor_hir_function_pair_variant_audit.ps1`. The `f[1]` pair-entry
candidate is real, but compared to the existing stack-slot carrier it only
removes the first context load per `82287788` call
(`pair_incremental_static_upper=1691272`), while requiring new pair-entry
ABI/prolog/thunk machinery. Do not implement a standalone `f[1]` pair-entry
behavior patch next. Move to a broader `8228252C-822825C4`
state-cache/callee-variant design, or an `fpscr` dirty-cache audit with exact
writeback points before any `fpscr` behavior patch.

Previous priority:
`docs/research/20260524-112944-82287788-function-pair-variant-audit.md`
adds `tools/thor/thor_hir_function_pair_variant_audit.ps1`. For the hot
`82282490` `82282598 -> 82287788` edge, the tool reports that `f[1]` (`+296`)
has a parent seed store at `82282594`, callee `loads=10`, `stores=0`,
`replaceable_loads=10`, `unsafe_loads=0`, and
`static_replace_upper=16912720` when child calls `82287854` and `82287ED4`
are treated as preserved. The `fpscr` control is rejected (`loads=26`,
`stores=26`, `variant_shape=blocked_by_callee_writes`). Do not patch behavior
yet: current `A64Emitter::Call` uses `x0` for the guest return address, so the
next useful code lane is a default-off pair-specific thunk/entry-seed design
or compile-time caller-seeded callee-slot proof for `f[1]`, preserving normal
`82287788` entry semantics and parent context visibility.

Previous priority:
`docs/research/20260524-111940-8228252c-state-forwarding-plan.md`
updates `tools/thor/thor_hir_interproc_state_roundtrip_audit.ps1` so the
direct-call state audit prints a forwarding-risk plan. For `82282490`
`82282598 -> 82287788`, the edge has `calls_total=1691272` and
`body_ticks_total=5653971`. `f[1]` is the only medium-risk read-only candidate
(`loads=10`, `stores=0`, `static_load_upper=16912720`), but the previous
callee stack-slot carrier A/B already missed speed proof. Next useful slice is
a function-pair/callee-variant design audit for `f[1]`, keeping parent context
visibility unless proven movable. Do not patch `fpscr`, `r[3]`, or `lr` from
this evidence: `fpscr` is mutable and very high-risk
(`static_load_upper=43973072`, `static_store_upper=43973072`), `r[3]` is
mutable/high-risk, and `lr` is call-link state.

Previous priority:
`docs/research/20260524-110545-blue-dragon-f1-carrier-fastpath-ab.md`
adds the default-off `arm64_blue_dragon_f1_carrier_fastpath` replacement probe
for the audited `82287788` `f[1]` (`+296`) load sites. NativeCore and
FullDeploy passed. Audit capture `scratch/thor-debug/20260524-105424-*`
reached the visible opening sky/dragon-wing route on APK SHA
`A2A59F7777F77B52526ABF5E401194A4A7490D71B4F7A878ED8099B0CA7557A9` with a
clean fatal-marker search and final counters `seed=223805/688280` and
`reuse=443749/1365365`. Quiet same-APK A/B was route-clean but not faster:
control `scratch/thor-debug/20260524-105813-*` reached a later opening
`Microsoft Game Studios Presents` frame, while fastpath-on
`scratch/thor-debug/20260524-110156-*` remained at an earlier sky/dragon-wing
frame; `82287788` code size changed `35568 -> 35572`, and `82282490` remained
the real wall. Keep both `arm64_blue_dragon_f1_carrier_fastpath` and
`arm64_blue_dragon_f1_carrier_audit` default-off and out of presets. Do not
repeat this exact f[1] carrier A/B unchanged. Next useful slice is broader
`8228252C-822825C4` parent/callee state/vector/FPR reduction, especially a
design that reduces the live `82282490 -> 82287788` state round-trip without
hiding guest-visible `r[3]`, `f[1]`, `fpscr`, or `lr` state. Require an offline
audit before the next Thor behavior patch.

Previous priority:
`docs/research/20260524-104145-blue-dragon-f1-carrier-runtime-audit.md`
adds the default-off `arm64_blue_dragon_f1_carrier_audit` runtime probe.
NativeCore and FullDeploy passed. Capture
`scratch/thor-debug/20260524-103438-*` reached the visible opening
sky/dragon-wing route on APK SHA
`514C6A8C0C14820BFE0ED48CF280D00371B30602AEEE5CD86AE06A82E05D139F` with a
clean fatal-marker search. Final counters were `total=755910/2159587`,
`helper_preserved=379058/1081707`, and
`child_preserved=376852/1077880`. This proves the `82287788` `f[1]`
replacement opportunity is dynamically real, but it is not speed proof because
the run used generated-code atomic counters and `82287788` remains cheap
(`body_ticks_total=3578711`, `ticks_per_entry=5`). Keep
`arm64_blue_dragon_f1_carrier_audit` default-off and out of presets. The later
fastpath A/B supersedes this as the current decision.

Previous priority:
`docs/research/20260524-102113-821ce028-f1-clobber-audit.md` resolves the
child-call blocker for the `82287788` `f[1]` carrier lane. Running
`tools/thor/thor_hir_fpr_carrier_safety_audit.ps1` on `821CE028` offset `296`
reports `target_loads=0`, `target_stores=0`, and
`decision=no_target_context_access_observed`; the only calls are the
`call_indirect.6` return paths at `821CE1CC` and `821CE1D4`. Hotpath report
shows the only FPR context traffic is `f[0]` (`+288`), with no `f[1]` or
`fpscr` traffic. Treat `821CE028` as `f[1]`-preserving only for the narrow
parent `82287788` carrier lane. Do not patch `821CE028`. Next useful slice is
a default-off `82287788` runtime carrier audit/probe that treats
`__savegprlr_28`, `__restgprlr_28`, and direct calls to `0x821CE028` as
`f[1]`-preserving and counts dynamic replacement opportunities for all 10
static `f[1]` loads before quiet A/B.

Previous priority:
`docs/research/20260524-101421-82287788-f1-carrier-safety-audit.md`
adds `tools/thor/thor_hir_fpr_carrier_safety_audit.ps1`. For `82287788`
offset `296` (`f[1]`), the audit reports `target_loads=10`,
`target_stores=0`, `helper_whitelist=2`, and `unknown_call_blocked=8`.
Source review of `TryEmitGprLrHelperCall` shows the `__savegprlr_28` /
`__restgprlr_28` helpers touch GPR/LR state but not FPR, VMX, or `fpscr`.
Only the first two `f[1]` loads are helper-whitelist candidates; the other
eight cross real child calls to `0x821CE028` at `82287854` and `82287ED4`.
Do not implement a speed patch from this audit alone. Next useful slice is a
default-off runtime counter for dynamic helper-whitelist versus
unknown-call-blocked `f[1]` hits, or a focused `821CE028` `f[1]` clobber/use
audit. Keep `fpscr` out of this lane.

Previous priority:
`docs/research/20260524-100409-82287788-callee-local-promotion-audit.md`
adds `tools/thor/thor_hir_callee_local_promotion_audit.ps1`. The audit splits
`82287788` into strict promotion windows broken by labels, context barriers,
calls, branches, and returns. `f[1]` has `loads=10`, `stores=0`, but
`strict_redundant_loads=0`; its useful `9` reload opportunities cross
boundaries and `2` cross calls. `fpscr` has `loads=26`, `stores=26`, and no
strict local wins; it is mutable/high-risk. Do not implement a blind
callee-local context cache. Next useful slice is a default-off
CFG/function-pair `f[1]` carrier audit/probe for `82282490 -> 82287788` that
proves call-clobber and flush rules before any quiet A/B. Do not patch `fpscr`
first.

Previous priority:
`docs/research/20260524-095137-82282598-82287788-state-roundtrip-audit.md`
adds `tools/thor/thor_hir_interproc_state_roundtrip_audit.ps1`. The offline
audit joins parent/callee filtered HIR with dynamic call-edge rows. For
`82282490` call PC `82282598 -> 82287788`, the dynamic edge is hot
(`calls_total=1691272`, `body_ticks_total=5653971`). Parent `82282490` stores
`r[3]`, `f[1]`, `fpscr`, and `lr` before or at the call, and callee `82287788`
loads all four tracked fields (`f[1] loads=10`, `fpscr loads=26/stores=26`).
Do not skip that live state. Next useful slice is a default-off
Blue-Dragon/function-pair carrier or callee-local promotion probe for
`82282490 -> 82287788`, with explicit helper/exit/exception/alias flush rules
and route proof before any quiet A/B.

Previous priority:
`docs/research/20260524-094213-hir-dead-state-store-dce-audit.md`
adds `tools/thor/thor_hir_dead_state_store_dce_audit.ps1`. The offline audit
virtually removes target `store_context` rows before DCE and counts recursive
dead assignment fallout. For the exact `13` route-proven candidate-dead stores
in `82282490:8228252C-822825C4`, it reports `dead_assignments=0`. Do not
implement a HIR-level version of that same `13`-store suppression; it would not
delete the upstream vector/FPR work and would repeat the tiny backend store skip
that missed speed. The unsafe all-span upper bound exposes `22` removable
assignments, mostly the live `f[1]` / `fpscr` path before
`82282598 -> 82287788`, but focused `82287788` HIR proves the callee really
loads `f[1]` and `fpscr`. Do not skip that live state. Next useful slice is an
interprocedural argument/state-roundtrip audit for `82282598 -> 82287788`,
focused on a guarded function-pair carrier, callee-local promotion, or direct
call argument lowering for live `f[1]` / `fpscr` without hiding guest-visible
PPC state.

Previous priority:
`docs/research/20260524-093001-blue-dragon-call-boundary-store-suppression-probe.md`
adds default-off `arm64_blue_dragon_call_boundary_state_suppress_dead_stores`.
It skips only the `13` route-proven candidate-dead stores before the Blue
Dragon `82282490` edge `82282598 -> 82287788`; it does not skip the live-in
`r[3]`, `f[1]`, `fpscr`, or `lr` stores. Route-safety capture
`scratch/thor-debug/20260524-091813-*` reached the visible opening
sky/dragon-wing route with clean fatal-marker search and proved
`suppressed=dead=9007635`. Quiet same-APK A/B
(`scratch/thor-debug/20260524-092153-*` control and
`scratch/thor-debug/20260524-092523-*` suppression-on) was route-clean and
shrank `82282490` code size only `86828 -> 86768`, but did not prove route-wide
speed. Keep the cvar default-off and out of presets. Do not repeat this exact
A/B next; move to broader `8228252C-822825C4` state/vector/FPR reduction or a
larger state-cache/lowering change backed by fresh offline evidence.

Previous priority:
`docs/research/20260524-085451-blue-dragon-call-boundary-state-runtime-audit.md`
adds the default-off `arm64_blue_dragon_call_boundary_state_audit` runtime
counter and Android/Thor launch plumbing. Capture
`scratch/thor-debug/20260524-084805-*` reached the visible opening
sky/dragon-wing route with clean fatal-marker search and proved the exact
runtime shape behind the static audit: `675279` dynamic boundary hits,
`13` candidate-dead stores per hit (`dead=8778627`) and `4` live-in stores per
hit (`live=2701116`). This is not speed proof because audit instrumentation
increased generated code size. The next useful slice is a separate
default-off, Blue-Dragon/function/PC-gated store-suppression probe for only the
`13` candidate-dead sites, with route-clean proof before any quiet A/B. Do not
skip the live-in `r[3]`, `f[1]`, `fpscr`, or `lr` stores. Keep
`arm64_blue_dragon_stvewx_stack_lane_fastpath` and
`arm64_blue_dragon_mul_add_v128_fastpath` default-off.

Previous priority:
`docs/research/20260524-082324-blue-dragon-mul-add-v128-fastpath-ab.md`
implements the default-off `arm64_blue_dragon_mul_add_v128_fastpath` for only
Blue Dragon `82282490` PCs `82282568`, `8228256C`, and `82282570`. `NativeCore`
and `FullDeploy` passed. Route-safety capture
`scratch/thor-debug/20260524-081212-*` reached the visible opening
sky/dragon-wing route with clean fatal-marker search. Quiet same-APK A/B
captures `scratch/thor-debug/20260524-081601-*` control and
`scratch/thor-debug/20260524-081929-*` fastpath-on both reached matching
opening route frames with clean fatal-marker searches. The fastpath shrank
`82282490` code size `86828 -> 85424`, but final counters were mixed
(`delta=65296 total=170142` control vs `delta=60844 total=177562` fastpath-on)
and thread samples did not prove route-wide speed. Keep the cvar default-off
and out of presets. The next useful lane is broader `8228252C-822825C4`
local-span state/vector/FPR reduction, not another immediate narrow FMA skip.

Previous priority:
`docs/research/20260524-080156-blue-dragon-mul-add-v128-audit-capture.md`
fixes the Android cvar plumbing for `arm64_blue_dragon_mul_add_v128_audit` and
records the first real route-clean runtime counters. Capture
`scratch/thor-debug/20260524-075713-*` reached the visible opening
sky/dragon-wing route with clean fatal-marker search on APK SHA
`7BBAB603A9931918867E9AAE0869193F3A2049329ED7FB448D36C4E66B97EE85`.
The final `MUL_ADD_V128` audit row for `82282490` PCs `82282568`,
`8228256C`, and `82282570` was `total=722256/2131533`,
each PC `240752/710511`, `fpcr_switch=240752/710511`, and all slow semantic
checks cold: `sw_flush_path=0/0`, `input_denorm=0/0`, `output_denorm=0/0`,
`nan_entry=0/0`, `nan_lane=0/0`. The next patch should be a default-off
Blue-Dragon/function/PC-gated `MUL_ADD_V128` fastpath for only those three PCs,
with route-clean proof before a quiet same-APK A/B. Keep the audit and fastpath
out of presets until speed is proven.

Previous priority:
`docs/research/20260524-052524-82281d28-call-edge-split.md` split the larger
`82281D28` lane. Capture `scratch/thor-debug/20260524-052031-*` reached the
visible opening sky/wing route with clean fatal-marker search on APK SHA
`A4CB9C0E8F4CBB63EB09F3DA38640BEA5410DFE5916A155627C18F3B90C39BD8`. With the
HIR/block dump removed, `82282490` returned to the top body row:
`82282490=30214411`, `82281D28=10212419`, `82287788=3195362`,
`82282388=3020644`, `82282410=2902097`, `821CE028=674464`, and
`826BF770=563064`. Dynamic edges show `82281D28` is mostly recursive:
`8228233C -> 82281D28` has `body_ticks_total=7350722`, `calls_total=69445`,
and about `72.0%` of final `82281D28` body time. Reported direct edges account
for about `89.1%`, leaving about `1115427` ticks of approximate
parent-exclusive work. Do not patch local `82281D28` generated code yet or
rerun the exact HIR/block capture unchanged. Next step should collapse/reason
about the recursive SCC and return to body-backed `82282490` work, or improve
the mapper enough to recover `8228233C` static span context before any local
`82281D28` codegen patch.

Tooling update:
`docs/research/20260524-053502-call-path-recursive-heuristic.md` updates
`tools/thor/thor_hir_call_path_report.ps1` to print recursive-edge coverage
and approximate parent-exclusive body time whenever call-edge and body-time
rows share a log. It confirms `82281D28` is `89.08%` dynamic direct-edge body
and `71.98%` self-recursive body, while `82282490` is `84.08%` direct-edge
body and `71.44%` self-recursive body on the older recursive capture. Use this
report before patching call-heavy blocks. The next patch candidate should come
from body-backed local/exclusive `82282490` spans such as the previously mapped
`8228252C-822825C4` area, not inclusive recursive block totals alone.

Current local-span ranking:
`docs/research/20260524-054204-82282490-exclusive-candidate-ranking.md`
updates `tools/thor/thor_hir_dynamic_block_slice_report.ps1` to print a
`Local Exclusive Candidate Ranking`. It keeps `8228252C-822825C4` first
(`approx_exclusive=2876500`, `exclusive_pct=33.72`) and `822824F0-82282528`
second (`approx_exclusive=2021174`, `exclusive_pct=57.72`). Do not repeat the
exact narrow `stvewx` stack-lane fold A/B for `82282580/82282584`; it was
route-clean but did not prove speed.

Latest top-span comparison:
`docs/research/20260524-055108-82282490-top-span-comparison.md` ran the
focused offline audit. `8228252C-822825C4` remains the larger absolute local
target (`approx_exclusive=2876500`), but its obvious safe lanes are not ready:
CR fusion/elision is rejected, exact `stvewx` lane-fold already missed speed
proof, and the remaining state/vector/FPR traffic needs more provenance.
`822824F0-82282528` is smaller but cleaner (`approx_exclusive=2021174`,
`exclusive_pct=57.72`) and exposes an exact `lvlx/lvrx` to
`load_vector_shl + permute` to `v[0]` shape. Do not patch generated-code
behavior from this comparison alone. Next slice should add or run focused
provenance for that `822824F0` vector-load join before any default-off
function/span-gated codegen experiment.

Latest vector-load provenance:
`docs/research/20260524-060827-822824f0-vector-load-provenance.md` adds
`tools/thor/thor_hir_vector_load_join_audit.ps1`. The tool confirms
`822824F0-82282528` is body-backed (`body_ticks_total=3501617`, child edge
`82274DB0=1480443`, approximate exclusive `2021174`), but the load/join is not
self-contained. `82282520 lvlx vr13,r0,r11` loads from `r30 + 0x14` and stores
`v[13]`; `82282528 lvrx vr0,r11,r10` loads from `r30 + 0x20`, includes the
`lvrx` zero path, and stores `v[0]`; the first matching `vor vr0,vr13,vr0`
join is `8228254C` in the following span. Do not patch local
`822824F0-82282528` codegen yet. Next slice should audit the cross-span
`lvlx/lvrx -> vor -> extract/splat -> vmaddfp` consumer chain through at least
`8228254C`, or return to the larger `8228252C-822825C4` state/vector/FPR target
with better provenance.

Latest cross-span consumer audit:
`docs/research/20260524-062037-822824f0-cross-span-consumer-audit.md` extends
the vector-load auditor with consumer rows. The larger `822824F0-82282574`
span contains the full `lvlx/lvrx -> vsldoi -> vor -> extract/splat ->
vmaddfp` chain, plus four stack-ish `lvx128` loads and downstream lookahead
`stvewx` users. It is body-backed (`body_ticks_total=3501617`, child edge
`82274DB0=1480443`, approximate exclusive `2021174`) but not a quick local
peephole: the three `MUL_ADD_V128` lowerings bring VMX FPCR handling,
scratch-stack saves, PPC NaN fixup, and denormal handling. Do not patch
local-only `822824F0` codegen yet. Next useful slice should estimate/generated
instruction cost for this exact chain and compare it against
`8228252C-822825C4` before a default-off function/span-gated experiment.

Latest source-reviewed span estimate:
`docs/research/20260524-063109-82282490-span-instruction-estimate.md` updates
`tools/thor/thor_hir_a64_codegen_audit.ps1` with a
`Source-Reviewed A64 Floor Estimate`. This is a heuristic source floor, not
exact generated instruction count. The comparison keeps `8228252C-822825C4`
ahead of the `822824F0-82282574` chain: `8228252C-822825C4` has approximate
exclusive `2876500`, estimated floor `192`, and `14981.77` exclusive ticks per
estimated instruction, while `822824F0-82282574` has approximate exclusive
`2021174`, estimated floor `164`, and `12324.23` exclusive ticks per estimated
instruction. Do not patch local-only `822824F0` vector codegen from this.
Next useful slice should target the larger `8228252C-822825C4` lane or a
shared `MUL_ADD_V128` cost lane with a default-off, function/span-gated audit
first. Good concrete options are `MUL_ADD_V128` source/audit for
`82282568/8228256C/82282570`, or better `82282588` dynamic-`EXTRACT_I32` /
`stvewx` provenance before another lane-fold experiment.

Latest `82282588` stvewx provenance:
`docs/research/20260524-064118-82282588-stvewx-provenance.md` updates
`tools/thor/thor_hir_stvewx_lane_audit.ps1` with `-ProvenanceStartGuest` and
simple `store_context` -> `load_context` forwarding. With target slice
`8228252C-822825C4` and provenance start `82282514`, the audit proves all
three dynamic `stvewx` extract lanes: `82282580 -> lane 0`,
`82282584 -> lane 1`, and `82282588 -> lane 2` through
`r6 = r1 + 0x50`, then `r6 + 0x8`. Do not repeat the old two-site A/B
unchanged. If the next slice patches generated code, use a new default-off
Blue-Dragon/function/PC-gated all-three-site `EXTRACT_I32` fastpath with audit
counters first; if it misses, move to `MUL_ADD_V128` cost.

Latest all-three `stvewx` fastpath audit:
`docs/research/20260524-065602-blue-dragon-all-three-stvewx-fastpath.md`
extends the existing default-off
`arm64_blue_dragon_stvewx_stack_lane_fastpath` to fold all three proven
`82282490` dynamic `EXTRACT_I32` PCs: `82282580 -> lane 0`,
`82282584 -> lane 1`, and `82282588 -> lane 2`. `NativeCore` and
`FullDeploy` passed. Capture `scratch/thor-debug/20260524-065132-*` reached
the visible opening sky/dragon-wing route on APK SHA
`8A3DA22B2208AA67DE13C07383490F3BEBA14E2DC1538AAB5CE08FD035626771`, had a
clean fatal-marker search, and ended with audit counters
`fastpath=722256/2008221 fallback=0/0`. This is route-clean correctness
evidence, not a speed win because audit counters and block body-time were on.
Keep the cvar default-off in presets. Next useful slice is a quiet same-APK
A/B or control sandwich with audit off; if that is inconclusive again, switch
to `MUL_ADD_V128` cost for `82282568/8228256C/82282570`.

Latest all-three `stvewx` quiet A/B:
`docs/research/20260524-071045-blue-dragon-all-three-stvewx-quiet-ab.md`.
Control `scratch/thor-debug/20260524-070253-*` and fastpath-on
`scratch/thor-debug/20260524-070613-*` used the same commit/APK with audit,
body-time, block body-time, and disassembly off. Both reached matching opening
sky/dragon-wing frames with clean fatal-marker searches. The fastpath shrank
`82282490` code size from `86828` to `86708`, but final `82282490` total
entries stayed identical at `183498` and final interval deltas were close
(`54908` control vs. `51940` fastpath-on). Treat this as no proven speed win.
Keep the cvar default-off and stop spending new sprint slices on narrow
`stvewx` lane folds unless broader state/vector work reopens them. Next lane:
`MUL_ADD_V128` source/cost audit for `82282568/8228256C/82282570`.

Latest `MUL_ADD_V128` source/cost audit:
`docs/research/20260524-072228-mul-add-v128-source-cost-audit.md`.
`tools/thor/thor_hir_a64_codegen_audit.ps1` now prints a dedicated
`MUL_ADD_V128 Source-Cost Audit` section. Both body-backed spans hit
`82282568`, `8228256C`, and `82282570`. `8228252C-822825C4` remains the larger
local target (`approx_exclusive=2876500`, `mul_add_v128=3`,
`approx_exclusive_ticks_per_estimated_instr=14981.77`). Do not patch behavior
yet: A64 `MUL_ADD_V128` includes VMX FPCR handling, optional denormal flushing,
scratch source saves, `fmla`, PPC NaN fixup, optional output flushing, and a
dest copy, while x64 intentionally avoids fused host FMA for this opcode. Next
use a default-off function/span/PC-gated runtime audit for
`82282568/8228256C/82282570` to count denormal flush need, NaN-fixup entry and
per-lane repair, FPCR mode switches, and source/dest alias copies before any
shortcut.

Latest `MUL_ADD_V128` runtime audit patch:
`docs/research/20260524-074442-blue-dragon-mul-add-v128-runtime-audit.md`.
The backend now has default-off cvar
`arm64_blue_dragon_mul_add_v128_audit`, gated to Blue Dragon guest function
`82282490` and PCs `82282568`, `8228256C`, and `82282570`. It logs total and
per-PC calls, FPCR switches, software flush-path eligibility, input/output
denormal sightings, NaN-fixup entry and repaired lanes, and source/dest vector
copies. `tools/thor/thor_xenia_debug.ps1` exposes
`-Arm64BlueDragonMulAddV128Audit`; presets keep it default-off. `NativeCore`
passed. Next continuation should run `FullDeploy`, then a delayed `82282490`
body-time speed capture with `-Arm64BlueDragonMulAddV128Audit true`. Do not
patch a fastpath until that route-clean audit proves the expensive paths are
cold.

Previous priority:
`docs/research/20260524-050931-82281d28-focused-capture.md` followed the
larger `82281D28` lane. Capture `scratch/thor-debug/20260524-050427-*`
reached the visible opening sky/wing route with clean fatal-marker search on
APK SHA `A4CB9C0E8F4CBB63EB09F3DA38640BEA5410DFE5916A155627C18F3B90C39BD8`.
Final body rows put `82281D28=65346716` first, then
`82282490=31615045`, `82282388=18965773`, `82282410=17323669`,
`82287788=3277477`, and `821CE028=716173`. The top internal block row is
`8228233C` with `body_ticks_total=46564347`, but the warning-level HIR dump
starts at `82282398`, so exact local span mapping is incomplete. The visible
HIR after that point shows calls to `0x826BF770` and recursive `0x82281D28`,
and large sibling/alias-looking rows `82282388` and `82282410` make this look
call-edge heavy. Do not patch `82281D28` generated code yet. Next run should
be a route-stabilized `82281D28` call-edge split with delayed body-time
comparators:
`82281D28,82282388,82282410,82282490,826BF770,82287788,821CE028`.

Previous priority:
`docs/research/20260524-045602-821ce028-focused-capture.md` follows the
`82287788 -> 821CE028` child edge. Capture
`scratch/thor-debug/20260524-045022-*` reached the visible opening sky/wing
route with clean fatal-marker search on APK SHA
`A4CB9C0E8F4CBB63EB09F3DA38640BEA5410DFE5916A155627C18F3B90C39BD8`. Final
body rows were `82282490=37190996`, `82281D28=7446911`,
`82287788=4992080`, and `821CE028=2523574`. `821CE028` emitted OptHIR and its
top local block `821CE048-821CE1CC` is fully local at `body_total=607573`, with
`vmsum4fp128*:6`, `vcmpgefp.:6`, `vcmpgtfp.:6`, `vslw:6`, `vxor:6`,
`mfcr:12`, and `rlwinm:13`. Do not patch `821CE028` immediately: CR
store/barrier and broad VMX-dot lanes remain rejected, and the only safe-looking
lane is a narrow offline `r10` state-traffic audit. Prefer the larger next
target: route-stabilized filtered HIR plus delayed body/block-time capture for
`82281D28`, keeping `82282490`, `82287788`, and `821CE028` as comparators.

Previous priority:
`docs/research/20260524-044223-822877bc-span-reduction-audit.md` closes the
first parent-side `822877BC-82287B38` patch temptation. The new
`tools/thor/thor_hir_span_reduction_audit.ps1` combines filtered HIR,
block-body rows, and the separate call-edge capture. It shows
`body_ticks_total=1173620` for the parent span, but the child edge
`82287788 -> 821CE028` accounts for `1147798` ticks over `340310` calls,
leaving only `25822` approximate parent-exclusive ticks (`2.2%`). Do not patch
local `822877BC-82287B38` generated code first, even though the parent dump has
large CR/state/vector traffic. Next slice should run a route-stabilized
filtered HIR plus delayed body/block-time capture for `821CE028`, keeping
`82282490` and `82287788` in the body-time filter as comparators. Keep CR
compare/barrier fusion, CR-store elision, broad VMX-dot, stale `822824F0`,
broad GPR caches, and the exact stvewx lane-fold A/B closed unless new
exclusive body evidence reopens them.

Older priority:
`docs/research/20260524-042555-82287788-focused-callee-split.md` follows the
`8228252C -> 82287788` child lane after the lane-fold probe. Two captures
(`scratch/thor-debug/20260524-041413-*` and
`scratch/thor-debug/20260524-042031-*`) reached the visible opening sky/wing
route with clean fatal-marker searches on APK SHA
`A4CB9C0E8F4CBB63EB09F3DA38640BEA5410DFE5916A155627C18F3B90C39BD8`. The
lower-noise call-edge split keeps `82282490` as the main wall
(`body_ticks_total=31584286`) and puts `82287788` as a secondary child
(`body_ticks_total=3659269`, `ticks_per_entry=5`, `code_size=35380`). The only
meaningful dynamic child edge is `82287788 -> 821CE028`
(`body_ticks_total=1147798`, `calls_total=340310`, `ticks_per_call=3`). The top
local span is `822877BC-82287B38`, with `store_context=186`,
`load_context=145`, `context_barrier=15`, `lvlx/lvrx/vor/stvx`, `lvx128:6`,
`vmaddfp:6`, `vspltw:6`, constant `extract=6`, and `splat=10`. Do not revive
the known-crashy CR compare/barrier fusion or CR-store elision cvars, and do
not restart broad VMX-dot or stale `822824F0` peepholes. Next slice should make
or run a focused `822877BC-82287B38` state/vector-reduction audit, then only
patch a default-off function/span-gated lowering if the audit identifies a
semantics-safe shape.

Older priority:
`docs/research/20260524-040404-blue-dragon-stvewx-lane-fastpath.md` closes the
first `8228252C` lane-fold probe. The default-off A64 `EXTRACT_I32` fastpath
for only `82282580 -> lane 0` and `82282584 -> lane 1` is route-clean and
audited: `scratch/thor-debug/20260524-035227-*` reached the visible opening
sky/wing route, had clean fatal-marker search, and ended with
`fastpath=446272/1338814 fallback=0/0`. The quiet same-APK A/B did not prove a
speed win: control `scratch/thor-debug/20260524-035623-*` reached
`Microsoft Game Studios Presents`, while fastpath-on
`scratch/thor-debug/20260524-035944-*` reached an earlier sky/wing frame.
Keep `arm64_blue_dragon_stvewx_stack_lane_fastpath` and audit default-off. Do
not repeat the exact A/B unchanged. Next worker slice should move to larger
body-backed work in `8228252C-822825C4`, especially the `8228252C -> 82287788`
child path or broader state/vector/FPR traffic.

`docs/research/20260524-034103-8228252c-stvewx-lane-provenance.md` proves the
first narrow `stvewx` lane-folding target. Use
`tools/thor/thor_hir_stvewx_lane_audit.ps1` before changing this lane again.
The audit proves `82282580 -> lane 0` from `r1 + 0x50` and
`82282584 -> lane 1` from `r1 + 0x54` under the normal PPC stack-pointer
16-byte alignment assumption. It leaves `82282588` unknown because the address
is `r6 + 0x8`. Next slice may implement a default-off,
Blue-Dragon/function/PC-gated A64 `EXTRACT_I32` fastpath for only `82282580`
and `82282584`, with audit/counters before any quiet speed A/B.

`docs/research/20260524-033116-8228252c-a64-codegen-audit.md` adds the A64
generated-code/lowering audit for the current top local `82282490` slice. Use
`tools/thor/thor_hir_a64_codegen_audit.ps1` for this lane. The slice
`8228252C-822825C4` has body total `8530471`, child edge
`8228252C -> 82287788` body `5653971`, approximate exclusive local work
`2876500`, and a generated-code floor dominated by `store_context:27`,
`load_context:14`, `extract:9`, `splat:6`, `stvewx:3`, and `vmaddfp:3`. Next
slice should prove stack-lane provenance for `82282580`, `82282584`, and
`82282588`, or make a default-off function/block-gated `stvewx` lane-folding
probe only for the two apparent `r1 + constant` stores. Keep broad `822824F0`
vector peepholes stale until fresh body-backed evidence reopens them.

`docs/research/20260524-032005-82282490-dynamic-block-slice-report.md` is the
latest local-target map. Use
`tools/thor/thor_hir_dynamic_block_slice_report.ps1` before a local
`82282490` codegen patch. It maps `8228252C` to exact slice
`8228252C-822825C4`, with body total `8530471`, edge body `5653971`, and
approximate exclusive `2876500`. This is the current top local patch candidate:
vector/FPR/state traffic with `vspltw:6`, `lvx128:4`, `stvewx:3`,
`vmaddfp:3`, `store_context:27`, `load_context:14`, `extract:9`, and
`splat:6`. Next slice should run an A64 generated-code audit for this exact
span before implementing a default-off function/block-gated vector/store or
state-traffic experiment.

`docs/research/20260524-031017-82282490-exclusive-callee-attribution.md` now
supersedes the broad "exclusive attribution" instruction. Same-run block
body-time plus call-edge timing in `scratch/thor-debug/20260524-030450-*`
reached the visible opening route with clean fatal search. Dynamic direct-call
edges account for `84969304` ticks, or `84.08%` of final `82282490` body ticks;
the self-recursive `822825E0 -> 82282490` edge alone is `72194274` ticks,
about `71.44%` of final `82282490` body time and `98.32%` of the `822825E0`
block body. Treat this as callee-body work, not generic call/prolog overhead.
Do not rerun the exact block/call-edge capture unchanged. Next slice should
map dynamic block-body guests such as `8228252C` back to exact `82282490`
HIR/PPC spans, then choose a default-off function/block-gated state-traffic or
lowering experiment from body-backed local spans.

`docs/research/20260524-025544-a64-entry-exit-profiler.md` supersedes the
stackpoint/prolog question raised by
`docs/research/20260524-021116-82282490-recursive-stack-sync-probe.md`.
Recursive `822825E0 -> 82282490` is still the opening-route wall, but the new
default-off `arm64_speed_profile_entry_exit_time_filter` capture
`scratch/thor-debug/20260524-025000-*` shows generated prolog/epilog/stackpoint
overhead is tiny: `82282490` entry/exit was `273393` ticks over `186010`
entries (`1.47` ticks/call, `0.93%` of body total), and `82281D28` was `1.547`
ticks/call (`1.56%` of body total). Keep the Blue Dragon speed-pack
`a64_enable_host_guest_stack_synchronization` default `true`; stack-sync-off is
still code-size evidence only, not speed proof. Do not run another unchanged
stack-sync or entry/exit A/B. Next slice should focus exclusive attribution
inside the `82282490` body/recursive child path, or fresh body-backed
state-traffic reduction.

The `82485DD8` word copy-loop fastpath remains confirmed for the Blue Dragon
Thor speed preset. `docs/research/20260524-014050-word-loop-preset-rebaseline.md`
confirmed `arm64_blue_dragon_word_copy_loop_fastpath=true`, reached the visible
opening sky/wing route, and had clean fatal-marker search. Keep
`arm64_blue_dragon_vmx_copy_loop_fastpath` default-off and do not combine the
two copy-loop toggles yet.

As of the latest sprint, `82282490` remains the opening-scene body-time wall.
The offline HIR reports now map context offsets to PPC state names and
barrier/span churn. Prioritize a real GPR/CR state-traffic reduction plan for:

- `r[1]`, `r[10]`, `r[11]`, `r[28]`, `r[29]`, `r[30]`, `r[31]`;
- CR6 fields: `cr6.all_equal`, `cr6.1`, `cr6.none_equal`;
- explicit flushes at helpers, exits, exceptions, and aliasing barriers.

Use `tools/thor/thor_hir_state_span_report.ps1` before editing A64 state-cache
code. Current `82282490` signal: `r[1]` is the leading cross-span repeated
load, while `r[11]`, `r[10]`, and `r[31]` are leading cross-span
load-after-store GPRs.

Also run `tools/thor/thor_hir_gpr_promotion_audit.ps1` before cross-block GPR
promotion work. The first guarded local-slot/data-flow probe for `r[1]` and
`r[11]` reached the opening route but was not faster, so keep
`arm64_context_promotion_gpr_local_slots` default-off. The counter follow-up
showed the local-slot bridge is a bad shape for `82282490`: `103` local stores
for only `3` replaced loads, with `r[11]` giving zero replaced loads. Keep the
local-slot audit default-off too. The first pinned `r[1]` probe was also a
negative result: no-fallthrough stayed route-clean but logged
`loads/hits=107/0`, while fallthrough black-stalled before `82282490`. Keep
`arm64_context_pinned_gpr_r1` and its fallthrough mode default-off. The next
load-shape report found `76` first-in-block loads, `87` multi-predecessor
loads, and no aliasing stores. Move the next state-cache design before A64
register allocation only after a CFG/live-in availability report proves which
target-load blocks have all predecessors leaving `r[1]` clean.
The live-in report now says strict helper+barrier flushing exposes only `14`
replaceable first loads, but preserving clean `r[1]` across `context_barrier`
exposes `56`. The next runtime patch should therefore be default-off,
function-filtered, audited, pre-RA, and barrier-aware.
The first guarded runtime implementation is clean but underpowered:
`docs/research/20260522-153742-r1-livein-state-cache-probe.md` and
`scratch/thor-debug/20260522-152727-*` show `loads_attempted=107` but only
`loads_replaced=16`, with `82282490 code_size=87224` and no searched fatal
markers. Keep `arm64_context_promotion_gpr_livein_r1` default-off. The next
action is not another long speed run; add dirty-reason/actual-CFG eligibility
audit or improve the pre-RA carrier until the runtime replacement count is much
closer to the offline `56` first-load opportunity.
Follow-up `docs/research/20260522-154921-r1-livein-branch-preserve.md` fixed
the likely gap: `branch_true` / `branch_false` should not kill the explicit r1
edge carrier just because HIR marks them volatile. NativeCore passes. Next run
an audited route capture with the r1 live-in toggle enabled and confirm
`loads_replaced` approaches `50+` before judging FPS.
The audited capture is complete:
`docs/research/20260522-160205-r1-livein-audit-capture.md` and
`scratch/thor-debug/20260522-155827-*` show `loads_replaced=64/107`,
`branch_preserves=153`, and no searched fatal markers. This proves the
replacement-count target. The quiet same-APK A/B did not prove speed:
`docs/research/20260522-162135-r1-livein-quiet-ab.md` shows control
`scratch/thor-debug/20260522-161306-*` reached the opening sky/wing route with
`82282490` body-time rows, while live-in-on
`scratch/thor-debug/20260522-161710-*` stayed at the loading spinner and
emitted no `82282490` body-time rows after activation. Keep
`arm64_context_promotion_gpr_livein_r1` default-off. Do not repeat this exact
A/B unchanged; if the path is revisited, do one control-sandwich confirmation
or move to another `82282490` hotpath.
The next offline filter is `tools/thor/thor_hir_block_mix_report.ps1`. Current
evidence says dynamic entry-count hot blocks are the early control/state path,
especially `822824F0`, while the large static VMX/vector blocks need block
body-time proof before broad vector lowering. Next slice should inspect or audit
`822824F0`, or add lower-noise block body-time evidence, instead of repeating
the exact r1 live-in A/B or restarting broad VMX/dot/permute work.
That `822824F0` audit now exists. It shows a mixed block with two calls, five
barriers, CR6/CR0 stores, stack-derived args, `vspltw`, `vmaddfp`, and
`stvewx`. Next slice should add lower-noise per-block body-time attribution for
`82282490`, or an A64 `stvewx` / `extract` / `splat` codegen audit, before a
peephole.
That vector-shape audit now exists too:
`docs/research/20260522-165526-82282490-vector-shape-audit.md`. Use
`tools/thor/thor_hir_vector_shape_report.ps1` before any vector peephole.
Current result: `82282490` has `stvewx=6`, `stvewx_dynamic_extract=6`,
`extract=22`, `extract_dynamic=6`, `splat=152`, `extract_then_splat=16`,
`permute=381`, `load_vector_shl=73`, and `load_vector_shr=64`. `822824F0` is
the only dynamic-hot vector block in the old entry-count profile
(`total=1994364`) and carries `3` `stvewx` / dynamic-extract / `store.1`
shapes. This is a plausible micro-target, but still not body-time proof. Next
slice should add lower-noise per-block body-time attribution for `82282490`,
separating `822824F0` from the other entry-hot control blocks before a
default-off `stvewx` dynamic extract/store codegen experiment.
That block body-time profiler now exists:
`docs/research/20260522-171725-82282490-block-body-time-profiler.md`. Use
`-Arm64SpeedProfileBlockBodyTime true` with `-Arm64SpeedProfileBlockFilter
82282490`, and use `tools/thor/thor_hir_block_mix_report.ps1` to parse the
`Dynamic Body-Time Blocks With HIR Mix` section. Capture
`scratch/thor-debug/20260522-170927-*` reached the visible opening route with no
searched fatal markers. The old `822824F0` entry-count target is not the current
body-time wall: `822825E0` dominates (`body_total=34726883`, peak
`14525259`, peak `ticks_per_entry=61`), then `822825C8`
(`body_total=3216407`, peak `1041116`, peak `ticks_per_entry=500`), then
`822824F0` (`body_total=1280491`, peak `554835`, peak `ticks_per_entry=1`).
Do not start the `822824F0` `stvewx` peephole yet. The call-path audit now
exists: `docs/research/20260522-173542-82282490-call-path-audit.md`.
`tools/thor/thor_hir_call_path_report.ps1` shows the charged body-time wall is
inclusive call work: `822825E0 -> 0x82282490` has
`charged_body_total=34726883`, and `822825C8 -> 0x8227FEE8` has
`charged_body_total=3216407`. A targeted `8227FEE8` dump attempt in
`scratch/thor-debug/20260522-172738-*` idled before the route and emitted no
callee dump, with no searched fatal markers. The default-off call-edge profiler
now exists: `docs/research/20260522-181040-a64-call-edge-profiler.md`, cvar
`arm64_speed_profile_call_edge_filter`, and Thor flag
`-Arm64SpeedProfileCallEdgeFilter`. The first capture exposed and fixed an `x9`
callee-address clobber; fixed edge capture `scratch/thor-debug/20260522-175951-*`
and same-APK control `scratch/thor-debug/20260522-180335-*` both black-idled
before `82282490`, so no dynamic edge rows existed yet. The later call-edge
audit-only note
`docs/research/20260522-190502-call-edge-audit-only-and-edge-proof.md`
changes the read. The old edge-only idle was missing the route-stabilizing
delayed `82282490` body-time filter. New default-off
`arm64_speed_profile_call_edge_audit_only` logs compile-time direct-call slot
counts without emitting edge counters. Audit-only plus delayed body-time reached
the opening route and logged `blocks=164`, `direct_call_edges=60`,
`instrumentation=0`; audit-only without the body-time filter black-idled before
`82282490`. Real call-edge profiling plus delayed body-time reached opening and
produced dynamic edge rows: `822825E0 -> 82282490` dominates
(`body_ticks_total=21299726`, peak `7315115`), followed by
`822825C8 -> 8227FEE8` (`body_ticks_total=4515376`, peak `1933191`). Keep
`arm64_speed_profile_call_edge_filter` and
`arm64_speed_profile_call_edge_audit_only` default-off, but future edge
captures for this route should include `-Arm64SpeedProfileBodyTimeFilter` with
`82282490` and `-Arm64SpeedProfileBodyTimeAfterMs 120000`. Next slice should
focus `8227FEE8` or the recursive `822825E0 -> 82282490` child path using the
proven edge evidence, not the stale `822824F0` `stvewx` peephole.
The focused `8227FEE8` capture now exists:
`docs/research/20260522-193049-8227fee8-focused-capture.md`. Route-clean
capture `scratch/thor-debug/20260522-191427-*` reached the visible opening
sky/wing route with no searched fatal markers and measured `8227FEE8` at
`body_ticks_total=3650429`, peak delta `1408271`, peak `ticks_per_entry=738`,
and code size `49804`; `82282490` remained larger at
`body_ticks_total=25866736`. It did not produce HIR because filtered dumps were
info-level under the stable `log_level=1` speed lane, and a `-LogLevel 0` retry
black-idled. Filtered function dumps now log at warning level and Thor metadata
records `disassemble_function_filter`, but first patched filtered run
`scratch/thor-debug/20260522-192429-*` black-idled before route progress and
emitted no HIR/body rows. Do not rerun that exact filtered capture unchanged.
Next prove patched APK route safety with a no-filter control, or use a
control-sandwiched filtered capture before using `8227FEE8` HIR for codegen.
The no-filter control is now a negative route-safety result:
`docs/research/20260522-230909-patched-route-control-black-idle.md`.
`scratch/thor-debug/20260522-230518-*` used HEAD `0ec440af6`, APK SHA
`89086669EF6CC19A028049FBF5957827CF7CFA97C85F0083021A739B9C16FFAF`, no
disassembly filter, delayed body-time filter `82282490,8227FEE8`, and
`arm64_context_promotion_gpr_livein_r1=false`. It black-idled before opening:
counters went flat from `23:06:05`, body-time activated without any
`82282490`/`8227FEE8` rows, screenshot was black, and fatal-marker search was
clean. Idle snapshot was skipped because the processor debug lock was busy with
`last_global_owner_sys_tid=21741`.
Idle owner attribution and a patched route recheck now exist:
`docs/research/20260522-232945-a64-idle-owner-attribution.md`.
The A64 idle snapshot skip line now logs a lock-free native-TID hint
(`owner_hint`, guest thread ID, handle, and state) when the processor debug lock
is busy, and successful thread snapshots include `native=...`. Patched capture
`scratch/thor-debug/20260522-232133-*` used APK SHA
`E92DAC2CB4E7080C196DB9656305F372DC20C189E7697A2FCCD47D1E12DA3FA3`, reached
the visible opening sky/wing route with no searched fatal markers, and did not
exercise the owner-hint line because the route stayed active. Next run a
control-sandwiched filtered `8227FEE8` capture with delayed body-time route
stabilizer. If it reaches opening, use the warning-level HIR dump for focused
codegen audit; if it black-idles again, inspect the new `owner_hint` fields
before changing guest behavior.
The filtered `8227FEE8` sandwich now has a route-clean HIR middle and a
black-idle post-control:
`docs/research/20260522-234847-8227fee8-filtered-hir-sandwich.md`.
Filtered capture `scratch/thor-debug/20260522-233545-*` reached visible
opening, emitted the warning-level `8227FEE8` OptHIR dump, and had no searched
fatal markers. It measured `8227FEE8` as secondary
(`body_ticks_total=4125095`, `ticks_per_entry=264`, code size `49804`) while
`82282490` still dominated (`body_ticks_total=26728115`,
`ticks_per_entry=120`). The `8227FEE8` HIR shape is state-traffic heavy
(`store_context=1426`, `load_context=896`, `context_barrier=332`,
`branches=284`, `calls=78`) with a smaller `lvlx/lvrx/stvlx/stvrx` vector
surface in static blocks `82280A68` and `82280E1C`; no dot/extract/splat/stvewx
surface appears. Post-control `scratch/thor-debug/20260522-234038-*`
black-idled with clean fatal search and `owner_hint=miss` for
`last_global_owner_sys_tid=7347`, so route nondeterminism remains. Current best
next move: delayed block body-time for `8227FEE8` with
`-Arm64SpeedProfileBlockFilter 8227FEE8 -Arm64SpeedProfileBlockBodyTime true`.
Do not start a broad GPR cache, r1 live-in retry, or vector peephole until that
block attribution says which internal blocks actually burn time.
The first delayed `8227FEE8` block body-time run black-idled before target rows:
`docs/research/20260523-001018-a64-owner-thread-id-attribution.md`.
`scratch/thor-debug/20260522-235449-*` had a black screenshot, clean fatal
search, no `8227FEE8` body/block rows after activation, and another busy
processor debug-lock skip with `last_global_owner_sys_tid=14186` plus
`owner_hint=miss`. A diagnostic patch now records
`last_global_owner_thread_id`, queries owner hints by guest thread ID or handle
as well as native TID, and logs `owner_hint_source` plus
`owner_hint_sys_tid`. NativeCore and FullDeploy passed; patched APK SHA is
`962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5`. Short
validation `scratch/thor-debug/20260523-000506-*` stayed active at loading
with clean fatal search, so the new idle line still needs a black-idle capture.
The repeated delayed block body-time capture is now route-clean:
`docs/research/20260523-122718-8227fee8-block-body-time-route-clean.md`.
`scratch/thor-debug/20260523-122223-*` reached the visible opening sky/wing
route, had a clean fatal-marker search, and produced `8227FEE8` body/block
rows. Final `8227FEE8` body row: `body_ticks_total=6914659`,
`body_ticks_delta=1871176`, `entries_delta=2041`, `ticks_per_entry=916`,
`code_size=71868`. Dynamic block body-time says do not start the stale
`82280A68` / `82280E1C` vector peephole yet. The internal wall is call-heavy
block `822809F4` (`body_total=1937201`, peak `665665`, peak
`ticks_per_entry=128`) with direct calls to `0x8227F1D8` and `0x8247BE20`.
Next split those callees with delayed body-time and
`-Arm64SpeedProfileCallEdgeFilter 8227FEE8` before a codegen patch.
That callee split is now route-clean:
`docs/research/20260523-124029-8227fee8-callee-call-edge-split.md`.
`scratch/thor-debug/20260523-123406-*` reached visible opening on the same APK
SHA `962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5` with a
clean fatal-marker search. Final function body-time put `8227F1D8` first in
the last interval (`body_ticks_total=3714635`, `ticks_per_entry=84`), while
`8247BE20` stayed tiny (`body_ticks_total=27747`). The dominant dynamic edge
is `822809F4 -> 8227F1D8`: `calls_total=26098`,
`body_ticks_total=2031295`, peak delta `1137492`, peak
`ticks_per_call=216`. The next slice should dump/profile `8227F1D8` itself
with delayed block body-time before any codegen experiment.
That filtered HIR and block body-time capture is now route-clean:
`docs/research/20260523-131338-8227f1d8-filtered-hir-block-body-time.md`.
`scratch/thor-debug/20260523-130934-*` reached visible opening, emitted
`8227F1D8` OptHIR, and had a clean fatal search. Final `8227F1D8` function row:
`body_ticks_total=4238549`, `body_ticks_delta=907049`,
`entries_delta=9675`, `ticks_per_entry=93`, `code_size=28180`. Block body-time
is almost entirely block `8227F1D8`: `body_ticks_total=4152240`, peak delta
`1764204`, peak `ticks_per_entry=262`. That block has only `69` HIR
instructions but two calls, `0x82490030` and `0x826BFC7C`, so the next slice is
a call-edge split for `8227F1D8`, not a peephole.
That call-edge split is now route-clean:
`docs/research/20260523-153235-8227f1d8-call-edge-split.md`.
`scratch/thor-debug/20260523-152754-*` reached visible opening and had a clean
fatal-marker search. Final dynamic rows show `82490030` nearly fully explains
the parent: `8227F1D8 body_ticks_total=4117139`,
`82490030 body_ticks_total=4007328`, and
`8227F1D8 -> 82490030 body_ticks_total=4054641`, `calls_total=32107`,
`ticks_per_call=72`. `826BFC7C` did not appear as a meaningful dynamic row.
The next slice should dump/profile `82490030` itself with delayed block
body-time and `8227F1D8` as parent comparator before any codegen experiment.
The first filtered `82490030` attempt black-idled:
`docs/research/20260523-154117-82490030-filtered-capture-black-idle.md`.
`scratch/thor-debug/20260523-153726-*` had a black screenshot, clean fatal
search, and no `82490030` HIR/body/block rows. Counters went flat by
`15:38:15`, body-time activated later with `entry_delta=0`, and idle
attribution reported a busy processor debug lock with
`last_global_owner_thread_id=F80002E8`, `owner_hint=hit`,
`owner_hint_source=thread_id_or_handle`, and `owner_hint_state=zombie`. The
next slice should run a no-disassembly delayed body-time control for
`8227F1D8,82490030`; if that also black-idles, improve zombie owner/native TID
attribution before changing generated code.
That no-disassembly control also black-idled:
`docs/research/20260523-155012-82490030-control-black-idle.md`.
`scratch/thor-debug/20260523-154626-*` used the same APK SHA
`962D3086F4030D9BD5A9D46AF5E8DFA4A320A13BFCD14135B8B077AECDC31CC5`, no
disassembly filter, delayed body-time for `8227F1D8,82490030`, and
`arm64_context_promotion_gpr_livein_r1=false`. It produced no target body rows,
ended on a black screenshot with clean fatal search, and flatlined from
`15:47:15` onward. Idle attribution again points at
`last_global_owner_thread_id=F80002E8` with `owner_hint=hit`,
`owner_hint_source=thread_id_or_handle`, `owner_hint_state=zombie`, and a
suspect stale `owner_hint_sys_tid=946810032`. Do not repeat either 82490030
capture unchanged and do not patch generated code from this evidence. Next
slice should improve zombie owner/native TID attribution, especially live
`/proc/<pid>/task/<last_global_owner_sys_tid>` mapping and clearer separation
between the real native owner TID and stale XThread hint fields.
That native-TID attribution patch now exists:
`docs/research/20260523-160650-a64-owner-native-tid-attribution.md`.
It adds `Processor::OnThreadNativeStarted()` so Android hints store the real
Linux `gettid()` value after XThread startup, and it logs `/proc/self/task`
liveness for the lock owner and hint TIDs. Validation capture
`scratch/thor-debug/20260523-160357-*` used APK SHA
`2E3D88F46BB709AA3A869634C24219FBBA0568695C1F4902693132701CF9EBE5` and
reproduced the black-idle with clean fatal search. The new line reports
`owner_hint_source=system_tid`, `owner_hint_sys_tid=28245` matching
`last_global_owner_sys_tid=28245`, and both native liveness probes false while
`owner_hint_state=zombie`. Next slice should instrument/audit global
critical-region ownership and release lifetime around the processor debug lock,
not patch 82490030 generated code.
That owner-source attribution patch now exists:
`docs/research/20260523-232053-global-critical-owner-source-attribution.md`.
It records global critical-region owner sequence, owner age, and owner source
in the A64 idle skip line, and tags `Acquire`, `AcquireDirect`, `TryAcquire`,
`XThread::LockApc`, and `ObjectTable::LookupObject`. `NativeCore` and
`FullDeploy` passed with APK SHA
`D68ED877D6C265420CD6853AB6C108C18F17E23911C2A1135016797D345C4823`. Short
validation `scratch/thor-debug/20260523-231636-*` stayed active for 100 seconds
with clean fatal search but remained at the loading overlay and did not emit an
idle-snapshot skip line. Next run a longer route-stabilized attribution capture;
if it black-idles, inspect `global_lock_owner_seq`,
`global_lock_owner_age_ms`, and `global_lock_owner_source`. If it stays active
and reaches the visible route, return to measured performance profiling instead
of repeating stale `82490030` captures.
The first long follow-up reproduced black-idle:
`docs/research/20260523-232908-global-owner-source-black-idle.md`.
Capture `scratch/thor-debug/20260523-232432-*` on commit `94fb4546e` reported
`global_lock_count=0`, `global_lock_owner_source='Acquire'`,
`global_lock_owner_age_ms=25053`, dead native owner liveness, and zombie
`owner_hint_state`, so the source was useful but too generic. A follow-up patch
now exists: `docs/research/20260523-234203-processor-thread-lifecycle-owner-tags.md`.
It adds `Acquire(source)` and tags processor thread lifecycle methods. `NativeCore`
and `FullDeploy` passed with APK SHA
`862F86C44625B460A5BAB8528E25AB4E946F52CDB30137D7479D24AC3BD50FCB`. Short
validation `scratch/thor-debug/20260523-233953-*` stayed active for 100 seconds
at loading with clean fatal search and no idle-snapshot skip line. Next run a
longer tagged-lifecycle attribution capture before changing lock behavior or
returning to hotpath profiling.
The longer tagged-lifecycle capture identified and fixed the blocker:
`docs/research/20260523-235800-object-release-outside-global-lock.md`.
Capture `scratch/thor-debug/20260523-234500-*` on commit `11747d104` reproduced
black-idle with `global_lock_owner_source='Processor::OnThreadDestroyed'`.
The fix defers `object->Release()` in `ObjectTable::ReleaseHandle` and
`RemoveHandle` until after the object table/global critical-region lock is
dropped. `NativeCore` and `FullDeploy` passed with APK SHA
`FE5CBCF23E832807E51547D66387C3680DF6DB4B4802C9DB5BB64ACB46B8489B`.
Validation `scratch/thor-debug/20260523-235417-*` reached the visible sky/wing
opening route, had a clean fatal search, emitted no idle-snapshot skip line,
and restored body-time rows for `8227F1D8` and `82490030`. Resume measured
performance profiling from this route unless a fresh capture reopens the
`Processor::OnThreadDestroyed` black-idle.
The restored `82490030` call-edge split is route-clean:
`docs/research/20260524-001152-82490030-call-edge-split.md`.
Capture `scratch/thor-debug/20260524-000643-*` reached visible opening, had no
searched fatal markers, and emitted no idle owner lines. The dynamic child
summary puts `82486178` first (`body_ticks_total=2302621`,
`calls_total=33218`), then `82485DD8` (`804023`) and `82486018` (`679426`).
The call-path parser now supports dynamic-only call-edge logs without a HIR
dump. Next slice should dump/profile `82486178` with delayed body/block-time,
not patch local `82490030` codegen.

Do not restart the rejected broad `PERMUTE_I32` lane-replace helper, naive VMX
dot-product fastpath, non-constant V128 store cleanup, generic compare-branch
fusion, or old block-local/fallthrough context cache without new evidence.

## Stop Conditions

Stop and notify the user only when:

- the proof marker exists;
- max Stop-hook attempts are exhausted;
- ADB/device/build state is blocking further progress;
- two experiments in the same subsystem fail or go inconclusive and the next
  step needs new instrumentation or a user decision;
- continuing would require unsafe, copyrighted, or unsupported material.

Otherwise continue through the Stop hook or heartbeat using the most recent
evidence.
