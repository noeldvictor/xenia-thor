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

Latest priority, superseding the older chronology below:
`docs/research/20260524-021116-82282490-recursive-stack-sync-probe.md` confirms
the Blue Dragon word-loop speed preset moved the wall back to recursive
`82282490` work. Fresh route-clean captures show
`822825E0 -> 82282490` dominates dynamic call-edge body time
(`body_ticks_total=28602334`, `calls_total=117425`, peak
`ticks_per_call=1244`), while `822825C8 -> 8227FEE8` is secondary
(`body_ticks_total=11951162`). Do not restart stale `822824F0` vector work from
entry counts alone. Android/Thor tooling now exposes
`a64_enable_host_guest_stack_synchronization`; keep the Blue Dragon speed-pack
default `true` for now. Stack-sync-off is route-clean twice and shrinks
generated code for `82282490` (`87168` -> `85104`), but the repeat off capture
landed at `82282490=27192157`, essentially matching the stack-sync-on control
(`27192906`). Treat this as code-size evidence only, not speed proof. Do not
run another unchanged stack-sync A/B; next slice should add stackpoint/prolog
overhead attribution or directly reduce recursive call/prolog cost.

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
