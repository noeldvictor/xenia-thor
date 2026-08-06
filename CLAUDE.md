# CLAUDE.md — xenia-thor (Xbox 360 emulator on the AYN Thor)

## Goal
Xbox 360 games fast + playable on the AYN Thor (Snapdragon 8 Gen 2 / Adreno 740). **Blue Dragon → 30fps @ 720p
full foliage; Burnout/Gears/Lost Odyssey/Banjo → 30-60.** Ship every win as a cvar-gated, per-game
`GameProfiles` / `XeniaOptimizations` toggle (default-off until validated).

## ⚙️⚙️⚙️ STANDING DIRECTIVE (user 2026-07-31): AOT + LLVM IS THE DEFAULT — every game, from the GUI, no flags
**The standard configuration for ALL games launched from the app GUI: full AOT precompile BEFORE the game starts
(RPCS3-style full-screen compile progress in EmulatorActivity, driven by the native precompiler's log lines) +
the LLVM whole-function recompiler as the standard CPU backend.** Made safe 2026-07-31: `cpu_llvm_no_runtime_
compiles` (default TRUE, llvm_assembler.cc Assemble gate on `Processor::is_aot_runtime_phase()`) confines libLLVM
to the load window — any function first discovered during gameplay compiles on the a64 fallback, so the known
intermittent libLLVM codegen crash (killed Burnout mid-race, 2026-07-31) structurally cannot fire in gameplay.
GUI wiring: `opt_aot_precompile` (cpu_aot_maximize) + `opt_llvm_backend` (cpu_backend_llvm) + `opt_xendroid_parity`
(the 5 XenDroid-shipping GPU levers) are ALL defaultEnabled=true in XeniaOptimizations; GameProfiles adds
per-title fixes on top. a64 compile speed is real (~15k fns/1.2s device-measured, Burnout) — LLVM is the slow/
quality compile, a64 the fast/stable one; both AOT at load. VRS removed from Burnout (visible quality loss).
Game profiles ship IN the APK — updating them = rebuild + reinstall.

## 🧭🧭🧭 STANDING DIRECTIVE (user 2026-07-31): XENDROID/XENIA-EDGE IS THE COMPAT REFERENCE — USE IT TO FIX OUR SHIT
**XenDroid (github.com/rfandango/XenDroid, Kotlin shell + vendored xenia-edge, Canary lineage) runs games on the
SAME AYN Thor that our fork breaks (Lost Odyssey boots there, stalls here; user: "xendroid works so well but our
shit sucks/we crash games"). Our fork is upstream-MASTER-based (leaner, enabled the perf work: BD native renderer,
LLVM AOT, custom a64) — the cost is Canary's years of kernel/XAM compat fixes that master never had. THE RULE:
when a title misbehaves in xenia-thor but works in XenDroid, DIFF THE EDGE KERNEL AND PORT THE DIVERGENCE —
don't debug from scratch.**
- **Reference sources in-tree:** git remote `edge` (active branch `edge/edge`, NOT edge/master) — use
  `git show edge/edge:src/xenia/...`; clones at `reference/XenDroid` + `reference/xenia-edge`. Compat lives in
  `src/xenia/kernel/{xam,xboxkrnl}` + `src/xenia/vfs` (Edge's is Canary-derived and heavily diverged from ours).
- **Proven workflow (LO session 2026-07-31):** reproduce on DESKTOP xenia.exe (pull the ISO from the device if
  needed — `scratch/lost-odyssey/lo_disc1.iso` stays local, never commit) → in-tree wait/event tracing
  (`--xboxkrnl_thread_wait_trace=true --xboxkrnl_thread_wait_trace_budget=2000000` +
  `--xboxkrnl_event_trace=true`, analyzer = begin-without-end per thid) → cdb thread-stack dumps
  (`cdb -p <pid> -c ".lines; ~*k 14; qd"`) → Explore-agent diff vs Edge → port → desktop verify → APK.
- **Already ported from the LO audit (f2ea321a6):** trimmed-path NtCreateFile/NtOpenFile (probe-hits-open-misses
  class), XamGetOverlappedResult honors bWait (was infinite non-alertable park), overlapped event Reset on arm +
  25ms dispatch (was 100), fiber-thread UnlockApc null guard, scheduler-aware XIOCompletion.
  **LO STILL STALLS after these** (main thread polls a guest flag in KeDelayExecutionThread wrapper lr 827CACFC;
  workers idle; official canary A/B on desktop = the discriminator in progress). Earlier RE trail:
  docs/research/20260530-*lost-odyssey* (zombie-join hypothesis, later refined).
- **✅ THE EDGE KERNEL PORT IS LANDED AND DEVICE-VALIDATED (merged to master 2026-08-04, f3e780d2e).**
  Burnout Revenge: title + attract mode at **59.3 fps**. Blue Dragon: title at **29.6 fps**. Both on Turnip
  26.3.0, screenshotted, no faults. Bring-up took 5 defects after the merge compiled clean — see the EXECUTION LOG
  in `docs/research/20260731-edge-kernel-port-plan.md` (the two that generalize: desktop oracles cannot validate an
  Android memory/threading port, and a wholesale subsystem merge must also take the CALLER-side changes from Edge).
  Still pending: Phase 4 IRQL unification, and the deliberate omissions listed in that doc.
  **⚠️ INTERMITTENT STARTUP STALL, seen ONCE (2026-08-04), cause unknown:** GPU init completed and the presenter
  attached, then the Emulator thread futex-waited forever with the UI thread idle in epoll, no guest threads ever
  created, 0 swaps, 0.0 fps. It did NOT reproduce - the same command line (`--es hid nop` + a button sequence) ran
  to the title at 59.1 fps on the retry, so `hid nop` is NOT implicated despite the first read. Treat as a rare
  race in the emulator-thread/UI-thread handoff; if it recurs, capture the Emulator thread's stack before killing
  the app.
- **DECIDED 2026-07-31 (user: "xendroid is still more stable and reliable"): PORT EDGE'S WHOLE KERNEL LAYER**
  (xam+xboxkrnl+vfs as a unit) onto our tree, keeping our CPU/GPU. Two titles in one day reduced to ~10
  canary-lineage kernel divergences — whack-a-mole lost. Scoping report drives the multi-session migration;
  our kernel-layer customizations to re-apply afterward: trainers, guest scheduler fiber hooks, XamSwapDisc/
  disc_playlist, user_gamertag, ntreadfile_force_complete, HLE hooks, content manager. Edge stability becomes
  the floor; our CPU/GPU perf stays on top.

## 🔥🔥🔥 THE DIRECTIVE (user 2026-07-07, FURIOUS at circling): FUSE THE PASSES — DELETE EDRAM, do NOT make it cheaper
**"FUCK THE EDRAM SHADER, WE DONT WANT EDRAM." "fuse the fucking passes." "stop saying its a multisession rewrite
and just fucking do it."** The circle to STOP: trying to make the EDRAM transfer cheap (copy/blit fast-path) STILL
KEEPS EDRAM = wrong. The fix = render the WHOLE field (opaque + foliage, every draw) into the ONE held-open native
pass (native color + native depth) so there is NO pass break and NO EDRAM at all → no transfer, no resolve.
**PROVEN THIS SESSION (hard data, all on Thor Turnip):**
- Native RT + resolves deleted + safe transfers deleted = field renders CORRECT, 10fps (`bd_coloronly.png`).
- Drop ALL EDRAM transfers (any way: `skip_edram_transfers`, `drop_all_xfer` even WITH a sync barrier) = **29.7fps
  but field COLLAPSES to a right-strip** (`bd_dropall.png`). 30fps is PROVEN reachable; correctness is the gap.
- The collapse is NOT sync (barrier ruled out) — dropping EDRAM removes the OPAQUE (terrain/buildings), which the
  pitch-720 native pass does NOT render; the opaque comes in via the EDRAM transfer. **So the fix = get the opaque
  INTO the native pass (fusion), not preserve its EDRAM.**
- **THE FUSION MECHANISM EXISTS: `gpu_bd_native_whole_frame`** (vulkan_command_processor.cc ~4220) catches ALL
  vk97 field draws (foliage + opaque) into the native pass. **THE ONE BLOCKER: BD's field is a predicated 2-tile
  fan-out with WINDOW OFFSETS (left win_off=0 → 0..672, right win_off!=0 → 608..1280); in the native full-surface
  RT the tiles place wrong → STRIPE.** Offset-ignore is DEAD (3 variants). The tiling coordinate mapping is the
  whole remaining problem — resolve it with RenderDoc (`gpu_bd_native_tile_filter` 1/2 = poor-man's per-tile view;
  desktop `renderdoccmd` capture) then apply the correct per-tile viewport/scissor placement in the native RT.
- Also fuse the SHADOWS: `whole_frame` disables the aux path (`!bd_native_gate`) → shadows (render-to-texture) must
  either stay on aux (keep aux firing for non-main-fb draws) or render into their own native surfaces.
**🎯 FRAME STRUCTURE DECODED (2026-07-07, gate-log dump under whole_frame — the real map):** BD's field frame =
(a) MAIN FIELD `pitch=720 scissor 672×720` + foliage `pitch=720 scissor 8192` → the scene; (b) a BLOOM DOWNSCALE
PYRAMID `pitch=320 320×180`, `pitch=640 640×360`, `pitch=160 160×90`, `pitch=80 80×45` → post-processing; (c) a
FINAL COMPOSITE `pitch=1280 1280×720`. **whole_frame is WRONG because it dumps the bloom pyramid + composite into
the MAIN field RT → corrupts it (black on desktop, strip on Thor).** The fusion must catch ONLY the main-scene
(pitch-720) draws; the pyramid/composite are separate RTs (aux territory).
**🎯 THE COLLAPSE = DEPTH-CULLING (decisive):** basic renderer (pitch-720 only, NO whole_frame, NO aux) + drop ALL
EDRAM also collapses to the right strip (`desk_basic_drop.png`); + earlier drop-color/keep-depth = CORRECT,
drop-depth = collapse. ⇒ the field COLOR pass depth-tests against a depth buffer that comes via EDRAM; the native
pass's own depth is CLEAR+LOAD but does NOT contain BD's DEPTH-PREPASS result (the prepass is depth-only / not
vk_format==97 → the color gate MISSES it → it goes to EDRAM). Drop the EDRAM depth transfer → the native color
pass tests against a wrong/empty depth → almost all geometry culled → only the sliver where it passes survives =
the strip. **THE FIX (concrete, not vague): redirect BD's DEPTH-PREPASS draws into the native pass's DEPTH buffer
(extend the gate to catch the depth-only prepass, not just the vk97 color draws) so the field's depth test is
fully native → drop the EDRAM depth transfer → no cull → full field at 30fps.** NOT window-offset (all win_off=0),
NOT tile-placement, NOT MSAA — it's the DEPTH-PREPASS source. Next: identify the prepass draw signature (depth
format, no color, 672×720) at the gate, redirect it to the native depth, verify the field survives EDRAM-drop.
New cvars this session (allowlisted, gated off): gpu_bd_native_drop_all_xfer, gpu_bd_native_drop_all_color_xfer.
Desktop build UNBLOCKED (x64-backend crash root-caused, NOT the HLE — RenderDoc injection is Defender-blocked so
use the in-code gate/viewport LOGS as the poor-man's RenderDoc; they gave this whole frame map).

## 🟢🟢🟢 TURNIP IS MANDATORY FOR THE AYN THOR (user 2026-07-06, verbatim: "TURNIP IS MANDATORY FOR AYN THOR ADRENO STUFF")
**The Thor's DEFAULT/system Vulkan driver is the QUALCOMM PROPRIETARY Adreno driver (`vulkan.adreno.so`,
`vendorID=0x5143`, `driverID=8` = VK_DRIVER_ID_QUALCOMM_PROPRIETARY, device="Adreno (TM) 740") — confirmed
on-device 2026-07-06 via `GetVulkanDevice()->properties()`. This is the WRONG driver: BD/Adreno work MUST run on
TURNIP (Mesa's open-source Adreno driver, `driverID=VK_DRIVER_ID_MESA_TURNIP`). Why it matters:**
1. **The Qualcomm proprietary driver CRASHES on the native renderer** (null-deref inside `vulkan.adreno.so`; it
   was misdiagnosed as a CPU/JIT crash until `/proc/PID/maps` located the host pc in the driver .so). Turnip is a
   different driver with different behavior — the whole TBDR optimization strategy (minimize render passes, GMEM
   residency, LRZ, load/store_op) is premised on TURNIP, and RE2-on-GameNative (the 30fps benchmark) runs on
   Turnip (GameNative ships a custom Turnip driver).
2. **`IsAdreno()` fork point:** `BdNativeRenderer::IsAdreno()` (via device properties) branches Adreno-strict /
   super-optimized paths. Use it + check `driverID` to require/prefer Turnip. Desktop Vulkan (NVIDIA/AMD, lenient)
   hides driver-strictness bugs — validate on Turnip.
3. **To get Turnip on the Thor:** load a custom Turnip `libvulkan_freedreno.so` (as GameNative/emulators do), not
   the system driver. Confirm at runtime via the logged `driverID` (must be MESA_TURNIP, not 8).
4. **VULKAN VALIDATION is now bundled** (`app/src/main/jniLibs/arm64-v8a/libVkLayer_khronos_validation.so`,
   allowlisted cvar `vulkan_validation`) — it found the on-device crash as `VUID-VkRenderPassBeginInfo-
   clearValueCount-00902` (native CLEAR pass begun with clearValueCount=0 → driver reads null pClearValues →
   null-deref). ALWAYS run `--vulkan_validation=true` on-device when a driver crash is suspected; strict drivers
   crash where desktop tolerates.

**🎯 GOAL (user 2026-07-05, THE mandate): BD→30fps via a FULL D3D9→VULKAN HLE — the DXVK-for-360 native renderer,
NOT levers (all dead).** RE2 (heavier) hits 30fps on this Thor via DXVK ⇒ HW is fine, xenia's 95-pass EDRAM LLE
emulation is the wall. BUILD a separate native Vulkan renderer (seam 0x82489F40 → own full-surface RT, few
passes, native vertex-input/shaders/ROP-blend, early-Z reject) that BYPASSES the PM4/EDRAM/tiling back-end.
Develop STRUCTURE on desktop `--gpu=vulkan`; **SUPER-OPTIMIZE for the Thor's Turnip/Adreno TBDR** (Thor Vulkan ≠
desktop Vulkan — minimize passes/GMEM flushes, GMEM-resident RT, Turnip extensions). Also AOT-LLVM (RexGlue-style
PPC→native) for CPU/thermal. Blueprint: `docs/research/20260705-native-vulkan-renderer-plan.md`. Be creative,
novel, research (arxiv/DXVK/Cemu). Convert the WHOLE pipeline at once — do NOT do one lever at a time (all dead).

## 🚨🚨🚨 TOP PRIORITY (user 2026-07-05, verbatim): BUILD THE FULL D3D9→VULKAN HLE — on DESKTOP VULKAN
**"DO FULL HLE for D3D9. use vulkan on desktop." STOP planning, STOP levers (all dead), BUILD the native
renderer NOW.** Two hard corrections to obey:
1. **Develop on DESKTOP with the VULKAN backend** (`xenia.exe --gpu=vulkan`), NOT D3D12 — for STRUCTURE +
   CORRECTNESS (the HLE renderer's VkImage/VkRenderPass/VkPipeline/SPIR-V ships to Turnip, no rewrite; iterate in
   seconds w/ RenderDoc). ⚠️ **BUT THOR/TURNIP VULKAN ≠ DESKTOP VULKAN (user 2026-07-05): the Thor STILL NEEDS
   SUPER-OPTIMIZATION for its Vulkan.** Desktop Vulkan runs on an IMMEDIATE-MODE GPU (no tiling cost, no GMEM); the
   Adreno 740 is TBDR — the real perf comes from Turnip/Adreno-SPECIFIC tuning that desktop CANNOT show: minimize
   render passes (each = a GMEM tile store/flush = the whole 95-pass wall), keep the RT GMEM-RESIDENT across draws
   (one held pass, subpasses via dynamic_rendering_local_read/ROAA), NEON/Adreno pipeline config, load/store_op
   tuning, LRZ-friendly depth. So: STRUCTURE on desktop Vulkan, PERF-TUNE on the Thor (the TBDR/Turnip
   optimization is where the 30fps actually comes from — desktop just proves the render is correct + few-pass).
2. **FULL HLE, whole frame at once** — a SEPARATE native Vulkan renderer (own full-surface RT, one held-open
   render pass, native VkBuffer vertex-input, Xenos→SPIR-V, hardware ROP blend, depth-prepass + forced early-Z),
   capturing BD's D3D9 draws at seam **0x82489F40**, BYPASSING xenia's PM4/EDRAM/95-pass LLE back-end entirely.
   The partial decoupled-native-HLE was PERF-FLAT because it kept that back-end. Build order + amplification math:
   **`docs/research/20260705-native-vulkan-renderer-plan.md`**. Why it works: RE2 (heavier) hits 30fps on this
   Thor via DXVK = the HW is fine, the 95-pass emulation is the wall. ALL levers/reject/FSI/ROAA DEAD (ledger).

## ⭐⭐⭐ THE DIRECTION (user 2026-07-04): HYBRID HLE — be DXVK/Cemu for the 360
**BD is slow because xenia LLE-emulates the 360 GPU (PM4 command stream + register file + EDRAM + predicated
tiling + SSBO-vertex-fetch). PROOF it's the emulation, not the chip: the user runs Resident Evil 2 Remake (far
heavier than BD) on the SAME Thor via GameNative/DXVK — an HLE API *translator*.** So build the DXVK equivalent
INSIDE xenia (the **Cemu model**: general emulator + HLE graphics + per-game graphic packs):
- **KEEP** xenia's LLE PM4 path = compatible default (runs any title correctly).
- **ADD** an opt-in **HLE GPU path**: translate BD's D3D9 draw/state/resource → native Vulkan (REUSE xenia's
  shader/texture/pipeline BACKEND; replace only the PM4/EDRAM FRONT-END).
