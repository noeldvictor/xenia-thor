---
name: xenia-a64-speed-hotpath
description: Use for Xenia Thor ARM64 performance work on AYN Thor Max, especially Blue Dragon 2 FPS runs, A64 speed-profile counters, hot guest PCs, PPC helper/prologue churn, guest-to-host transitions, direct/indirect call overhead, XMA/audio cost, and Snapdragon 8 Gen 2 profiling. AYN Thor ARM64 is the priority; Windows is only a control lane when it answers a specific semantics question.
---

# Xenia A64 Speed Hotpath

Use this skill when Blue Dragon gets past boot/title/opening but runs too slow,
or when the next question is "CPU or GPU?" on Thor. This is the speed lane for
the imported aX360e/Edge-style A64 backend, not the removed helper mini-JIT.

## Priority

- AYN Thor Max ARM64 truth comes first.
- Windows/x64 is a control microscope only when a suspected emulator semantics
  bug needs a faster comparison.
- Do not spend a speed run on broad GPU traces, shader dumps, disassembly dumps,
  live logcat, or RenderDoc unless the experiment explicitly targets that
  subsystem.
- Keep trace-heavy correctness captures separate from trace-off speed captures.

## Baseline Run

Build and deploy only when the native core changed:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Run the canonical profiled speed capture:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 100 -PerfSampleSeconds "45,90" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1
```

Blue Dragon capture modes stop Xenia after the final screenshot by default.
Keep that default for automated speed/title work so a frozen or idle emulator
process cannot keep chewing battery/CPU after the run. Pass
`-StopAppAfterCapture false` only for a deliberate manual inspection pass.

Blue Dragon speed/title defaults also force `arm64_context_value_cache=false`.
That block-local cache is a negative result, and Android's saved
`xenia.config.toml` can keep old experiment cvars alive if the launcher does not
send an explicit override.

Do not use `-XmaFastSilence true` as a general Blue Dragon speed preset.
Capture `scratch\thor-debug\20260521-145734-*` lowered CPU use but black-idled
with repeated `entry_delta=0`, while clean audio-on capture
`scratch\thor-debug\20260521-145533-*` progressed to the loading screen.

Then summarize the latest evidence:

```powershell
rg -n "A64 speed profile|A64 thread snapshot|last_global_owner_sys_tid|ResolvePath\\(cache|__savegprlr|__restgprlr|8246B408|8248B040|827294CC|8272A3A4|Fatal signal|AndroidRuntime|VK_ERROR_DEVICE_LOST|GPU is hung|XMA|top_threads" scratch\thor-debug
```

`tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` reapplies
the debug app-op for `MANAGE_EXTERNAL_STORAGE` after install. If a run shows
`funcs=0` forever, verify `cmd appops get jp.xenia.emulator.github.debug
MANAGE_EXTERNAL_STORAGE` before chasing A64.

The Thor debug launcher defaults `mount_cache=true` for Blue Dragon. Keep it on
for correctness runs; `MountCache false` can keep the game busier but leaves
`ResolvePath(cache:\pack) failed - device not found` noise in the route.

## Title Proof Lane

Use this lane to answer only "does Blue Dragon reach the visible title screen on
Thor?" It avoids the speed lane's auto START/A input and forced Vulkan signed
10:10:10:2 fallback. Default title capture is final-screenshot only; pass
`-TitleScreenshotSeconds "30,35,40,45"` only when the experiment needs timed
screenshots because extra screencaps can perturb this route.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonTitleCapture -DeviceSerial c3ca0370 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshot true -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -Arm64BlueDragonMemcpyFastpath true -Arm64BlueDragonStricmpFastpath true -Arm64BlueDragonJumpTableFastpath true
```

Known title proof: `scratch\thor-debug\20260520-011006-*` shows `press START`
with APK SHA `EA375B75215C12AC84EB5E121C7FC0AC191B189EAD9BBB97F333CC49B2372EF7`.
This is still timing-sensitive; same-cvar captures can black-idle when snapshot
sampling is disabled. Treat that as a route-determinism bug, not as
compatibility.

## Thread Snapshot Lane

Use the thread snapshot flag when the screen is black or the route's final
interval disagrees with `top -H`. It logs each guest thread's last A64 function,
guest return address, LR/CTR/R1/R3/R4, and retries briefly before giving up on
the processor debug lock. Skip lines include the last observed global critical
owner system TID and PPC global-lock owner breadcrumbs.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 50 -PerfSampleSeconds "45" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 10 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshot true
```

For timing-sensitive black-idle repros, prefer the idle-only snapshot first. It
does not sample during the boot/title transition; it logs one thread snapshot
only after counters have been active and a later interval goes flat.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonTitleCapture -DeviceSerial c3ca0370 -TitleScreenshotSeconds "0" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileThreadSnapshotOnIdle true -A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitInlineInCaller true -Arm64BlueDragonMemcpyFastpath true -Arm64BlueDragonStricmpFastpath true -Arm64BlueDragonJumpTableFastpath true
```

## Body-Time Lane

