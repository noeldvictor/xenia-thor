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

## Current Best Next Move

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
before `82282490`, so no dynamic edge rows existed yet. The follow-up sandwich
`docs/research/20260522-183742-call-edge-control-sandwich.md` is a stronger
negative for the current edge-filter path: controls
`scratch/thor-debug/20260522-182318-*` and
`scratch/thor-debug/20260522-183118-*` both reached opening and emitted
`82282490` body-time rows on the same APK, while edge capture
`scratch/thor-debug/20260522-182705-*` with
`arm64_speed_profile_call_edge_filter=82282490` black-idled by 18:27:52 and
emitted no dynamic edge rows. Keep the cvar default-off and do not rerun that
exact edge capture unchanged. Next slice should add a low-overhead
compile/activation audit, or make the profiler inert until the filtered
function is compiled/reached, then prove route safety before using edge rows.

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
