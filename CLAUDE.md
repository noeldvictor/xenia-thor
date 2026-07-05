# CLAUDE.md — xenia-thor (Xbox 360 emulator on the AYN Thor)

## Goal
Xbox 360 games fast + playable on the AYN Thor (Snapdragon 8 Gen 2 / Adreno 740). **Blue Dragon → 30fps @ 720p
full foliage; Burnout/Gears/Lost Odyssey/Banjo → 30-60.** Ship every win as a cvar-gated, per-game
`GameProfiles` / `XeniaOptimizations` toggle (default-off until validated).

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