Use body-time counters when entry counts are misleading after helper fastpaths.
This is especially useful for Blue Dragon where `827294CC`, `826C5620`, and
`826BF770` can stay high-frequency even when each call is cheap.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 95 -PerfSampleSeconds "70" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "8272A3A4,8272A8E8,826C5620,827294CC,826BF770"
```

For the current Blue Dragon opening-scene route, delay body-time activation
until after the title/input transition. Body-time from boot
`scratch\thor-debug\20260521-164744-*` black-idled after 16:48:32, while the
delayed route `scratch\thor-debug\20260521-165657-*` reached the rendered
opening scene and identified `82282490` as the true body-time leader.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64SpeedProfileBodyTimeFilter "82274DB0,82287788,826BF770,82274E38,82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Current read: high-entry `82274DB0`, `826BF770`, and `82274E38` are cheap
one-ish-tick helpers in the opening scene. Prioritize the large
`82282490` / `82281D28` cluster before chasing those tiny helpers.

## Block Profiler Lane

Use block counters only after body time identifies one concrete function. The
filter is exact-start scoped for single addresses, so `8272A3A4` should only
instrument the function whose start address is `8272A3A4`.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 45 -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "8272A3A4" -Arm64SpeedProfileBlockFilter "8272A3A4"
```

Current `82282490` opening-scene block profile:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -A64LseKernelLockFastpaths true -A64RtlLeaveFastpathAudit false -Arm64SpeedProfileBodyTimeFilter "82282490" -Arm64SpeedProfileBodyTimeAfterMs 120000 -Arm64SpeedProfileBlockFilter "82282490" -StopAppAfterCapture true
```

Capture `scratch\thor-debug\20260521-170107-*` reached the opening
`Microsoft Game Studios Presents` scene and kept `82282490` body-time top.
The hot block PCs to classify next are `822824B8`, `822825F4`, `822824F0`,
`822825E0`, `8228252C`, `82282490`, `82282600`, `822824EC`, and `822825C8`.

Use the offline HIR hotpath report before another `82282490` edit when a
filtered dump exists:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_hotpath_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Top 20
```

Known `82282490` OptHIR baseline from that tool:
`hir_lines=6799`, `store_context=1415`, `load_context=763`,
`dot_product_4=136`, `permutes=381`, `permute.2=236`, and
`context_barriers=213`. See
`docs/research/20260521-182504-hir-hotpath-report-tool.md`.

The report now annotates context offsets with PPC context field names. Current
`82282490` class totals are loads `GPR=546`, `VMX=155`, `FPR=38`, and stores
`GPR=562`, `VMX=373`, `CR=343`, `LR/CTR=68`. Top concrete state slots are
`r[1]`, `r[11]`, `r[10]`, `r[29]..r[31]`, `v[0]`, `v[11]..v[13]`, and
`cr6.all_equal/cr6.none_equal`. Use this to design a real state-cache or
state-traffic pass before another isolated vector micro-peephole. See
`docs/research/20260521-195741-hir-context-offset-annotations.md`.

Use the state-span report when deciding which context slots are worth caching
across HIR blocks or barriers:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_state_span_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Top 20
```

Current `82282490` span read: `r[1]` dominates cross-span repeated loads, while
`r[11]`, `r[10]`, `r[31]`, `r[29]`, and `r[30]` are the leading cross-span
load-after-store GPRs. That points toward a real GPR state cache with explicit
flushes at helpers, exits, exceptions, and aliasing barriers. See
`docs/research/20260521-210004-hir-state-span-report.md`.

The state-span report now also emits a `Candidate GPR State Cache Plan`. On the
current `82282490` dump, the top scores are `r[1]=220`, `r[11]=169`,
`r[10]=98`, `r[31]=92`, `r[29]=78`, `r[30]=71`, and `r[28]=69`. Treat the
first implementation as a guarded clean-value cache only: preserve clean INT64
GPR knowledge across no-op `context_barrier`, reset on calls, branches, labels,
helper-expanded instruction ranges, volatile ops, and overlapping writes, and
do not elide stores yet. See
`docs/research/20260521-211840-gpr-state-cache-candidate-plan.md`.

The first emit-time version of that idea is a negative result. Keep
`arm64_context_value_cache_preserve_barrier` default-off: capture
`scratch\thor-debug\20260521-212305-*` reached the visible opening route, but
`82282490` logged `loads/hits=546/0` even with `barrier_preserves=213`. It did
cache stores (`r[11]=110`, `r[10]=64`, `r[31]=19`), but
`register_invalidations=768` killed all reuse. The next real attempt should be
HIR-level GPR load promotion before A64 register allocation or a pinned-register
experiment for `r[1]`/`r[11]`, not another emit-time cache-preservation tweak.
See `docs/research/20260521-212700-a64-gpr-cache-barrier-negative.md`.

Before changing `ContextPromotionPass` for cross-block GPR work, run the
promotion audit:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_gpr_promotion_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Top 20
```

Current audit read: `82282490` has `118` blocks, `546` whole-GPR loads,
`562` whole-GPR stores, `29` dominated single-predecessor blocks, and `61`
first whole-GPR loads in those blocks. Top dominated first-load candidates are
`r[1]` (`score=32`), `r[11]` (`13`), and `r[10]` (`11`). Important gate:
the PPC translator does not currently run `DataFlowAnalysisPass`, so do not
wire predecessor `Value*` objects directly into successor blocks. The next
runtime patch needs local-slot lowering or a guarded data-flow stage before
`RegisterAllocationPass`. See
`docs/research/20260521-213650-hir-gpr-promotion-audit.md`.