- **TOGGLE per-game** via `GameProfiles` (= graphic packs). BD gets HLE; untested titles stay LLE-safe.
- **Boundary problem** (360 D3D9 is static-linked, no runtime hook): solved by **load-time XEX signature-HLE** —
  scan the XEX at load, byte-match the XDK D3D9 fns, install trampolines (mechanism PROVEN: the bin-once ran a
  host body in place of a guest D3D fn). The RPCS3 model applied to graphics.
- **Full architecture + staged build order: `docs/research/hybrid-hle-architecture.md`.**

**⛔ STOP proposing incremental GPU levers — they are ALL DEAD** (they patch the emulator instead of replacing
it: native-input flat, bindless regressed, cap=1/interlock/EDRAM-fusion/driver-internals dead). The fix is the
HLE front-end, not another cvar. `check` the experiment DB first (below).

## 📉 PC HLE EDRAM-REMOVAL PROGRESSION (2026-07-07, RenderDoc pass count, all CORRECT on desktop, all gated)
**69 (no HLE) -> 61 (native renderer + drop_resolves) -> 54 (+aux native surfaces + drop native-served transfers)
-> 46 (+drop_all_color_xfer = ALL color EDRAM transfers dropped, keep depth) — field FULLY CORRECT at each step
(desk_hle_colordrop.png). = 23 EDRAM passes deleted on PC, verified.** Config for the 46-pass correct HLE:
`--gpu_bd_native_renderer --gpu_bd_native_aux_rt --gpu_bd_native_aux_fmt37=false --gpu_bd_native_drop_resolves
--gpu_bd_native_drop_transfers --gpu_bd_native_drop_all_color_xfer --gpu_bd_native_skip_resolves
--gpu_bd_native_stretch_width=672`. Remaining ~46 = the inherent scene passes (~15) + the DEPTH-conversion passes
(mixed-resolution 720<->400, LOAD-BEARING — dropping them collapses on Thor; needs native depth handling) + the
format-37 opaque passes (fmt37 off on desktop; on the Thor fmt37 ON covers them). NEXT PC step to push below 46:
native mixed-res depth (the depth transfers are the remaining big EDRAM chunk that can't just be dropped).

## 🚨🚨🚨 DECISIVE (2026-07-08, on clean Thor): BD FIELD IS **CPU-BOUND**, NOT GPU-BOUND — the whole GPU/EDRAM era was the wrong processor
**MEASURED on the clean Thor (busycheck.ps1, Turnip, field rendering): GPU busy% = 10-48% (avg ~28%) at the
MINIMUM clock 401MHz (Adreno 740 max ~680MHz). The GPU is IDLE 60-85% of every frame + downclocked = it has
nothing to do = the CPU can't feed it fast enough. THIS IS CPU-BOUND.** Confirmed by every GPU lever being inert
on-device THIS session: drop ALL EDRAM ops (resolves+all color transfers) = 9.9fps (no change); force_no_color_write
(skip ALL pixel shading) = ~15fps (small, cross-run-confounded); gpu_bd_skip_foliage_shadows (skip foliage
shadow-casters) = no change; near-EMPTY scene (fence+1 bush) = SAME 9.9fps as the dense field. A GPU-bound title
would show ~99% busy + boosted clock + scene-dependent fps. BD shows the opposite. => **The EDRAM/HLE/pixel/shadow
work does NOT and CANNOT get BD to 30fps — the bottleneck is the CPU (guest-code emulation + the CP-thread GPU
command translation, ~190 HLE redirects/frame). The path to 30fps is CPU: the LLVM-JIT backend, residency
write-back (#1 CPU lever per memory), XMA idle-skip, global-lock lock-free, cross-block optimizer — the committed
CPU direction. The HLE is CORRECT + shippable (renders the field right, EDRAM ops deletable) but is NOT the fps
lever.** Temp hit 63C at min GPU clock = the HEAT is the CPU cores working = CPU is the hot busy component. ⚠️ Cross-
run fps is scene-confounded (memory rule); the GPU-busy%/clock reading is the RELIABLE signal and it is unambiguous.

## ✅✅ PC HLE DEV UNBLOCKED (2026-07-07): aux HLE now RUNS + renders CORRECT on DESKTOP (desk_aux97.png)
**The desktop x64-backend crash that blocked PC-primary HLE dev ALL SESSION was the vk-FORMAT-37 aux surface
handling, NOT the HLE. New cvar `gpu_bd_native_aux_fmt37` (default true; set FALSE = cover vk-97 only). With
`--gpu_bd_native_aux_rt=true --gpu_bd_native_aux_fmt37=false --gpu_bd_native_drop_resolves=true` the aux HLE
(resource-keyed native surfaces + Brick-B + resolves-deleted) renders BD's field FULLY CORRECT on desktop x64
(no crash, no LLVM needed). => PC-FIRST HLE DEVELOPMENT IS NOW POSSIBLE: iterate the native-RT coverage + drops on
desktop, verify correctness (screenshot) + pass count (RenderDoc rda9), NO Thor needed. On the Thor, keep fmt37 on
(the opaque needs covering). NEXT PC step: with the desktop HLE now running, extend native coverage RT-by-RT +
drop each covered RT's transfer, watching the RenderDoc pass count fall + the screenshot stay correct — pure PC.**

## 🧠🧠🧠 THE FUSION-COLLAPSE DIAGNOSIS (2026-07-07, rda12 — the corrected root cause; supersedes "transfers are the wall")
**61 passes but only 13 UNIQUE render targets = ~4.7 passes PER RT (ResourceId::465 alone = 10 passes). The
fragmentation is the SAME RT re-begun many times because BD INTERLEAVES its RTs (draw 465, switch to shadow, back
to 465...) and Vulkan REQUIRES ending a render pass to switch framebuffers. whole_frame did NOT collapse it
(61->60): redirecting draws to a native RT can't hold one pass open across draws that target different RTs.
Dropping resolves+depth-transfers barely moved it (69->61) => the transfers/resolves are a SMALL slice; the bulk is
inherent RT-switching.**
**WHY 30fps on desktop/DXVK but 10fps on Thor with the SAME 61 passes: on immediate-mode GPUs a pass-begin is ~free;
on Turnip TBDR every pass-begin FLUSHES GMEM (store tile + reload). DXVK-on-Turnip (RE2's path) keeps RTs
GMEM-RESIDENT across re-begins so switching back doesn't re-flush. xenia's EDRAM model does NOT — it tile-fragments
the RTs (320x8192 etc.) and reloads each re-begin. THAT is the wall. So "EDRAM" IS the cause — but the precise fix
is NOT "drop the transfers" (small); it's REPLACE the EDRAM RT model with native resource-keyed GMEM-RESIDENT RTs
(= the goal verbatim: resource-keyed native RTs, DXVK-for-360).**
**⚠️ HARD PC-vs-THOR SPLIT (critical for "EDRAM on PC first"): the pass COUNT is PC-measurable but is largely
INHERENT (RT-interleaving) — there is NO big pass-count win available on PC without unsafe draw-reordering. The
actual 30fps win is GMEM RESIDENCY of the re-begins, a TBDR concept that is INVISIBLE on desktop (immediate-mode
doesn't flush GMEM) — so it structurally CANNOT be measured on PC. PC-first can build+verify the native RT
STRUCTURE + correctness + dedup the EDRAM ping-pong RTs (two 1280x2048, two 320x8192); the residency PERF must be
Thor-measured. Do NOT chase a PC pass-count drop as the goal — it's mostly inherent; the goal is native
GMEM-resident RTs, perf-validated on Turnip.** Analyzer: tmp/rda12.py (passes per unique output resource).

## 🎯🎯🎯 PC-MEASURED PASS BUDGET (2026-07-07, RenderDoc rda9/10/11 on desktop — the REMOVE-EDRAM-ON-PC metric)
**BD's field frame = 61 render passes (with resolves + depth-downscale dropped; 69 without). Breakdown by draws/pass:
45 = SINGLE-DRAW passes, 3 = 2-4 draws, 3 = 5-20, 9 = 21+ (the real scene), 1 clear-only. The 45 single-draw
passes have NO input texture — they are GEOMETRY draws (1 draw each) into ~45 SEPARATE EDRAM render targets, many
tile-strip-addressed (320x8192, 80x8192, 160x8192, 360x1824, 640x4096, 1280x2048). Each pass = a GMEM flush on
Turnip/Adreno TBDR = the wall. DXVK renders a frame in ~10-15 passes; BD's 61 (esp. the 45 single-draw) is the
EDRAM fragmentation.** ⇒ **"REMOVE EDRAM ON PC" IS NOW A CONCRETE, PC-VERIFIABLE METRIC: drive the RenderDoc pass
count from 61 toward ~15, verifying correctness (desktop screenshot) + count (RenderDoc) — NO Thor needed (pass
COUNT is the emulation structure, identical on desktop + Thor; only per-pass cost is TBDR-specific).** The fix =
native FULL-SURFACE resource-keyed RTs that FUSE BD's ~45 fragmented EDRAM RTs into few native passes (the DXVK
model). The 40 depth transfers are NOT the pass-count cost (dropping them was only 69->61 = -8); the cost is the
45 single-draw fragmented-RT passes. NEXT PC build: measure whether the aux native-surface path reduces the count
(capture aux+cpu_backend_llvm, count passes), then extend native coverage to collapse the 45 -> few. Analyzers:
tmp/rda9.py (count), rda10.py (draws/pass buckets), rda11.py (single-draw pass in->out). Capture the drop-config
frame via tmp/rdselfcap.ps1 -N 4600. ALL desktop, no device — matches user "EDRAM on PC FIRST before the Thor."

## 🔥 DECISIVE REFRAME (2026-07-05 late): RE2-on-Thor proves it's EMULATION-inefficiency, NOT a hardware/foliage limit
**User's clinching point: Resident Evil 2 Remake (VASTLY heavier than a 2007 360 game) runs 30fps on THIS Thor via
GameNative (Box64/FEX + DXVK D3D→Vulkan).** So the Adreno 740 does 30fps of far heavier rendering — the hardware is
NOT the wall; **xenia's LLE emulation is.** Any "BD foliage is intrinsically too heavy / 30fps is a physical limit"
conclusion is RETRACTED.
- **THE MEASURED WALL (per-pass GPU trace, matched heavy Shu field):** BD's field = **n=95 render passes/frame** =
  16 geometry + **79 pure EDRAM-emulation overhead (35 xfer + 23 resolve + 18 composite)**. `gpu_pass_us`≈4ms (the
  actual draws) but `gpu_frame_us`≈123ms — the ~119ms is deferred TBDR tile-store/render across the 95 passes +
  barriers. DXVK renders RE2 in a HANDFUL of native passes; that's the whole gap.
- **DECOMPOSITION (clean isolations, each moved a number):** ~4ms draws + ~12ms EDRAM transfers
  (`skip_edram_transfers`, −12ms) + ~40ms ROP/blend (`gpu_force_no_color_write`, −40ms) + ~60ms fragment ALU/
  depth/tile-store. `gpu_vrs_foliage_rate=4 + gpu_foliage_decimate_pct=45 + msaa1` → 46ms (~22fps) = fragment-bound,
  but that only proves the fragment work is done INEFFICIENTLY (RE2's heavier fragment work fits 33ms).
- **EVERYTHING incremental is DEAD w/ data (exp_ledger, do NOT re-run):** bin-once/force-1-tile (striped/confounded),
  decoupled-native-HLE (correct render but PERF-FLAT — it swaps the front-end, keeps the 95-pass EDRAM back-end),
  in-pass transfers, ROAA (off), downscale, MSAA, hw-vertex-fetch, AND **every early-Z/LRZ reject** (force_depth,
  spike, feedback, spike+feedback, forced per-pixel EarlyFragmentTests `gpu_foliage_force_early_z` [built, gate-
  fixed, committed]) — cutout-`discard` defeats coarse LRZ + LOAD_OP_LOAD kills LRZ every pass. Math: even PERFECT
  reject caps ~10-16fps (visible foliage + transfers floor). **Reject/levers are the wrong axis.**
- **⇒ THE ANSWER = FULL D3D9→Vulkan HLE (DXVK-for-360), whole frame at once** (user mandate, verbatim: "REIMAGINE
  the gfx pipeline d3d9→vulkan", "convert the whole pipeline at Once", "WE MUST HAVE HLE FOR D3D9"): capture BD's
  D3D9 draws at the RE'd seam **0x82489F40** (SubmitRectDraw, one call/draw, PRE per-tile fan-out; state in dev-ctx
  **dev+0x2800..**) → render NATIVELY in a FEW Vulkan passes (native `VkBuffer` vertex-input NOT SSBO vfetch; Xenos→
  SPIR-V; native `VkPipeline` ROP-blend/depth; ONE persistent full-surface host RT; no per-tile fan-out, no EDRAM
  xfer/resolve/composite passes) → present. This BYPASSES the entire PM4/EDRAM/95-pass LLE stack = efficient like
  DXVK. The decoupled native-HLE was PARTIAL (kept xenia's back-end) = why flat; the FULL HLE = a SEPARATE native
  Vulkan renderer. Multi-session architectural build = THE reimagination. Detail: memory `[[gpu-d3d9-hle-architecture-pivot]]`.

## ⭐ NATIVE HLE — CURRENT BUILD STATE (2026-07-05): RE DONE + enabler committed, IMPLEMENTING on desktop
- **SEAM (found + verified on the field via screenshot + write-watch):** universal per-draw recorder =
  **0x824895C8** (writes each draw's PM4 into the IB); high-level D3D9 draw = **0x82186BA0** (allocates that draw's
  vertex+index buffers, submits one draw). Chain `0x82186BA0 → 0x82489F40 → 0x824895C8`. General
  DrawIndexedPrimitive = 0x82477D70. (The RE is DONE — this was the 3-session-stuck hunt.)
- **ENABLER (COMMITTED, x64_emitter.cc):** desktop HLE intercepts NEVER FIRED on x64 (kExtern dispatch bug — a
  guest `bl` bypassed SetupExtern handlers). FIXED → recorder intercept fires on desktop (0→20000+). Makes the
  RE-on-PC / implement-on-PC loop actually work. (All earlier desktop `count=0` intercept results were FALSE
  NEGATIVES from this bug — e.g. the "field=composite, BeginTiling never fires" read was wrong; the field DOES
  tile via replay 0x82487fe0.)
- **THREADING (solved):** recorder runs on the GUEST CPU thread; xenia's `IssueDraw` + `register_file_` = CP
  WORKER thread. CANNOT call IssueDraw at the recorder. Handoff = **`CommandProcessor::CallInThread`**
  (command_processor.cc:719; `pending_fns_` push is unlocked → per-draw path needs a lock/lock-free queue).
- **NATIVE-DRAW PLAN (2 halves):** (A) guest capture writes a **minimal synthetic PM4** (1 clean DRAW_INDX + only
  the changed reg/const, NO per-tile fan-out) → enqueue via CallInThread → CP thread runs it through xenia's
  EXISTING PM4→register_file_→IssueDraw = reuse ALL decode, drop the 196KB re-emit + the 2× replay. (B)
  **EDRAM-DECOUPLED full-surface RT = THE WALL:** render_target_cache.cc:642 welds RT width to
  RB_SURFACE_INFO.surface_pitch (BD's 360-strips); overriding it black-screens (:643-646) — native draws need
  their OWN full-surface host RT.
- **DEAD (don't revisit):** force-1-tile flatten crashes BOTH platforms (tile count coupled to the per-tile
  resolve). Full roadmap: memory/bd-d3d-hle-re-state.md (NATIVE-HLE ROADMAP) + `exp_ledger.py check native`.

## 🖥️ DEV ON PC WITH THE **VULKAN** BACKEND, SHIP TO THOR (user 2026-07-05: "use vulkan on desktop")
**Build + iterate the native HLE renderer on DESKTOP xenia running the VULKAN backend (`xenia.exe --gpu=vulkan`),
NOT D3D12.** Rationale (user directive): the Thor is Vulkan/Turnip — developing on the SAME Vulkan backend means
the HLE renderer (VkImage/VkRenderPass/VkPipeline/SPIR-V) is the SAME code that ships to the device, no rewrite;
D3D12 would be throwaway. Desktop iterates in SECONDS (real debugger, RenderDoc capture) vs the Thor's 150s-nav/
thermal/build-install. Same guest CPU + CommandProcessor/register-file; validate the native render on desktop
Vulkan (RenderDoc = inspect the pass count / pipelines), then build the SAME code for Turnip + measure fps.
Skill: **xenia-bd-pc-reverse-engineer** (+ the native-renderer build). Build: `tools\build\bin\premake5.exe
--file=premake5.lua vs2022` → `MSBuild build\xenia-app.vcxproj /p:Configuration="Release Windows" /p:Platform=x64`
(MSBuild at `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe`); run
`build\bin\...\xenia.exe --gpu=vulkan "<Blue Dragon.iso>"`. **⚡ Incremental rebuilds ~1min** (edit one .cc → MSBuild
relinks) — iterate the native renderer HERE, not in blind 13-min Thor cycles.

### 🛠️ DESKTOP BUILD GOTCHAS (2026-07-07 — cost HOURS this session; skill `xenia-desktop-build`)
1. **✅ STALE AS OF 2026-07-30: Defender no longer quarantines xenia.exe** (Tamper Protection is OFF; a fresh Release link survived, PE-valid, and ran 12s with --guest_scheduler smoke-clean). The historical gotcha, kept for if it regresses: **🛑 Windows Defender QUARANTINES the built xenia.exe** (emulator = PUA false-positive). Link says
   `-> xenia.exe`, then it's GONE within ~1s + zeroed PE. **Fix requires the GUI, NOT PowerShell** — `Add-MpPreference`/
   `Set-MpPreference -DisableRealtimeMonitoring` SILENTLY FAIL when **Tamper Protection** is ON (check
   `(Get-MpComputerStatus).IsTamperProtected`). USER must: Windows Security → Virus & threat protection → Manage
   settings → **Tamper Protection OFF**, then **Real-time protection OFF** (or add a **Folder exclusion** for
   `...\xenia-thor\build`). Verify `(Get-MpComputerStatus).RealTimeProtectionEnabled -eq $false` before building.
   NOTE: an exe quarantined DURING a build stays quarantined after disabling Defender — REBUILD to get a fresh one.
2. **🛑 The 32-bit linker WEDGES on `xenia.exe`** ("failed to do memory mapped file I/O on xenia-kernel.lib") — CPU
   flatlines, exe stays locked for 20+ min, output corrupt. **Fix: force the 64-bit linker: add
   `/p:PreferredToolArchitecture=x64` to the MSBuild command.** Always.
3. **MSBuild SKIPS the relink if you manually `rm` the exe** (it tracks build state, not output existence) — it
   prints `-> xenia.exe` without linking → no exe. To force a real link after a manual delete: **touch a source file**
   (`echo // >> src/.../some.cc`) or `/t:Rebuild`.
4. **Corrupt-PDB `LNK4020` treated as fatal error**: the `build\xenia-app.vcxproj` sets
   `<TreatLinkerWarningAsErrors>true</TreatLinkerWarningAsErrors>` — patch those to `false` (build artifact,
   regenerated by premake) so the (harmless, debug-info-only) corruption doesn't fail the link.
5. **Kill zombie build procs first**: `Get-Process link,MSBuild,cl,mspdbsrv | Stop-Process -Force` — accumulated
   workers hold file locks and cause the pdb corruption + mmap wedge.
6. **ISO path has SPACES** ("New project 8") — in PS `Start-Process -ArgumentList`, pass the ISO as its OWN quoted
   element or the path truncates at the space ("Unable to mount STFS container"). Verify PE valid after build:
   read bytes at e_lfanew == `PE\0\0`. BD desktop ISO: `scratch/blue-dragon/bd_disc1.iso` (reaches the field via the
   `run_field.ps1` HID-nop nav; desktop logs to `--log_file` — reliable, no logcat rotation).
**🔬🔬 RenderDoc UNBLOCKED + WORKING ON DESKTOP (2026-07-07 — injection was Defender-blocked, now bypassed):**
1. **SELF-LOAD (not injection):** renderdoccmd injection fails ("Failed to launch process") under Defender. FIX:
   xenia now LoadLibrary's renderdoc.dll itself at startup (vulkan_provider.cc, env `XENIA_RENDERDOC=<path to
   renderdoc.dll>`, BEFORE vkCreateInstance) → RenderDoc hooks Vulkan → in-app `--gpu_bd_renderdoc_capture_frame=N`
   TriggerCapture works with NO injection. Captures land in `%LOCALAPPDATA%\Temp\RenderDoc\*.rdc`.
2. **CAPTURE IN THE FIELD:** N is a SWAP number; the field is ~swap 4400+ (basic renderer ~30fps, field at ~150s).
   N too low = menu (tiny .rdc); field frame = big (~78MB). Runner: `tmp/rdselfcap.ps1 -N 4600`.
3. **ANALYZE HEADLESS:** `qrenderdoc.exe --py <script>` (NOT `--python <script.rdc>` — a 2nd .rdc arg makes the GUI
   open+hang). **The script AND its output path MUST be a real NO-SPACE, NON-JUNCTION path** (qrenderdoc silently
   fails on "New project 8" spaces AND on the `C:\xt` junction). Copy the script to `~/.claude/jobs/.../tmp/` and
   hardcode the .rdc path inside it. Working analyzer: `tmp/rda2.py` (post-VS NDC per draw). Replay is SLOW
   (~5-8min: OpenCapture + SetFrameEvent-per-draw each re-replays). Poll the out file for "=== DONE ===".
**🎯 FIELD NDC DATA (correct case, transfers on — `scratch/thor-debug/bd_field_ndc_correct.txt`):** the main field
draws render into `rt=1280x720` via `viewport x=0 w=672` (NOT 8192 — the device viewport-LOG's 8192 was misleading);
NDC[-1,1] → pixels[0,672], then stretch_width=672 → full screen. Field draws span a WIDE NDC x [-14..+6]: the ones
with |ndc x|>1 are frustum-clipped (BD submits geometry beyond the view). So the CORRECT field = [0,672]. The
collapse (EDRAM dropped → right strip) does NOT change NDC/viewport (geometry is identical) ⇒ it's what RENDERS
(depth-cull/content), not placement.
**RenderDoc facts (frame4601, correct case, `rda3/rda4.py`): the field = TWO draw groups — `vpw=1280` (full-scene,
FIRST) then `vpw=672` ([0,672] field, SECOND) — BOTH `Ztest=True Zwrite=True Zop=LessEqual` (standard-Z) AND BOTH
render into the SAME color=ResourceId::471 + depth=ResourceId::473. So they are ONE BD surface rendered
scene-then-field, and it renders CORRECTLY with transfers on ⇒ sharing the surface/depth is INTENDED, NOT the bug
(the earlier "conflation/occlusion" guess is RETRACTED). `depth_clear` 0.0→1.0 did NOT fix the collapse.**
**⚠️ DISCIPLINE NOTE (2026-07-07): RenderDoc is unblocked + working, but I made TWO wrong root-cause claims this
session from over-fast interpretation (both retracted): (a) "vpw1280 occludes vpw672" — NO, they share surface
471/473 intentionally; (b) "471 is EDRAM, native only does foliage" — NO. VERIFY before asserting.**
**🔧 DEPTH-COPY ACTION (2026-07-07, LANDED): extended `gpu_bd_native_copy_transfers` to DEPTH (added CmdVkCopyImage
to the deferred cmd buffer; same-tile-layout depth transfer → vkCmdCopyImage instead of the per-pixel EDRAM shader).
Thor result (`bd_depthcopy.png`): field renders CORRECT, but STILL 9.8fps, pass_begins NOT dropped (12456 vs 12928
baseline). ⇒ the heavy ~97ms depth transfers are NOT same-tile-layout — they are TILE-REINTERPRETING (moving depth
between BD's different EDRAM tile layouts = genuine format conversion), so a cheap 1:1 copy can't replace them.
CONFIRMED: the depth transfers are inherent EDRAM emulation; they exist because the field is PARTIALLY native (471
catches pitch-720) and partially EDRAM, and the transfers BRIDGE the two tile layouts. **THE ONLY elimination = make
the WHOLE field native (every draw → a native linear RT with native depth, NO EDRAM tile layouts) so there is
nothing to reinterpret → no depth transfers. That is full resource-keyed native coverage of ALL field surfaces
(471 + the non-720 opaque/composite RTs), each a native linear surface — the goal's "resource-keyed native RTs,
EDRAM deleted." Partial coverage always leaves the bridging transfers.** whole_frame failed (forced 1280 field into
720 foliage RT + MSAA). Next real build: per-BD-surface native RTs keyed by resolve-dest covering the full field.
**whole_frame RULED OUT for good (2026-07-07): whole_frame + force_samples1 + gpu_force_max_msaa_samples=1 (all
pipelines 1x → match native 1x pass) = STILL BLACK on desktop. The MSAA was not the (only) blocker; whole_frame is
fundamentally wrong because it crams MULTIPLE BD surfaces (main 471 + bloom pyramid 320/160/80 + composite) into
ONE native RT → corruption. One-RT-for-everything is the wrong model.**
**⚠️ THE REAL BLOCKER, stated plainly: 471/473 IS the native RT, but xenia's render_target_cache STILL models it
with EDRAM TILE LAYOUTS (base_tiles/pitch_tiles), so the tile-reinterpreting depth transfers exist to bridge 471's
tile layout ↔ other RTs' tile layouts. Eliminating them requires 471 (+ every field surface) to be a LINEAR
full-surface RT with NO tile layout — but rtc.cc:642 welds RT width to surface_pitch and overriding it black-screens
(known dead). So full EDRAM deletion = a deep rewrite of the RT cache's tile model into per-surface linear native
RTs. This is genuinely the multi-session DXVK-style rewrite the goal names; every one-turn shortcut (drop-all,
barrier, color-only, depth-copy, whole_frame, force_samples1, force_max_msaa, linear-RT-override) is RULED OUT with
data. Do NOT re-try any of them.**
**⛔ TILING-HLE PATH IS DEAD — verified in the experiment ledger (do NOT chase FUN_82487cc8 / bin-once): the
reachable Shu-village field is IMMEDIATE-MODE, NOT deferred-tiled. BeginTiling 0x8248A188, token-interp 0x82487CC0,
tiling-replay — ALL tested, NONE FIRE at the field (planted:1 / TRSTEP:0). The bin-once mechanism works pixel-perfect
but never fires here. So the tile-reinterpreting transfers are NOT from the field's internal tiling — they're from
BD ALIASING different-PITCH EDRAM surfaces (main field 1280, bloom pyramid 320/160/80, composite): each surface's
pitch → its own EDRAM tile layout, and the ownership transfers bridge between the aliased surfaces.**
**🎯🎯 RENDERDOC-CONFIRMED (2026-07-07, rda8.py — desktop, no device): BD samples depth as a texture in ZERO of
1313 draws. So the depth transfers are NOT for texture-sampling — Brick-B-for-depth is RULED OUT. There are 7
depth buffers at DIFFERENT RESOLUTIONS (473=1280x720, 461=720x1824, 501=360x1824, 402=1040x2528, 422=520x1264,
2640=320x8192, 2657=160x4096) and BD DEPTH-TESTS against them. ⇒ BD renders the field at MIXED RESOLUTIONS and the
40 transfers convert the shared scene depth BETWEEN those resolutions (720<->400 etc.) so geometry drawn at one res
depth-tests coherently against geometry drawn at another. This is mixed-resolution depth-attachment usage, NOT
sampling, NOT MSAA, NOT tiling. **⇒ THE FIX = handle BD's mixed-resolution field natively: render each resolution
into its own native depth image AND do the cross-resolution depth conversion NATIVELY (a depth downsample/upsample
shader between the native depth images), replacing the EDRAM conversion transfers. This is the native mixed-res
depth pipeline — a real shader build, Thor-verified (depth doesn't validate on desktop).** The resolution
heterogeneity is the same class of wall as the MSAA heterogeneity (BD renders parts at 720, parts at 400).**
**🎯 REFINED (2026-07-07): the depth transfers are DEPTH DOWNSAMPLES (720->400), NOT MSAA. Applied the RT-cache
MSAA clamp in the field gate (vulkan_command_processor.cc ~4229, draw_util::ClampForcedMsaaSamples — no-op unless
gpu_force_max_msaa_samples set; matches the native RT sample count to EDRAM so force-1x doesn't stripe). Result:
force_max_msaa=1 now unifies MSAA (transfers go msaa 1<->2 -> all msaa 0) BUT the 40 transfers REMAIN, now as PITCH
conversions `base=810 pitchT=9 (720px) <-> pitchT=5 (~400px)` + `base=0 pitchT 13<->7<->16`. So BD renders depth at
TWO RESOLUTIONS (full 720 + downscaled ~400) regardless of MSAA — the transfers DOWNSAMPLE the full depth to the
half-res depth that POST-PROCESSING samples (DOF/soft-particles/fog). force-1x is NOT the fix (clamp fix still
partial-stripes the sky, desk_msaa1fix.png; transfers unchanged). **⇒ THE FINAL TARGET = native depth DOWNSAMPLE:
produce the ~400-wide depth view via a native downsample (compute/blit) of the field's native depth 473, extend
Brick-B to serve DEPTH samples so post reads the native half-res depth, then the 720->400 EDRAM downsample
transfers are redundant -> drop. That's the last EDRAM. NOT MSAA (ruled out), NOT drop (Thor-collapses).**
**🔬 DEPTH-CONVERSION DROP TESTED (2026-07-07, gpu_bd_native_drop_depth_downscale — drops per-transfer depth xfers
where src/dst pitch OR msaa differ, keeps identity): DESKTOP = field CORRECT (desk_ddrop2.png, full field). THOR =
COLLAPSES to sky-blue + right strip (bd_ddrop.png), 26.8fps but CONFOUNDED (less rendered because culled). ⇒ the
depth-conversion transfers are LOAD-BEARING ON THE THOR (TBDR depth needs them) even though desktop (immediate-mode)
tolerates dropping them. **KEY LESSON: desktop correctness does NOT validate depth/EDRAM changes — the Thor's TBDR
depth differs; depth changes MUST be Thor-verified.** So they can't be DROPPED; they must be done NATIVELY. Also
confirmed: the 23 dominant ones are MSAA-conversions (base=810 pitchT=9 msaa 1<->2 = the field's 1x-opaque <-> 2x-
foliage depth), the rest pitch-downscales (720->400 for post depth samples). **⇒ native-first fix = produce these
depth VIEWS natively: a native MSAA-resolve (2x->1x) + native depth-downsample (720->400) from the field's native
depth 473, redirect the consumer to the native view, THEN drop the EDRAM conversion transfer. That's the concrete
build — native depth conversion shaders, not a drop.** cvars: gpu_bd_native_drop_depth_downscale (drops, breaks
Thor — diagnostic only), depth-xfer logging (BD DEPTH XFER) in PerformTransfersAndResolveClears.**
**🎯🎯 PC-VERIFIED DEPTH-TRANSFER TARGET (2026-07-07, `BD DEPTH XFER` log on desktop — the exact surfaces): the 40
depth ownership transfers/frame are between BD's DIFFERENTLY-SIZED depth VIEWS at the SAME EDRAM base — dominant
group (23) = `base=810 pitchT=9 (720-wide) ↔ pitchT=5 (400-wide)` [or at 2x: pitchT=9 msaa1↔msaa2]; also
`base=0 pitchT=13 ↔ 7 ↔ 16`. These are MSAA + PITCH conversions of the depth = BD keeping a FULL-res depth AND
DOWNSCALED depth views (for post-processing / depth-of-field / soft particles that SAMPLE depth at lower res). The
transfers carry real depth content between the full + downscaled views. NOT tile-reinterpretation of one surface —
DEPTH DOWNSCALE/RESOLVE between views. force_max_msaa=1 does NOT help (corrupts: tile desync black-stripes,
`desk_msaa1.png`; and just converts msaa-diff → pitch-diff, still 40).** ⇒ **native-first fix = serve BD's depth
VIEWS natively (Brick-B-for-DEPTH): when the field samples a downscaled depth view, serve it from a native
downsample of the field's native depth (473) instead of the EDRAM transfer.** i.e. extend the resource-keyed
native texture-binding (currently color-only) to DEPTH samples: key each depth view (base+pitch), back it with a
native image derived from 473, redirect the depth sample → drop the transfer. THE next build = native depth-view
coverage. This is the PC-verified, concrete target (the transfers are enumerated, not guessed).
**🎯 THE ACTUAL FIX = the goal's literal words: RESOURCE-KEYED NATIVE RENDER TARGETS for ALL of BD's field EDRAM
surfaces (not just the shadows the aux path covers today). Give each BD surface (main 1280 field 471/473, the bloom
320/160/80, the composite) its OWN native RT keyed by resolve-dest — so NONE are EDRAM-aliased, so there is nothing
to reinterpret, so the ownership transfers are redundant → drop them → EDRAM deleted.** The aux resource-keying
mechanism (already built + proven for shadows) is the RIGHT tool; the build = EXTEND its coverage to the main-scene
+ bloom + composite surfaces (handle their formats/pitches/MSAA per-surface). NOT tiling-HLE (dead), NOT RT-cache
width-override (black), NOT levers (all dead). THE next build = full resource-keyed native surface coverage of the
field's EDRAM surfaces via the aux mechanism. RenderDoc (now working) verifies each surface as it's converted.
**VERIFIED FACTS (RenderDoc frame4601):**
- `471`(1280x720 "2D Color Attachment")+`473`(1280x720 depth) = **the NATIVE renderer RT** — its render passes are
  `C=Clear/Load, D=Clear/Load, S=Don't Care`, the EXACT BdNativeRenderer clear/load signature. So **the native
  renderer DOES render the main field** (scene vpw=1280 + field vpw=672) into 471/473. Both groups LessEqual+Zwrite,
  standard-Z, into the SAME native color+depth (intended).
- `456`(720x1824)+`461` = a SEPARATE RT (foliage/other). Swap = `355/356/357`; present = a single fullscreen draw
  (ev6459) into swap 357.
- Field renders CORRECT with transfers on; drops to the right strip when ALL EDRAM dropped; NDC/viewport/depth-state
  UNCHANGED; depth_clear 0.0→1.0 no effect; drop-color/keep-depth = correct, drop-depth = collapse.
**OPEN (NOT pinned — do NOT overclaim): 471 is a NATIVE RT with its OWN native depth 473 (Clear/Load), so why does
dropping EDRAM collapse it?** Leading unproven hypothesis: the field draws into 471 SAMPLE EDRAM textures/depth
(the scene/background/depth-effect from prior EDRAM passes via Brick B); the DEPTH-class sample is not fully
native-served, so dropping the EDRAM depth transfer feeds it wrong content → the field's depth-dependent draws
collapse. TO VERIFY NEXT: (1) what textures do the vpw=672 field draws SAMPLE (GetPipelineState bound SRVs) — are
any EDRAM depth/scene RTs? (2) does 473 get written by an EDRAM copy/transfer, or purely by native Clear/Load?
Only after (1)+(2) assert the fix. Captures: `bd_field_ndc_correct.txt`, `bd_field_textures_actions.txt`,
`*15.37*frame4601.rdc` (correct), `*16.35*frame4601.rdc` (dropped). Analyzers `tmp/rda2..6.py`.

**🔬 RenderDoc REPLAY (headless, WORKING): skill `xenia-renderdoc-replay`, env `tools/renderdoc/`.** When
screenshots + register logs are ambiguous ("geometry lands where the registers don't predict"), capture the frame
(cvar `gpu_bd_renderdoc_capture_frame=N` under `renderdoccmd`) + analyze per-draw (post-VS NDC = frustum clipping,
RTs, viewports) via `qrenderdoc.exe --python tools/renderdoc/rd_analyze.py` — NO GUI. This is how the BD field
buildings-black tiling bug was pinned to per-draw ndc_offset (post-VS NDC x beyond [-1,1] = off-screen-clipped).
**Desktop @~60fps PROVES BD's foliage is NOT intrinsically slow — the Thor's ~15ms is TBDR-binning/tiling/
emulation-specific.** So **"optimize HOW the foliage uses the GPU, NO gfx loss" is user-PERMITTED (2026-07-04) +
genuinely achievable** (change the submission/technique, keep the pixels). This REFRAMES the standing "foliage is
intrinsic" verdict: intrinsic on the ADRENO PATH, not the game — the emulation's Adreno submission IS the lever,
RE'd fast on PC. (All 224 D3D9 methods identified; the IB recorder is now FOUND = **0x824895C8** — see the
"NATIVE HLE — CURRENT BUILD STATE" section above. The RE is DONE; we're implementing the native draw.)

## 🔬 EXPERIMENT DB — check before running, record after (anti-repetition RAG)
`python tools/exp_ledger.py check "<keyword>"` BEFORE any device experiment or new lever; `add` after every
result. If DEAD/FLAT, do NOT re-run — build on the note. Skill: **xenia-experiment-ledger**. DB:
`docs/research/experiments.db` (human narrative: `docs/research/experiment-ledger.md`). Exists because we
repeatedly burned device runs re-deriving dead ends (grep-the-markdown kept missing them).

## ⚠️ Measurement is the #1 trap
BD's GPU scene complexity swings ~4×/second → **cross-run fps / gpu_frame_us is CONFOUNDED (worthless)**. Only
trust: single-run in-place alternating A/B on a GPU-busy frame (`gpu_freeze_ab_alternate_vrs`,
vulkan_command_processor.cc ~2280); screenshot correctness; qemu-a64 byte-identical; code facts. `gpu_frame_us`
absolute = unreliable (includes idle). **OSD fps box (screenshot it) = the truth for ONE scene.** Post-temp
change confirms the field was reached (no heating = idle/menu scene = discard run). **Derive the structural
cause from code/arch FIRST; fire the device only to CONFIRM one hypothesis, never to explore.**

## 🖤🖤🖤 BLACK SCREEN? **CHECK THE DISPLAY IS AWAKE FIRST** (cost most of a session, 2026-08-03)
**`adb shell dumpsys power | grep mWakefulness` MUST say `Awake`. If the panel is asleep the Android activity is
stopped, its SurfaceView never gets a surface, the presenter has no surface, and it silently DROPS every guest
frame — and `screencap` returns a fully black PNG (~10.9 KB at 1920x1080). The emulator is meanwhile perfectly
healthy: guest running, GPU hot, swaps issued.** Launching over adb does NOT wake the screen. Wake it with
`adb shell input keyevent KEYCODE_WAKEUP` (then `input keyevent 82` to dismiss the keyguard) BEFORE any visual
check — `tools/thor_launch.sh` does this automatically now.
- **Screenshot size is the instant tell:** ~10.9 KB = a black panel; hundreds of KB = real content.
- Burnout Revenge on the merged Edge kernel + Turnip 26.3.0: **title screen at 59.2-59.3 fps** (screenshotted).
- The presenter now SAYS so instead of failing mutely: `Presenter: guest output ready but paint mode is kNone -
  frame N dropped, nothing will be shown` (with surface/window state). Grep that first on any black screen; the
  paint chain is also traceable via logcat tag `XeniaPaint` (surfaceCreated/surfaceChanged/postInvalidate/onDraw).

## ⚠️ Never thrash the Thor (hard safety)
Before ANY launch read `/sys/class/kgsl/kgsl-3d0/temp` (milli-°C) + `gpu_busy_percentage`; launch only if
temp < 50-55°C. Force-stop `jp.xenia.emulator.github.debug` past ~70°C. Device degrades under heavy firing
(boot stalls after ~6 launches). Batch fixes, build once, fire once, fill cooldowns with device-free work.
**DO NOT reboot the device; never blame it for a result.**
- **🔀 THE THOR IS SHARED — another Claude Code session may be driving it (user, 2026-08-05).** Before firing, check
  the device is idle (`adb -s c3ca0370 shell pidof jp.xenia.emulator.github.debug` + the temp read above); a title
  already running is someone else's measurement, and installing an APK under it invalidates their run as well as
  yours. Assume any unexplained fps/thermal reading may be another session's run, and re-measure rather than
  reasoning about it. The same applies in reverse — say what you are firing before you fire it.
- **Check `git log` before starting an optimization.** Two sessions independently mining the same source will
  implement the same lever twice; on 2026-08-05 the RPCS3 ARM64 list was already ~half landed (ISB, LLVM target
  features, SHA3 detection, MIDR classification) by a parallel session within hours.

## 🦾🦾 ARM64 / SNAPDRAGON 8 GEN 2 TUNING — the RPCS3-parity track (opened 2026-08-05)
**Source of truth: `docs/research/20260805-rpcs3-arm64-optimizations-applicable.md`** — Whatcookie's RPCS3 ARM64 work
("PS3 emulation is fast on ARM now"), mined to a local transcript and cross-checked against the merged PRs, not the
video alone. **It was measured on an AYN Odin 2 = the SAME Snapdragon 8 Gen 2 as the Thor**, and the PS3 PPU and the
Xenon are both PowerPC+VMX (several items are literally the same guest instruction), so these transfer DIRECTLY, not
by analogy. Their claim — **theirs, not ours, unverified by us**: ~60% faster at ~75% power. Never restate as ours.
- **Claude cannot watch video → use the `video-transcript-mining` skill** (yt-dlp auto-captions in seconds; whisper
  only as a fallback). Guessing a talk's contents from its title, or from the author's commits, yields confident
  nonsense. Transcript first, always.
- **Device feature reality (read from /proc/cpuinfo, 2026-08-05):** `asimddp i8mm bf16 fphp asimdhp atomics lrcpc
  ilrcpc sha3`. **NO SVE/SVE2** — Qualcomm shipped 8 Gen 2 as ARMv9 without it, so every RPCS3 SVE PR (XAR, SVE
  multiplies, SVE2 FMS) is N/A to us. `sha3` DOES apply and brings EOR3/BCAX/RAX1/XAR (useful far outside crypto).
- **⚠️⚠️ THE BUG CLASS TO SWEEP FOR: `#if XE_ARCH_AMD64` guards that leave ARM64 with NOTHING.** Found 2026-08-05 in
  `xeKeKfAcquireSpinLock` (xboxkrnl_threading.cc, a `kHighFrequency` export): the remote-holder backoff loop body was
  `_mm_pause()` inside an AMD64 guard, so on ARM64 the "16-try brief spin budget" was an **empty loop** that elapsed in
  nanoseconds and fell straight through to the fiber reschedule it existed to avoid. Fixed via the new
  `xe::threading::SpinLoopHint()` (base/threading.h). **Grep for this shape before assuming any lever is live on ARM.**
- **Spin counts are WALL-CLOCK budgets, not iteration counts.** x86 `PAUSE` ≈140 cyc (Skylake+) vs ARM `ISB` ≈10-30,
  so an x86-tuned constant under-spins by ~4-8× on ARM (RPCS3 PR 18055 hit exactly this). `kRemoteHolderSpinTries` is
  now arch-split: 96 on ARM64, 16 on x64.
- **⚠️ `yield` IS A NO-OP on every core in this SoC** (X3/A715/A710/A510) — it is a hint that retires as a NOP, NOT an
  x86 `pause`. `ISB` is the closest working backoff. But see the CONFOUNDED result below before assuming it wins.
- **STATUS — do NOT redo these:** ✅ inline CNTVCT_EL0 guest timer (clock_posix.cc + a64_emitter.cc:5112; the comment
  records clock_gettime was ~15% of CPU on timing-heavy titles) ✅ VPERM→TBL (a64 emits `tbl`; LLVM emits 2×TBL1 OR'd,
  deliberately — the TBL2 intrinsic needs a consecutive register pair we cannot satisfy) ✅ MIDR_EL1 core
  classification (platform_arm64.cc) ✅ LLVM target features ✅ FEAT_SHA3 detection ✅ `fmax`/`fmin` native in a64.
  ⚠️ `a64_spin_hint_isb` = **CONFOUNDED / default-off / NO WIN measured** — frame-capped title screen + unequal
  thermal start (53.1°C vs 57.8°C); a refutation it is NOT. Retest in a real race from equal temps.
  ❌ The A510 "two of three share a vector unit" claim is **REFUTED on the Thor** by our own probe (34014db95) —
  do not re-plumb thread affinity around it.
  **OPEN:** vmaxfp/vminfp `FixupVmxMaxMinNan` removal (#2 — 6 extra insns on every VMX float max/min; ARM `fmax` NaN
  semantics may already match VMX, but this is correctness-critical, verify with the qemu-a64 differential, do NOT
  eyeball it); ABD/ABA 3-input mid-core trick (#4, the real differentiator — nobody else has it); EOR3/BCAX
  codegen consumers (#5, detection landed but nothing reads it); UDOT byte-sum (#7).
- **Items the FIRST mining pass missed — LLVM-on-ARM codegen workarounds (from the transcript, 2026-08-05):** LLVM
  scalarizes some vector ops on ARM (fix = write the IR idiomatically to match the x86 shape; `CMTST` is a fused
  and+compare); LLVM doesn't know ARM shifts shift in zeros (use the intrinsic, ~2 insns saved per shift); a
  compare+select chain collapses to one `BSL` (15 insns → 1 in their SPU FCGT case); and **re-rolling fully-unrolled
  codegen back into a loop is ~2% on BOTH arches** via code-cache pressure — directly relevant to our AOT precompile.

## CPU = AOT-LLVM (the committed CPU direction)
Whole-fn HIR→LLVM→ORCv2 recompiler. ⚠️ **`cpu_backend_llvm` is DEFAULT **OFF*** (llvm_backend.cc:55 AND the
device's `files/xenia.config.toml`) — this file previously said "default-on", which is WRONG and cost a device
A/B on 2026-07-24: a lever stack containing the LLVM-ONLY residency cvars was measured WITHOUT ever enabling
the LLVM backend, so all three were silent no-ops. **Any LLVM-lever experiment MUST pass `--ez cpu_backend_llvm
true` explicitly.** (`cpu_backend_llvm_opt` does default to 2.) Renders BD,
100% emittable-opcode coverage, hybrid a64 fallback. Model = RPCS3/ReXGlue: precompile all fns, residency,
direct calls, NO JIT/dispatch at gameplay. Levers: **residency** (`cpu_backend_llvm_residency_writeback` = WIN,
qemu byte-identical, smoother+cooler), inline-cache direct calls (~13%), NEON for VMX. Payoff: CPU-bound titles
(Burnout/Gears → 30) + BD heat/sustained-fps. Detail + build recipe: memory `[[llvm-jit-backend-build]]`. Open
bugs (fix forward): opt=2 residency crash, BD-cyan codegen `[[bd-llvm-postload-3d-cyan-bug]]`. Validate every
change: qemu-a64 differential + device render/no-fault (qemu can't catch SVE-SIGILL / q-decode hangs).

## Device
AYN Thor, ADB `c3ca0370`. Snapdragon 8 Gen 2 (QCS8550), Android 13, ~16GB UMA. ADB:
`C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe`. Package `jp.xenia.emulator.github.debug`.
- **CPU:** 8× ARMv9 (1× Cortex-X3 @3.19GHz = cpu7, 4× A715/710 @2.8, 3× A510 @2.0). **NEON only (no SVE).**
  asimddp/i8mm = int8/bf16 matrix (heuristics only, NEVER guest FP32 geometry — it black-screens); fphp = FP16
  (pixel-shader only).
- **GPU:** Adreno 740v2, TBDR, Vulkan 1.3, **Turnip (Mesa)** driver. GMEM = EDRAM-emulation target. Single
  graphics+compute queue. LRZ early-Z defeated by alpha-test/discard. EDRAM resolves = per-pixel compute dispatches.
- **Driver = Mesa Turnip, BUNDLED in-APK** (`assets/drivers/turnip.zip`, auto-installs + auto-selects via
  GpuDriverManager). **⚠️ Do NOT swap to the Qualcomm blob (v840/837) — it LACKS ROAA / dynamic_rendering_local_read
  = a downgrade.** Mesa source at WSL `/root/mesa` (build-android works) for driver patches. PRESENT: ROAA,
  dynamic_rendering_local_read, multisampled_render_to_single_sampled, custom_resolve, load_store_op_none.
  ABSENT: fragment_shader_interlock (but ROAA covers same-pixel; interlock is DEAD for BD — its composites are
  neighboring-pixel bloom). Feature audit: `docs/research/20260620-adreno-turnip-feature-gap-audit.md`.

## 🟩🟩🟩 STANDING DIRECTIVE (user 2026-08-03): ALWAYS SHIP THE LATEST TURNIP — newer = more perf extensions
**"we need the latest turnips always they have extra apis for performance."** Turnip is where the Adreno perf
extensions land (ROAA, dynamic_rendering_local_read, custom_resolve, load_store_op_none all arrived this way), so we
TRACK UPSTREAM — never pin. **`python tools/update_turnip.py`** fetches the newest AdrenoTools build, validates the
zip (meta.json + the .so it names — a bad zip would silently fall back to the Qualcomm driver on every device),
writes `assets/drivers/turnip.zip`, and rewrites `GpuDriverManager.BUNDLED_TURNIP_VERSION`. `--check` reports
without writing; `--tag` pins one release. Source = `The412Banner/Banners-Turnip` (rebuilds from Mesa main on EVERY
upstream commit; take the plain non-suffixed asset = A6xx/A7xx, correct for the Thor's Adreno 740 — NOT the A8xx or
-Test variants). Run it at the start of any GPU work.
- **The driver ships INSIDE the APK ⇒ updating it = rebuild + reinstall.** Bumping the version alone does nothing
  until repackaged.
- **⚠️ These are automated bleeding-edge builds, NOT guaranteed stable.** Always device-validate after updating:
  launch a title FROM THE IN-APP GUI and confirm the log says `driverID` = `VK_DRIVER_ID_MESA_TURNIP`. If it says
  `driverID=8` / logs `AdrenoVK-0`, you are on the Qualcomm proprietary driver and every GPU measurement is invalid.
  Roll back with `--tag <older>` if a build regresses.
- **🛑 A BARE `adb shell am start` RUNS THE QUALCOMM DRIVER** — Turnip is applied by the Java launch path
  (`GpuDriverManager.applyToLaunch`), which only runs on a GUI launch. Bit me 2026-08-03: a whole Burnout
  bring-up session ran on AdrenoVK-0 before I noticed. **But the four driver extras ARE allowlisted in
  EmulatorActivity, so headless Turnip launches DO work** — pass them explicitly:
  `--es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path '<files>/gpu_drivers/<id>/'` (trailing slash
  REQUIRED) `--es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path '<nativeLibraryDir>'`
  (get it from `pm path <pkg>` → replace `/base.apk` with `/lib/arm64`; get `<id>` from
  `run-as <pkg> ls files/gpu_drivers/`).
  **VERIFY EVERY RUN:** the log must say `Loaded Turnip Vulkan driver ... via libadrenotools` and the physical
  device must print as **`Turnip Adreno (TM) 740`**. Plain `Adreno (TM) 740` + `AdrenoVK-0` = the Qualcomm blob and
  every GPU number from that run is invalid.
- **Upgrades reach existing devices** (fixed 2026-08-03): `ensureBundledDriverInstalled` used to skip whenever ANY
  driver was installed — including the one it had installed itself — so version bumps only ever helped FRESH
  installs. It now tracks the bundle-installed id (`KEY_BUNDLED_GPU_DRIVER_ID`), replaces exactly that package,
  re-selects it if it was selected, and deletes the superseded one. Drivers the user imported are never touched.
  "Already handled" requires the marker AND the recorded package to exist on disk — a marker-only check let the two
  disagree permanently (device recorded the new version while still running the old driver).
- **NEW SINCE THE 26.0 AUDIT (device-enumerated on Turnip 26.3.0, 174 device extensions — availability only, NOT
  measured):** `VK_EXT_descriptor_buffer` (cheaper descriptor updates than our push-descriptor path),
  `VK_EXT_host_image_copy` (CPU→image upload with no staging buffer or queue submit — texture uploads),
  `VK_EXT_graphics_pipeline_library` (pipeline-creation cost / shader stutter), `VK_KHR_maintenance5/6/7`,
  `VK_EXT_attachment_feedback_loop_dynamic_state`. All five 26.0-era perf extensions still PRESENT (ROAA,
  dynamic_rendering_local_read, custom_resolve, load_store_op_none, multisampled_render_to_single_sampled).
  `fragment_shader_interlock` still ABSENT (still DEAD for BD — ROAA covers same-pixel). Re-audit on each bump.

## Build / deploy / run
- **Path has spaces** → junction: `cmd /c mklink /J C:\xt "<repo>"`, build from `C:\xt\android\android_studio_project`
  (`subst X:` is NOT visible to background gradle tasks).
- **APK:** `cmd /c "C:\xt\android\android_studio_project\gradlew.bat -p C:\xt\android\android_studio_project
  :app:assembleGithubDebug"` (native ~1-2.5min). If `mergeResources` fails, run `:app:mergeGithubDebugResources`
  once first. APK: `android/.../app-github-debug.apk`. Install: `adb -s c3ca0370 install -r <APK>`.
- **BD launch:** ISO `/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`,
  turnip driver flags, `--es hid nop --es hid_nop_button_sequence '<start/a seq>'`. Heavy field ~120-135s. Reusable
  scripts: `scratch/thor-debug/*.ps1`. Trace: `--ez vulkan_trace_draw_outcomes_per_frame true` → the
  `GPU draw outcomes/frame` logcat line (rendered, pipeline_binds, guest_ms, cpu_* buckets).

## Shipping a win — the propagation loop (do ALL or the device runs stale)
1. **NEW cvar → allowlist it in `EmulatorActivity.java` onCreate** (copyBooleanExtra/Int/String) or `--ez/--ei/--es`
   silently no-ops → you measure the wrong thing.
2. Per-game win → add to that title's `GameProfiles` profile (with the validated "why"). Cross-game → flip
   `XeniaOptimizations` `defaultEnabled=true`. Frame cap per game via `gpu_frame_limit_fps` (BD=30, Burnout=60).
3. **REBUILD + REINSTALL the APK** — the Java registry/profiles DON'T reach the device until repackaged (stale
   APK = the silent-default-off confound, on-device — root cause of a "10fps Burnout" that `--ez` read at 46).
4. **VERIFY from the in-app UI launch (not `--ez`)** — reproduces the measured fps (menus uncapped = install stale).
- **Test stack:** every `--ez` A/B must pass the FULL validated set (reuse `$opts` in
  `scratch/thor-debug/measure_const_promo.ps1`) or it's confounded (silently missing flat_membase, the fence fix,
  etc.). Effective-value layering (highest wins): launch Bundle > `files/xenia.config.toml` > compiled default.

## Device-free testing (prefer over firing)
- **Host x64 cpu-tests:** `MSBuild build\xenia-cpu-tests.vcxproj /p:Configuration="Debug Windows" /p:Platform=x64`;
  run `build/bin/Windows/Debug/xenia-cpu-tests.exe "FILTER_*"` (Catch2).
- **qemu-a64** (real ARM64 backend, no device): WSL + aarch64-linux-gnu-g++ + qemu-aarch64. Recipe: `[[a64-qemu-harness]]`.
- **RE:** Ghidra 12.0.4 at `...\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC` (headless in the MAIN loop, long
  timeout, never in a subagent). **page-watch** (`cpu_watch_guest_write_page`) = the RE unblock: host-mprotect a
  guest page + emulate-on-fault at full speed. Skills: `xenia-ghidra-ooda-loop`, `xbox360-d3d-hle-recomp`.
- Mine existing captures (log-grep) instead of re-firing where possible.

## Game patches
`.patch.toml` (src/xenia/patcher/): be8/16/32/64 guest-memory writes, matched by title_id + build hash ("Module
Hash" in logcat). Applied in `KernelState::LoadUserModule` before execution. Skill: `xenia-thor-ghidra-game-patch`.

## Config + git rules
- Device persists `files/xenia.config.toml` (overrides compiled cvar defaults; only `--ez/--ei/--es` beat it).
- **Never fabricate** — every number read from device output THIS turn; unmeasured = say so; cross-run = say
  CONFOUNDED; read the screenshot before asserting a visual.
- **Targeted `git add` only (never `-A`).** Never commit ISOs/keys/dumps/screenshots/config backups. Work on
  `master`. **Forward-only** (never `git revert`; fix forward). End commits:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- Skills in `.agents/skills/`; knowledge in memory files (`MEMORY.md` index).
- **CONSULT CODEX gpt-5.6-sol (MAX) FOR HARD REARCH** — for any hard architecture/rearch/root-cause problem, get a
  second opinion from **`gpt-5.6-sol` at reasoning effort `max`** via the Codex CLI (the OpenAI/ChatGPT tool; the
  configured default in `~/.codex/config.toml` = `model = "gpt-5.6-sol"`, `model_reasoning_effort = "max"`). Run
  READ-ONLY: `codex exec -c model_reasoning_effort=max --sandbox read-only -` (uses the default sol; or `-m
  gpt-5.6-sol`). Pipe the prompt via stdin; give it the measured diagnosis + exact file:line context and ask it to
  cite code. It PAID OFF 2026-07-10 (caught a wrong build — the float-in-pass variant — before it was written, via
  `skip_fmt=0`; designed the color-only native HLE plan; hardened the drop gate to fail-closed).
  ⚠️ Only the PLAIN model names work on the ChatGPT-account login (`gpt-5.6-sol`, `gpt-5.6-terra`); the
  `-max`/`-high`/bare-`gpt-5.6` suffixes are rejected ("not supported when using Codex with a ChatGPT account") —
  set reasoning via `-c model_reasoning_effort=max`, NOT a name suffix. `gemini` CLI is the fallback.

## Autonomous mode (standing directive)
Pick the highest-value unit yourself, execute end-to-end (implement → build-verify → device-test → commit →
next). Don't ask which task / re-confirm direction / analysis-paralyze. A big effort is a reason to start, not
to ask. Surface only genuine external blockers. Thermal + no-fabrication rules always hold.
