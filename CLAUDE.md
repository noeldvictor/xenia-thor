# CLAUDE.md — xenia-thor (Xbox 360 emulator on the AYN Thor)

## Goal
Xbox 360 games fast + playable on the AYN Thor (Snapdragon 8 Gen 2 / Adreno 740). **Blue Dragon → 30fps @ 720p
full foliage; Burnout/Gears/Lost Odyssey/Banjo → 30-60.** Ship every win as a cvar-gated, per-game
`GameProfiles` / `XeniaOptimizations` toggle (default-off until validated).

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

## ⚠️ Never thrash the Thor (hard safety)
Before ANY launch read `/sys/class/kgsl/kgsl-3d0/temp` (milli-°C) + `gpu_busy_percentage`; launch only if
temp < 50-55°C. Force-stop `jp.xenia.emulator.github.debug` past ~70°C. Device degrades under heavy firing
(boot stalls after ~6 launches). Batch fixes, build once, fire once, fill cooldowns with device-free work.
**DO NOT reboot the device; never blame it for a result.**

## CPU = AOT-LLVM (the committed CPU direction)
Whole-fn HIR→LLVM→ORCv2 recompiler, **default-on** (`cpu_backend_llvm`, `cpu_backend_llvm_opt`=2), renders BD,
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
- **Driver = custom Mesa Turnip 26.0 (R8/rc08), BUNDLED in-APK** (`assets/drivers/turnip.zip`, auto-installs via
  GpuDriverManager). **⚠️ Do NOT swap to the Qualcomm blob (v840/837) — it LACKS ROAA / dynamic_rendering_local_read
  = a downgrade.** Mesa source at WSL `/root/mesa` (build-android works) for driver patches. PRESENT: ROAA,
  dynamic_rendering_local_read, multisampled_render_to_single_sampled, custom_resolve, load_store_op_none.
  ABSENT: fragment_shader_interlock (but ROAA covers same-pixel; interlock is DEAD for BD — its composites are
  neighboring-pixel bloom). Feature audit: `docs/research/20260620-adreno-turnip-feature-gap-audit.md`.

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
- Skills in `.agents/skills/`; knowledge in memory files (`MEMORY.md` index). Consult Codex/Gemini for hard
  rearch (`[[consult-hard]]`).

## Autonomous mode (standing directive)
Pick the highest-value unit yourself, execute end-to-end (implement → build-verify → device-test → commit →
next). Don't ask which task / re-confirm direction / analysis-paralyze. A big effort is a reason to start, not
to ask. Surface only genuine external blockers. Thermal + no-fabrication rules always hold.