The first guarded local-slot version is route-clean but not a speed win. Keep
`arm64_context_promotion_gpr_local_slots` default-off. Capture
`scratch\thor-debug\20260522-113303-*` enabled it only for `82282490`, reached
the visible opening sky/dragon-wing route, and had no searched fatal markers,
but `82282490` grew from `87168` to `87660` bytes and comparable
ticks-per-entry were flat to slightly worse. See
`docs/research/20260522-113012-gpr-local-slot-promotion-probe.md`.

The counter follow-up explains why that shape lost. Capture
`scratch\thor-debug\20260522-114838-*` reached the same visible opening route
with no searched fatal markers and logged `82282490` local-slot counts:
`103` local stores for only `3` replaced loads. `r[11]` had `68` local stores
for `0` replaced loads; `r[1]` had `35` local stores for `3` replaced loads.
Do not tune the local-slot bridge next. Try a lower-overhead pinned-GPR path
for `r[1]` first, with explicit resets for helpers, exits, exceptions,
conditional branches, returns, traps, multi-predecessor joins, volatile ops,
and overlapping context writes. See
`docs/research/20260522-114745-gpr-local-slot-promotion-counters.md`.

The first pinned `r[1]` version is also not a speed win. Keep
`arm64_context_pinned_gpr_r1` and
`arm64_context_pinned_gpr_r1_fallthrough` default-off. Final APK control
`scratch\thor-debug\20260522-123536-*` reached the loading spinner with
`82282490 code_size=87168`; pinned no-fallthrough
`scratch\thor-debug\20260522-123918-*` was route-clean but logged
`loads/hits=107/0`, `pin_loads=107`, and grew `82282490` to `87596` bytes.
Pinned fallthrough `scratch\thor-debug\20260522-123232-*` black-stalled before
`82282490`. Do not tune emit-time `x29` pinning next. First classify the
`r[1]` loads by block/predecessor/alias shape, or move the state-cache design
before A64 register allocation. See
`docs/research/20260522-123855-a64-pinned-r1-cache-probe.md`.

The follow-up load-shape report did that classification. Use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_gpr_load_shape_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Gpr 1 -Top 25
```

Current `r[1]` result: `107` exact loads, `11` exact stores, `0` aliasing
stores, `76` first loads in their block, `87` loads in multi-predecessor
blocks, and `31` loads after a context barrier before the next branch. This
matches the zero-hit pinned cache result. Do not implement another post-RA
cache for this path. Next add a CFG/live-in availability report or guarded
pre-register-allocation GPR state-cache design that proves all predecessors
leave `r[1]` clean before replacing loads. See
`docs/research/20260522-125206-r1-load-shape-report.md`.

The live-in availability report is now available:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_gpr_livein_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Gpr 1 -Top 40
```

Current read: strict helper+barrier flushing exposes only `14` replaceable
first `r[1]` loads. Helper flushing with `context_barrier` preservation exposes
`56` replaceable first loads and `61` replaceable loads total. Next runtime
patch should be a default-off, function-filtered, audited pre-RA `r[1]`
live-in/state-cache probe for `82282490`, with explicit kills at calls/helpers,
exits, exceptions, volatile context ops, and exact/aliasing writes. See
`docs/research/20260522-150536-r1-livein-availability-report.md`.

The first guarded live-in implementation is route-clean but under-replaces.
Keep `arm64_context_promotion_gpr_livein_r1` default-off. Capture
`scratch\thor-debug\20260522-152727-*` had no searched fatal markers and logged
`loads_attempted=107`, `loads_replaced=16`, `loads_seeded=91`,
`call_resets=91`, and `82282490 code_size=87224`. Do not start another long
Thor speed run from this toggle yet. Next add actual-CFG/dirty-reason audit or
improve the pre-RA carrier until the runtime replacement count approaches the
offline `56` first-load opportunity. See
`docs/research/20260522-153742-r1-livein-state-cache-probe.md`.
The next code fix preserves the carrier across conditional branches:
`docs/research/20260522-154921-r1-livein-branch-preserve.md`. HIR marks
`branch_true` and `branch_false` volatile, but the explicit r1 edge carrier
must see their successor edges rather than killing state there. NativeCore
builds. Next run an audited r1 live-in capture and require roughly `50+`
runtime replacements before any quiet FPS comparison.
The audited replacement target is now met. Capture
`scratch\thor-debug\20260522-155827-*` logged `loads_attempted=107`,
`loads_replaced=64`, `loads_seeded=43`, `local_loads=59`,
`local_stores=17`, `call_resets=43`, and `branch_preserves=153`, with no
searched fatal markers. See
`docs/research/20260522-160205-r1-livein-audit-capture.md`. The follow-up
quiet A/B did not prove a speed win. Control
`scratch/thor-debug/20260522-161306-*` reached the visible opening sky/wing
route and logged `82282490` body-time rows at code size `87168`; live-in-on
`scratch/thor-debug/20260522-161710-*` stayed at the loading spinner and logged
no `82282490` body-time rows after activation. See
`docs/research/20260522-162135-r1-livein-quiet-ab.md`. Keep the toggle
default-off. Do not repeat this exact A/B unchanged; use one control-sandwich
confirmation if needed, otherwise move to the next `82282490` hotpath.

The block-mix report is now the next offline filter:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_block_mix_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -BlockProfileLog scratch\thor-debug\20260521-170107-speed-logcat.txt -Top 20
```

Current read: dynamic entry-count hot blocks are the early control/state path
`822824B8`, `822824F0`, `822825E0`, `822825F4`, `822825C8`, `82282490`,
`82282600`, and `822824EC`. Static context/vector-heavy blocks such as
`82282CE4`, `82283DBC`, `822836C8`, `82283828`, and `822847E8` need block
body-time proof before broad vector work. Next patch should inspect or audit
dynamic-hot mixed block `822824F0`, or add lower-noise block body-time
instrumentation first. See
`docs/research/20260522-163537-82282490-block-mix-report.md`.

For a single-block audit, use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_block_detail_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -BlockGuest 822824F0 -Top 20
```

Current `822824F0` read: `198` HIR instructions, `22` context loads,
`43` context stores, `16` memory loads, `4` memory stores, `3` permutes,
`3` `mul_add`, `6` splats, `9` extracts, two calls (`0x82274DB0`,
`0x82287788`), and `5` context barriers. The block mixes CR6 gates,
stack/call setup, vector math/store (`vspltw`, `vmaddfp`, `stvewx`),
FPR/FPSCR work, and CR tail branches. Do not patch it from entry counts alone;
next add per-block body-time attribution or an A64 `stvewx` / `extract` /
`splat` codegen audit first. See
`docs/research/20260522-164404-822824f0-hir-profile-audit.md`.

Clean route after the reverted broad lane-replace probe:
`scratch\thor-debug\20260521-182630-*` reached the opening route again on
HEAD `5aaf0d776` with APK SHA
`FE1CA12C2B572C5643775702DF68D3DEA4A45661B2A2DCD7A8694EAF45F26312`.
`82282490` stayed at `code_size=87168`; final thread sample was Main Thread
about `92.3%`, XMA Decoder about `46.1%`, GPU Commands about `11.5%`. See
`docs/research/20260521-183001-clean-route-rebaseline.md`.

HIR/disassembly capture `scratch\thor-debug\20260521-170941-*` classifies
`82282490` as a large VMX-heavy routine, not a tiny helper. The visible slice
is dominated by `load_context`, `store_context`, `byte_swap`, branches,
`vmrghw` / `permute.2`, `vmsum4fp128` / `dot_product_4`, `stvx`, and `lvx`.
The broad `arm64_vmx_dot_f32_fastpath` experiment is a negative result:
`scratch\thor-debug\20260521-171859-*` black-idled with the toggle true, while
same-APK capture `scratch\thor-debug\20260521-172247-*` reached the opening
sky/dragon-wing scene with it false. Keep it default-off. Prefer
semantics-preserving direct `PERMUTE_I32` / `vmrghw` lowering, exact
byte-swap/store fusion, and vector state-traffic reduction before touching dot
product again. See
`docs/research/20260521-172826-blue-dragon-vmx-dot-negative.md`.

`arm64_permute_i32_zip_fastpath` is the current safe vector win and should stay
default-on for Thor unless a later A/B regresses. It maps exact `PERMUTE_I32`
controls `0x05010400` (`vmrghw`) and `0x07030602` (`vmrglw`) to NEON
`zip1 .s4` and `zip2 .s4`; use `-Arm64PermuteI32ZipFastpath false` as the
rollback. Proof `scratch\thor-debug\20260521-173359-*` reached the opening
sky/dragon-wing scene and shrank `82282490` code size to `87168`, while
same-APK rollback run `scratch\thor-debug\20260521-173734-*` black-idled before
body-time activated. See
`docs/research/20260521-174106-blue-dragon-permute-i32-zip-fastpath.md`.

Do not re-add the broad `PERMUTE_I32` lane-replace helper. Offline
`82282490` HIR shows useful-looking non-zip masks, but the generic helper
black-idled both `scratch\thor-debug\20260521-181513-*` and
`scratch\thor-debug\20260521-181920-*` before `82282490` body-time activated.
Retry only as an exact-mask experiment with correctness proof. See
`docs/research/20260521-182124-permute-i32-lane-replace-negative.md`.

Do not re-add the reverted non-constant swapped `STORE_V128` address-spill
cleanup as a casual follow-up. The local change built, but
`scratch\thor-debug\20260521-174627-*` black-idled before delayed body-time
activated, and the code was reverted before commit. Only retry it with an audit
that proves `82282490` hits and a route-clean opening-scene capture. See
`docs/research/20260521-175053-v128-store-address-spill-negative.md`.

`ContextPromotionPass` is now range-aware for context load reuse and
block-local context-store DSE. Keep the change as optimizer foundation, but do
not treat it as a speed win: proof `scratch\thor-debug\20260521-180212-*`
reached the opening sky/dragon-wing scene with APK SHA
`3A9C1EF2FB39F2DA4ACFA1B8C969A06D106439DBBFF1556D3084D1AA7A3CDCF3`, while
`82282490` stayed at `code_size=87168`. Runtime `82282490` context-audit
capture `scratch\thor-debug\20260521-175626-*` black-idled before delayed
body-time activated, so use low-noise compile-time summaries before another
context/state-cache sprint. See
`docs/research/20260521-180835-context-promotion-range-aware.md`.

Treat block-profiler runs as trace-heavy diagnostics. Harvest the first useful
interval, then return to a clean speed capture before judging progress. The
first `8272A3A4` run found hot guest block PCs `8272A8B4`, `8272AA50`,
`8272A3F4`, `8272A474`, `8272A548`, and `8272A424`.

Do not enable `arm64_blue_dragon_jump_table_inline_in_caller` by default.
Post-input-fix retest `scratch\thor-debug\20260521-164314-*` black-idled with
`entry_delta=0` from 16:44:02 onward. Keep it as a diagnostic only.

Do not reintroduce a partial `8272A3A4:8272A8B4` byte-copy splice without a new
full-region proof. The 2026-05-19 attempt either crashed at `8272A8B4` or
black-idled the route after resuming at `8272A8D0`; see
`docs/research/20260519-234533-blue-dragon-copy-fastpath-dead-end.md`.

Do not reintroduce wrapped-immediate `ADD`/`SUB` lowering by default without a
lowering audit first. The 2026-05-20 broad I32/I64 pass
`scratch/thor-debug/20260520-143752-*` and the narrower `ADD_I32`-only pass
`scratch/thor-debug/20260520-144309-*` both black-idled Blue Dragon; restored
default donor lowering `scratch/thor-debug/20260520-144638-*` resumed healthy
entry deltas. See
`docs/research/20260520-144829-a64-wrapped-addi-bisect.md`.

Exception: the audited `ADD_I64 reg, reg, wrapped-small-negative` case is now
route-proven and default-on. Capture `scratch\thor-debug\20260520-150652-*`
confirmed `arm64_add_i64_wrapped_imm_fastpath=true`, active Blue Dragon
counters through 60 seconds, and `8272A3A4 code_size=12772`. Roll it back with
`-Arm64AddI64WrappedImmFastpath false` if a future route regresses. Do not
extend this exception to `ADD_I32`, `SUB_I32`, or `SUB_I64` without a separate
audit proof. See
`docs/research/20260520-151030-a64-add-i64-wrapped-immediate-fastpath.md`.

The 2026-05-20 I64 logical-immediate pass is route-proven and should stay in
the generic A64 backend: `AND_I64`, `AND_NOT_I64` with constant second operand,
`OR_I64`, and `XOR_I64` now emit direct A64 logical immediates when masks are
encodable and keep the old scratch-register fallback otherwise. Capture
`scratch\thor-debug\20260520-152048-*` stayed active through 60 seconds and
shrunk `8272A3A4` to `code_size=12652`. See
`docs/research/20260520-152237-a64-i64-logical-immediate-lowering.md`.

The follow-up `AND_NOT_I8/I16/I32 reg, reg, const` cleanup is route-clean but
not a measured Blue Dragon hot-block win. Capture
`scratch\thor-debug\20260520-152601-*` stayed active through 60 seconds, while
`8272A3A4` and `8272A8E8` remained at `code_size=12652` and `5600`. Keep it as
generic codegen cleanup; use an immediate-lowering hit audit before broadening
more shapes. See
`docs/research/20260520-152733-a64-and-not-logical-immediate-lowering.md`.

Use the logical immediate audit before chasing more constant-materialization
work in a hot function:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 45 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64ImmediateLoweringAudit true -Arm64ImmediateLoweringAuditFunction 0x8272A3A4 -Arm64ImmediateLoweringAuditBudget 256
```

Capture `scratch\thor-debug\20260520-154135-*` proved `8272A3A4` has no
remaining logical-immediate `mov+reg` misses in the audited shapes: 38
`AND_I8` rows and 30 `AND_I64` rows were all `logical-imm`. The final interval
idled with audit logging active, so use this lane for translation evidence, not
speed comparison. For high guest address filters, pass hex; the Thor script
converts to signed Android `--ei` extras. See
`docs/research/20260520-154650-a64-immediate-lowering-audit.md`.

Global audit `scratch\thor-debug\20260520-154815-*` found 44 `OR_I32 imm 0`
fallbacks in the first 256 logical-immediate rows. Those now lower through the
zero fastpath as `action identity`; `AND reg, 0` also lowers as `action zero`.
Post-change audit `scratch\thor-debug\20260520-155154-*` left only two
first-budget `mov+reg` rows, both `AND_I64` masks (`0x6001007D` and `0xEF`).
Quiet proof `scratch\thor-debug\20260520-155321-*` stayed route-clean through
60 seconds. See
`docs/research/20260520-155700-a64-zero-logical-immediate-fastpath.md`.

The 2026-05-20 integer zero-select pass is a small generic cleanup for the
Blue Dragon shift/select pattern in `8272A3A4`: `SELECT_I8/I16/I32/I64` now
uses `wzr` / `xzr` directly when either value operand is constant zero. Repeat
proof `scratch\thor-debug\20260520-160757-*` stayed active through 70 seconds
and shrank `8272A3A4` from `12652` to `12540` bytes and `8272A8E8` from `5600`
to `5592` bytes. The earlier `scratch\thor-debug\20260520-160530-*` attempt
idled later, so keep treating Blue Dragon speed routes as timing-sensitive.
See `docs/research/20260520-161130-a64-zero-select-fastpath.md`.

The 2026-05-20 unsigned zero-compare pass folds integer compares that are
forced by an unsigned zero bound, such as `COMPARE_ULT x, 0 -> false` and
`COMPARE_UGE x, 0 -> true`. Proof
`scratch\thor-debug\20260520-161344-*` stayed active through 70 seconds and
shrunk `8272A3A4` from `12540` to `12432` bytes and `8272A8E8` from `5592` to
`5552` bytes. See
`docs/research/20260520-161650-a64-unsigned-zero-compare-fold.md`.

The 2026-05-20 compare-to-context-store peephole is the next proven CR churn
cleanup. A zero-store-only build `scratch\thor-debug\20260520-162727-*` reached
the Blue Dragon Voice Language menu but did not shrink the hot functions. The
actual win was fusing always-false unsigned zero compares whose only use is the
immediately following `store_context`; proof
`scratch\thor-debug\20260520-163134-*` reached the same menu with no searched
fatal markers and shrank `8272A3A4` from `12432` to `12332` bytes and
`8272A8E8` from `5552` to `5520`. See
`docs/research/20260520-163450-a64-compare-store-context-peephole.md`.

Do not re-add the uncommitted `COMPARE_UGT x, 0` plus `COMPARE_EQ x, 0`
context-store pair peephole without an audit counter first. The local
`scratch\thor-debug\20260520-163945-*` proof reached the Voice Language menu,
but clean hot function sizes stayed at `8272A3A4=12332` and `8272A8E8=5520`.
See `docs/research/20260520-164105-a64-ugt-eq-context-pair-nohit.md`.

The proven successor was the 2026-05-20 UGT/EQ CR branch peephole. It matches
when `COMPARE_UGT` and same-operand `COMPARE_EQ` store to adjacent PPC CR
`GT`/`EQ` bytes and emits one `cmp`, two `cset`/`strb` pairs, plus direct
`b.eq` / `b.ne` for an immediate branch. First proof
`scratch\thor-debug\20260520-170433-*` had no searched fatal markers and shrank
clean code size from `8272A3A4=12332` to `12296` and `8272A8E8=5520` to
`5508`. The later CR-shape audit proved the original only-use guard was too
strict; see the relaxed peephole note below before editing this path again. See
`docs/research/20260520-170621-a64-ugt-eq-cr-branch-peephole.md`.

Do not re-add the broad integer compare-branch fusion tried in
`scratch\thor-debug\20260520-171056-*`. It matched `COMPARE_*` followed by a
single-use branch and lowered it as `cmp` plus `b.cond`, but Blue Dragon
black-idled after the early burst with `entry_delta=0` in the final intervals.
The local code was reverted before commit. See
`docs/research/20260520-171256-a64-compare-branch-black-idle.md`.

The 2026-05-20 Edge-style global reservation helper import is default-off. It
adds `TryAcquireReservationHelper`, `ReservedStore32Helper`, and
`ReservedStore64Helper`, plus `-Arm64GlobalReservationHelpers true`, but the
default-on experiment `scratch\thor-debug\20260520-172518-*` black-idled after
the early burst. Keep it as opt-in PPC sync infrastructure, not a Blue Dragon
speed fix. See
`docs/research/20260520-173242-edge-reservation-helper-import.md`.

## External PPC To A64 Reference Lane

Use this lane when the work feels like isolated peepholes instead of backend
maturity. The 2026-05-20 source harvest says the useful public references are:

- Dolphin `JitArm64`: best direct PPC-to-AArch64 JIT structure. Study its
  pinned PPC state/membase registers, GPR/FPR/CR register cache, branch/CTR/LR
  lowering, block linking, and last-use-driven flushes. Do not copy GPL code
  bodies into this fork without a deliberate license decision.
- QEMU TCG: best mature DBT architecture reference for translation blocks,
  direct block chaining, `lookup_and_goto_ptr`, helper global read/write
  metadata, PPC CR globals, and reservation/atomic semantics. Use as a design
  and semantics oracle, not as embedded code.
- RPCS3 PPU/LLVM/AArch64: useful second-backbone reference for function/module
  analysis, local/global PPU state separation, LLVM lowering, AArch64 transform
  passes, and leaf-block handling. Treat LLVM as a parallel research route
  after the direct A64 hot path is instrumented.
- IBM PowerPC branch docs: sanity check for CR/LR/CTR and BO/BI branch
  semantics before broadening compare/branch fusions.

For Blue Dragon, translate this into a state-traffic sprint:

1. First add an audit for `8272A3A4` that counts context loads/stores, CR
   loads/stores, LR/CTR traffic, helper calls, direct exits, indirect exits,
   endian swaps, and dispatcher returns.
2. Then add a Dolphin-style PPC state cache at the HIR/A64 boundary, starting
   with CR bytes and common hot GPR context slots.
3. Flush only at helpers, exits, exceptions, and exact aliasing barriers.
4. Use QEMU-style helper classification to keep state live across helpers that
   are proven not to mutate guest context.
5. Only use LLVM/RPCS3 ideas for a larger hot-function comparison after the
   direct A64 audit gives us the concrete waste map.

See `docs/research/20260520-180132-powerpc-to-arm64-source-harvest.md`.

## Context Traffic Lane

Use this before another `8272A3A4` codegen pass:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 50 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "8272A3A4" -Arm64ContextTrafficAudit true -Arm64ContextTrafficAuditFunction 0x8272A3A4 -Arm64ContextTrafficAuditBudget 4
```

Known audit proof: `scratch\thor-debug\20260520-181744-*`; route-clean
post-triplet proof: `scratch\thor-debug\20260520-182128-*`.

`8272A3A4` currently reports `context_loads=255`, `context_stores=442`,
`ppc_stores=252 GPR + 183 CR`, and 85 context barriers. Top CR stores are
`0xA3C..0xA3E` (CR6). Top GPR load/store slots are around `r29`, `r31`, `r1`,
`r30`, `r27`, `r23`, `r11`, and `r10`.

The exact `LT/GT/EQ` CR triplet peephole is route-clean but did not shrink the
hot function: `8272A3A4 code_size` stayed `12544` in
`scratch\thor-debug\20260520-182128-*`. Do not count it as a speed win. Next
step is a CR shape hit audit or a narrow GPR context-cache experiment, not more
blind compare fusions.

See `docs/research/20260520-182253-a64-context-traffic-audit.md`.

The 2026-05-20 CR-shape audit found why the strict triplet missed:
`8272A3A4` has 60 exact CR triplets and 50 `UGT/EQ` pairs, but zero triplets
passed the old only-use guard because Blue Dragon keeps compare values alive for
later users. The relaxed lowering still materializes each compare result with
`cset` into its assigned HIR value register, then skips the redundant adjacent
compare/store work. Proof `scratch\thor-debug\20260520-183741-*` had no
searched fatal markers and shrank `8272A3A4` from `12544` to `12196`, and
`8272A8E8` from `5508` to `5356`. The run still black-idled later, so this is a
generated-code shrink, not an FPS breakthrough. See
`docs/research/20260520-184020-a64-cr-shape-relaxed-peephole.md`.

The 2026-05-20 block-local context value cache is a negative result, not a
speed win. With `arm64_context_value_cache=true`, `8272A3A4` reported
`loads/hits=255/0` twice despite 240 cacheable stores. Keep it default-off and
do not count simple same-block emit caching as the state-cache answer. The next
state-cache step must be a cross-block PPC GPR/CR cache with helper, exit,
exception, and aliasing flush rules. See
`docs/research/20260520-192930-a64-context-cache-and-spinlock-fastpaths.md`.

The 2026-05-21 fallthrough context-cache extension is also a negative result.
Even with `arm64_context_value_cache=true` and
`arm64_context_value_cache_fallthrough=true`, `8272A3A4` still reported
`loads/hits=255/0` and `fallthrough_preserves=0`. Keep both cache cvars forced
off in Blue Dragon presets unless a run explicitly asks for them.

Do not enable `arm64_cr_compare_branch_across_context_barrier` or
`arm64_cr_store_elide_for_fused_branch` in the Blue Dragon speed pack. The HIR
dump showed tempting `compare -> CR store triplet -> context_barrier -> branch`
shapes, but broadening CR branch fusion across the barrier and eliding CR
stores caused guest crashes. The safe CR triplet path must keep interleaved
`cset`/`strb` ordering because multiple compare values can share one host
register. See
`docs/research/20260521-153300-a64-context-cache-cr-branch-negative.md`.

Raised-IRQL spinlock exports are now an HLE/JIT speed lane. The A64 backend
inlines `KeAcquireSpinLockAtRaisedIrql`,
`KeTryToAcquireSpinLockAtRaisedIrql`, and
`KeReleaseSpinLockFromRaisedIrql` by default. Proof
`scratch\thor-debug\20260520-192739-*` reached the Blue Dragon Voice Language
menu with `a64_inline_kernel_spinlock_exports=true` and
`a64_inline_kf_lower_irql=false`; guest-to-host calls dropped versus the
previous healthy route. Keep `a64_inline_kf_lower_irql` default-off: the naive
IRQL-store inline skipped APC delivery and black-idled
`scratch\thor-debug\20260520-192530-*`.

`a64_lse_kernel_lock_fastpaths` is default-on for Thor and uses the existing
`kA64EmitLSE` feature check before emitting ARMv8.1 LSE atomics for hot kernel
lock/IRQL helpers. It shrinks `RtlEnterCriticalSection`, `RtlLeaveCriticalSection`,
`KeRaiseIrqlToDpcLevel`, `KeAcquireSpinLockAtRaisedIrql`, and
`KeReleaseSpinLockFromRaisedIrql` versus the old exclusive-loop paths. A/B proof:
`scratch\thor-debug\20260521-155831-*` reached the Blue Dragon Voice Language
screen with `-A64LseKernelLockFastpaths true`, while the same APK black-idled in
`scratch\thor-debug\20260521-155946-*` with the toggle false. Keep the rollback
switch available; this is backend maturity, not the final FPS fix. See
`docs/research/20260521-160124-a64-lse-kernel-lock-fastpaths.md`.

The speed/title automation now requires nop HID keystroke events as well as
button state. Before the fix, the title route could sit forever at visible
`press START` because Blue Dragon polls `XamInputGetKeystroke` in the menu path.
The fixed nop driver emits scheduled START/A key-down and key-up transitions.
Proof `scratch\thor-debug\20260521-163237-*` reached the loading spinner, and
`scratch\thor-debug\20260521-163453-*` reached the opening rendered sky/wing
scene by 180 seconds with APK SHA
`FB4877DF6BEA31D86B8354632668A36BDAD134D48738132E26813FD7C5F631B6`. See
`docs/research/20260521-164045-blue-dragon-nop-keystroke-route-fix.md`.

Do not enable `arm64_blue_dragon_stricmp_deferred_cr_fastpath` in the speed
pack. Capture `scratch\thor-debug\20260521-161210-*` crashed the guest at PC
`826A2498` with it on. Do not enable
`arm64_blue_dragon_jump_table_inline_in_caller` until it has a fresh same-route
A/B after the nop keystroke fix; the pre-fix short run looked promising for
`827294CC`, but the longer route evidence was contaminated by menu automation.

`RtlLeaveCriticalSection` now has a default-on uncontended final-unlock inline.
It only handles owner-current, `recursion_count == 1`, and `lock_count == 0`;
if a waiter races in, it restores owner/recursion and falls back to native HLE
so the wake path remains native. Proof `scratch\thor-debug\20260520-220613-*`
reached the Blue Dragon Voice Language screen with
`a64_inline_rtl_leave_final_unlock=true`, `a64_inline_kf_lower_irql=false`, and
no searched fatal markers. Roll back with
`-A64InlineRtlLeaveFinalUnlock false`; the cleaned off path should keep
`RtlLeaveCriticalSection code_size=448`, while the on path is `528`. If a
no-snapshot speed run black-idles, compare with the toggle off before blaming
this path because both on and off captures idled in the same cleaned APK.

Use `-A64RtlLeaveFastpathAudit true` when the next question is whether the
critical-section leave fastpath is carrying its weight. It is default-off and
diagnostic: proof `scratch\thor-debug\20260520-222648-*` reached Voice
Language and reported final totals `final_inline=195628`,
`recursive_inline=23189`, `native_fallback=3028`, and `restore_slow=14`.
`a64_rtl_enter_free_first` is default-off: the free-lock-first enter reorder
black-idled `scratch\thor-debug\20260520-223025-*`, while the same APK with
`-A64RtlEnterFreeFirst false` reached Voice Language in
`scratch\thor-debug\20260520-223155-*`.

`KfLowerIrql` remains a timing-sensitive native boundary. The APC-guarded
inline probe is default-off and diagnostic only:
`-A64InlineKfLowerIrqlApcGuard true -A64KfLowerIrqlApcGuardAudit true`
optionally combined with `-A64KfLowerIrqlApcGuardNativePollInterval N`.
Blue Dragon black-idled with no-poll (`scratch\thor-debug\20260521-001506-*`),
poll-64 (`scratch\thor-debug\20260521-002131-*`), and poll-4
(`scratch\thor-debug\20260521-002312-*`), while the same APK reached Voice
Language with the guard off (`scratch\thor-debug\20260521-001638-*`). The guard
can reduce G2H calls, but it is not a speed win until a native
`KfLowerIrql`/`CheckApcs()` audit proves the host scheduling/APC cadence that
must be preserved. See
`docs/research/20260521-002511-a64-kf-lower-irql-apc-guard.md`.

## Classification

Read the final speed-profile interval first.

- If `__savegprlr_*` or `__restgprlr_*` dominate, prove whether they are guest
  ABI helper leaf routines. Candidate work is guest helper inlining, direct
  link cleanup, or cheaper call/return lowering.
- If direct guest calls roughly track function entries, inspect direct block
  chaining and guest function boundary overhead before GPU work.
- If indirect calls are high, inspect indirect branch lookup, hash/cache shape,
  and branch target metadata.
- If guest-to-host or extern calls are high, inspect HLE, kernel waits, XMA,
  file/device I/O, and MMIO paths.
- If resolves or resolve misses are high, inspect function lookup, code-cache
  registration, and invalidation.
- If `A64 speed profile body top` disagrees with entry-count top rows, trust
  body time for the next optimization target. Entry count means "called often";
  body time means "burned measured generated-code cycles."
- If thread samples show XMA or audio ahead of the guest CPU, use
  `-XmaFastSilence true` only as an A/B cost probe, not as a fix.
- If GPU/composer threads are not hot and the screen is merely slow, stay in
  CPU/A64 until evidence changes.

## Files To Inspect

- `src/xenia/cpu/backend/a64/`
- `src/xenia/cpu/backend/x64/`
- `src/xenia/cpu/hir/`
- `src/xenia/cpu/ppc/`
- `src/xenia/kernel/xboxkrnl/`
- `src/xenia/apu/`
- `tools/thor/thor_xenia_debug.ps1`
- `docs/research/20260519-153016-xbox360-thormax-translation-report.md`
- `docs/research/20260519-162000-a64-speed-profile-counters.md`

## Static Lane

Use Ghidra only after a runtime profile gives a concrete guest PC, helper name,
or guest wait loop. The static question should fit in one sentence, such as:

- "What does guest function `8246B408` wait on?"
- "Are `__savegprlr_29` and `__restgprlr_29` compiler ABI helpers we can
  inline or shortcut safely?"
- "Which caller loop creates this direct-call storm?"

Use `$xenia-ghidra-android-debug` for setup and keep extracted guest content
out of git.

## Acceptance

A speed patch is not a win until the notes include:

- before and after capture directories from the same scene/route;
- commit hash and APK SHA-256;
- profiler cvars and logging mode;
- final A64 counter interval;
- final A64 body-time interval when the body filter was enabled;
- thread sample summary;
- screenshot path or route status;
- whether the run crashed, hung, or changed visible progress;
- next hotpath if the current patch helped only part of the wall.
