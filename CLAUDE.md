# CLAUDE.md — xenia-thor (Xbox 360 emulator on AYN Thor)

Guidance for Claude working in this repo. Read this first.

## Goal
Make Xbox 360 games run **fast and playable** on the **AYN Thor** Android handheld via this xenia fork.
Priority title: **Blue Dragon at full speed**. Other priority titles: Lost Odyssey, Banjo, Burnout.
Thor is ~10-20× the 360 in raw FLOPS/cores, but that is mostly **latent parallel + GPU throughput**;
emulation pays a heavy CPU translation tax and currently uses ~1 of 8 cores, so 4-6fps is not a
hardware limit — it's where the per-bottleneck work goes.

## ⚡ MISSION (2026-06-04): the BEST, FASTEST, most-optimized xenia ever, for the AYN Thor
**Every little optimization adds up.** Pursue the best codegen (recompiler quality), a fixed binning
front-end, and the best thread placement for this exact SoC — then **stack** the wins. Each perf win is
exposed as a **user-selectable, explained toggle in the in-app Settings UI** (registry:
`XeniaOptimizations` → rendered by `SettingsActivity` "Performance optimizations" section → wired to
engine cvars by `XeniaAndroidSettings.createLaunchArguments`). The point of the UI is to **see how the
little advances compound into big advances** by toggling/stacking them. Adding a new optimization =
add ONE `XeniaOptimizations` registry entry (it auto-appears in the UI and auto-wires its cvar) +
allowlist the cvar in `EmulatorActivity`. Default the device-validated wins ON; keep risky/experimental
ones toggleable (cvar still the engine mechanism, but surfaced + explained, not hidden).

## 🩹 GAME PATCHES (core feature, 2026-06-04) — a first-class part of the emu
The fork now has the **game-patch system** (xenia-canary `.patch.toml`), ported
back after this fork had dropped it. Patches are a core idea: **performance**
fixes (60 FPS, disable blur/SSAO/LoD), **cheats** (infinite lives/money/ammo),
and **compatibility bypasses** (e.g. Banjo's dirty-disc false-verification).
- **Engine** (`src/xenia/patcher/`, in `xenia-core`): `PatchDB` loads
  `files/patches/*.patch.toml`, matched by `title_id` + build hash; `Patcher`
  writes `be8/be16/be32/be64/f32/f64/string/u16string/array` values into guest
  memory. `KernelState::LoadUserModule` calls `ApplyPatchesForTitle` right after
  `module->Dump()`. Adapted to the fork's **cpptoml** (upstream uses tomlplusplus).
  The match hash is `UserModule::CalculateHash()` = **XXH3-64 of the code
  section**, logged by `Dump()` as `Module Hash: {:016X}` — read it from logcat
  to author a patch. `apply_patches` cvar (default on).
- **Patch addresses are guest effective addresses** (`0x82xxxxxx`); a `be32`
  value is a big-endian PPC instruction word (`0x60000000` = `nop`). Storage on
  Android is `getFilesDir()/patches` (== engine `storage_root/patches`).
- **In-app manager**: the launcher's per-game actions → **"Game patches"** (shown
  when the title ID resolves) → `GamePatchManagerActivity`. Downloads every
  matching `.patch.toml` for the title from
  **github.com/xenia-canary/game-patches** by title-id, lists each `[[patch]]`
  with a **Switch** (perf + cheats together), writes `is_enabled` back. Logic in
  `GamePatchManager.java` (mirrors `GpuDriverManager`).
- **Authoring new patches** (incl. the **Banjo dirty-disc bypass** — a guest-side
  PowerPC false verification, NOT a file/disc problem; the ISO is SHA-identical
  to a working PC copy): use the **`.agents/skills/xenia-thor-ghidra-game-patch`**
  skill (Ghidra: extract+load the XEX as `PowerPC:BE:32` @ `0x82000000`, find the
  bad branch / decrement, emit `.patch.toml`). `scripts/emit_patch_toml.py` +
  `references/ppc_patch_cookbook.md` help. See memory
  [[banjo-dirty-disc-stuck-pending]].

## 🚀 DRAMATIC VISION (2026-06-04, user direction) — TWO TRACKS IN PARALLEL, not either/or
The user is done with CPU-vs-GPU flip-flopping and timid step-by-step. Pursue **both** tracks at once:
- **CPU track** — recomp / best codegen / multicore / hardware accelerators. Speeds up **CPU-bound
  titles** (Lost Odyssey, and non-vista BD gameplay). This is where the recomp ambition lives.
- **GPU track** — binning / draw-coalescing / Turnip driver. The **BD-heavy-scene fps lever**, which
  recomp and codegen do NOT touch. Draw-coalescing renderer + the NEON triangle-cull micro-interpreter
  + driver work.
Neither substitutes for the other. Full rationale + the four locked direction choices:
[[thor-two-track-vision]]. The four (max-ambition) picks:
1. **Recomp** = full ground-up: keep the baseline JIT for cold code **+ a 2nd optimizing tier** for hot
   blocks (block-linking, cross-block regalloc, inlining, dead-flag/CR elimination, macro-op fusion).
   **LLVM stays OUT** (RPCS3 data: ~40% slower than a hand JIT).
2. **HW accelerators** = push them even for guest math (NEON + int8/bf16 matrix + Hexagon DSP/NPU), but
   **gate per-op/per-shader-class, validate on-device, NEVER on guest position/geometry precision**
   ([[approx-math-guest-visible-vs-heuristics]] — FP16/int8 on guest-visible math has black-screened BD).
3. **Driver manager** = full in-app GPU-driver downloader (URL or bundled Turnip → install to app files →
   libadrenotools hook → per-version recommendation text for Adreno 740; Citron/Winlator-style).
4. **Multicore** = host-subsystem offload to idle cores + parallel JIT translation + speculative
   guest-thread spreading; **fix the latent publication races first** (parallel-jit-design findings in
   [[major-refactor-build-progress]]).
Every win still ships as an explained, stacking `XeniaOptimizations` Settings toggle.

### Static-recomp verdict (workflow 2026-06-04) — answered, do NOT port
A Blue-Dragon-specific static recomp EXISTS — **re:Blue** (github.com/zolaware/reblue) on the **ReXGlue**
SDK (Xenia-derived AOT C++ codegen); plus general **XenonRecomp**/**XenosRecomp** and the Android-Vulkan
precedent **LibertyRecomp**. But static recomp's win is native ARM64 CPU — a cost **we don't have** (BD
is GPU-binding-bound, CPU ~75% idle). It does NOT reduce draws/binning; recomp renderers re-submit the
original draw stream 1:1. So **do not build a BD recomp port** (months of per-game RE, no Android build,
"barely playable" on PC). **STEAL** into xenia instead (each a toggle): FP16/RelaxedPrecision SPIR-V
decorations (Adreno 740 has native FP16; our translator emits none — never on position), persistent
on-disk pipeline-cache pre-warm (kills shader-compile stutter), ARMv9 FLAGM CR/XER flag handling. Detail:
[[bd-recomp-verdict]].

### Per-stage GPU profiling is a FULL-ADB job (not a GUI punt)
The app is debuggable + we ship Turnip, so the per-stage GPU split (binning/vertex vs fragment vs stall,
per-draw cost) is reachable over **full ADB, no root, no GUI** — via the Mesa/Turnip freedreno perfetto
counter producer, gfxreconstruct `.gfxr` capture+replay profiling, in-engine per-pass Vulkan timestamps,
or the AGI CLI. Skill: **`.agents/skills/xenia-thor-adb-gpu-stage-split`** (Snapdragon/AGI GUI demoted to
last resort). This unblocks the one load-bearing BD unknown — is the ~333µs/draw a removable per-draw
stall/context-roll or an irreducible per-primitive binning floor — instead of waiting on a user capture.

## ⚡ Current status (2026-06-04) — READ before optimizing
- **Turnip (Mesa) driver WORKS and is the FAST+CORRECT path.** The months-long black screen was xenia
  backing the 512MB guest-RAM mirror with a SPARSE buffer Turnip doesn't reliably back (vertex-fetch
  read zero → degenerate gl_Position). Fixed: non-sparse on `driverID==VK_DRIVER_ID_MESA_TURNIP`
  (`vulkan_shared_memory.cc`, committed 61c5600e9). All priority games render on Turnip now.
- **R2 + R3 refactors BUILT + DEVICE-VALIDATED on Blue Dragon (2026-06-04), both cvar-gated default-off.**
  R3 `arm64_use_flat_membase` (folds guest load/store to `[membase,Wn,UXTW]` indexed addressing, the
  a64 backend) and R2 `vulkan_dynamic_constants_arena` (persistent UMA constant rings + one
  UNIFORM_BUFFER_DYNAMIC descriptor set, no per-draw transient descriptor alloc) both render
  PIXEL-CORRECT on the BD heavy field, and compose (both-on correct). R2 cut `cpu_bind_us` ~8700→~7000
  (a CPU-hygiene win); both leave `gpu_frame_us` ~127k UNCHANGED — they do NOT move GPU-bound BD's fps
  (as predicted), so the fps payoff is for CPU-bound titles (Lost Odyssey, test next). R5 draw-concat was
  DROPPED — the merge path already elides all redundant state, so there was no slack to coalesce.
- **Blue Dragon heavy scene on Turnip is GPU-BOUND on the binning front-end** (CONFIRMED clean
  2026-06-03): GPU busy ~80%, CPU ~75% idle, NO guest thread pegged (guest/JIT threads 2-5%). The
  bottleneck is ~1100-2180 tiny draws / ~263k verts/frame. BD levers = **reduce DRAWS + submitted
  VERTICES** (Adreno bins per-vertex-per-draw *before* cull, so GPU post-bin cull = zero win).
  CPU/JIT levers do NOT speed BD (CPU idle) — they're for the CPU-bound titles (Lost Odyssey).
- **MEASUREMENT DISCIPLINE (cost real days):** measure perf with `tools/thor/thor_gpu_capture.ps1
  -NoDump` (the RT-dump readbacks poison timing — they made `gpu_frame_us` read a bogus ~1ms and
  led to a wrong "CPU-bound" verdict). **Trust the KGSL GPU busy% + per-thread `top` (`-TopProfile`),
  NOT the derived `gpu_frame_us`.**
- **Optimization roadmap:** `docs/research/20260603-thor-hyperopt-roadmap.md` (R1-R8) + live status in
  the `major-refactor-build-progress` memory. DONE + device-validated: **R3** (flat-membase) + **R2**
  (dynamic constants arena). DROPPED: **R5** (the draw-merge path already elides all redundant state).
  DEFERRED (silent-corruption risk, want device-per-step validation): **R4** (CPU pre-cull — also measured
  a NET LOSS per-triangle on strip-heavy BD, `draw_extent_estimator.cc:1346`) + **MC** (parallel JIT).
  BD is GPU-bound on binning, so the BD fps win still needs a DRAW/VERTEX reducer; R2/R3 are
  CPU-bound-title wins. NOTE: the old "every guest load/store emits a BRANCH+ALU" was WINDOWS-only (64K
  alloc granularity); on Android (4K page) it was just a `mov`, now folded away by R3's UXTW addressing.

## ⚠️ NEVER THRASH THE THOR (hard rule — it crashed the device once; do not repeat)
- The device is physical hardware. Repeated game launches pinned the GPU at 99% / 72°C and CRASHED it.
- **The BUILD is the primary verification, not the device.** Refactor → confirm it compiles+links → commit. Touch the device only for rare, gentle, supervised checks.
- **DEFENSIVE: if you see the GPU thrashing — `gpu_busy_percentage` pegged high or `temp` (milli-°C) climbing past ~70°C — immediately `am force-stop jp.xenia.emulator.github.debug` to shut the emulator down and let it cool.** Do not push through it.
- **Never the launch-after-launch loop.** Before ANY launch: read `/sys/class/kgsl/kgsl-3d0/gpu_busy_percentage` + `/sys/class/kgsl/kgsl-3d0/temp`; only launch if busy is low and temp < 60°C.
- The capture harness `tools/thor/thor_evidence.ps1` now ENFORCES this: it refuses to launch when hot/busy (cools first, force-stops the emu), and a watchdog force-stops mid-capture if temp ≥ 80°C. Use it; don't hand-roll `am start` loops.
- Read GPU temp: `adb -s c3ca0370 shell cat /sys/class/kgsl/kgsl-3d0/temp` (value is milli-°C; 55900 = 55.9°C).

## Device (the ONE target — optimize for it specifically)
- AYN Thor, ADB serial **`c3ca0370`**. SoC: Snapdragon 8 Gen 2 = **QCS8550** ("kalama"), Android 13
  (SDK 33), kernel 5.15. **~16 GB UMA** (unified CPU/GPU memory — exploit for zero-copy geometry feeds).
- **CPU — 8-core ARMv9.0-A, heterogeneous (pin hot threads to the right cluster):**
  - 1× **Cortex-X3** (cpu7) @ **3.19 GHz** — prime; pin the GPU-command / JIT-hot thread here.
  - 2× A715 + 2× A710 (cpu3-6) @ **2.80 GHz**; 3× A510 (cpu0-2) @ **2.02 GHz**.
  - ISA (device `/proc/cpuinfo`, all cores): NEON(`asimd`), **`asimddp`** (DOTPROD SDOT/UDOT 4×int8),
    **`i8mm`** (SMMLA/UMMLA int8 matrix), **`bf16`** (BFMMLA/BFDOT), **`fphp`+`asimdhp`+`asimdfhm`** (FP16
    arith + FMLAL), **`fcma`** (FCADD/FCMLA complex), **`flagm`/`flagm2`** (RMIF/SETF/CFINV), **`atomics`**
    (LSE LDADD/SWP/CAS), **`rcpc`/`ilrcpc`** (LDAPR weak-acquire), `jscvt`(FJCVTZS), `frint`, `crc32`,
    `aes`/`pmull`/`sha1-3`/`sha512`. **NO SVE/SVE2** → NEON is the ONLY SIMD (128-bit). PAC/BTI = security.
- **GPU: Adreno 740v2** — **tile-based deferred renderer (TBDR / FlexRender)**, Vulkan **1.3**, proprietary
  Qualcomm driver (NOT Turnip). Freq table MHz: 680 615 550 475 401 348 295 220 124.8 (**max 680**).
  On-chip **GMEM** tile buffer = the EDRAM-emulation target. The **binning pass runs the position VS per
  vertex, per draw** (the device-measured ~300µs/draw floor; backface/LRZ reject happens AFTER binning).
  - Vulkan PRESENT (use): **VK_KHR_push_descriptor** (shipped), **VK_EXT_extended_dynamic_state 1/2** (core
    in 1.3) + EDS3, **VK_KHR_draw_indirect_count** + draw_indirect, `multiDrawIndirect` (feature, enableable),
    vertex_input_dynamic_state, maintenance1-4.
  - Vulkan ABSENT (do NOT design around): **`VK_EXT_fragment_shader_interlock`** (no single-pass EDRAM),
    `VK_EXT_multi_draw`, `VK_EXT_descriptor_buffer`, `VK_EXT_external_memory_host` (pure zero-copy UMA dead).
- **HW ↔ guest-math mapping (what's safe to exploit):** guest is PowerPC **VMX128** (128-bit FP32 SIMD) +
  scalar FP. NEON maps VMX128 1:1 (done in the a64 JIT; vector ops are not C-thunked). **`asimddp`/`i8mm`/
  `bf16` are int8/bf16 matrix units — NOT usable for guest FP32 geometry** (precision loss → guest-visible
  divergence, HELD per [[approx-math-guest-visible-vs-heuristics]]); they ARE safe for INTERNAL heuristics
  (cull tests, extent estimation). `fcma` accelerates complex/rotation math; `flagm` shipped for ADD_CARRY,
  `atomics`(LSE) for kernel locks (CPU-bound titles like Lost Odyssey). On the GPU-bound BD scene the **CPU
  is ~85% idle** → spare X3/A715 headroom for NEON pre-transform / triangle-cull feeds via UMA.
- ADB path: `C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe`
- Package: `jp.xenia.emulator.github.debug`

## Build / deploy / run (Windows)
- Path has spaces → use a subst drive for builds: `subst X: "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor"`.
- Build (foreground; **background tasks do NOT see the X: subst** — use real paths or run in foreground):
  `cmd /c "X:\android\android_studio_project\gradlew.bat -p X:\android\android_studio_project :app:assembleGithubDebug"`
  Native change ≈ 1-2.5 min; java-only ≈ 20s. APK: `android/android_studio_project/app/build/outputs/apk/github/debug/app-github-debug.apk`.
- Install: `adb -s c3ca0370 install -r <APK>`
- Launch Blue Dragon (heavy 3D field scene ≈ 120-135s after launch; intros are slow at 1-2fps):
  ISO `/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`
  `am start -W -n <comp> --es gpu vulkan --es cpu arm64 --es apu android --es hid nop --es hid_nop_button_sequence 'start@20000:300;a@26000:300;start@32000:300;a@38000:300;start@45000:300;a@52000:300;start@60000:300;a@70000:300;start@82000:300;a@92000:300;start@102000:300;a@112000:300' --ez arm64_enable_mini_jit true --ez android_hide_osd true --ez mount_cache true --es target '<ISO>'`
- fps proxy: count `VdSwap(` logcat lines per second. Leave the device idle (force-stop) when done.

## Config gotcha (cost real days — remember it)
The device persists `files/xenia.config.toml` which **OVERRIDES compiled cvar defaults** (only
`--ez/--ei` intent extras beat it). A stale cvar left there caused a months-long phantom "black-3D"
bug (it was `arm64_vmx_dot_f32_fastpath=true`, a known BD black-idler). When defaults seem ignored,
check/clear the device TOML. New cvars must be added to the **EmulatorActivity allowlist**
(`android/.../EmulatorActivity.java`, the `copyBooleanExtra/copyIntExtra/copyStringExtra` lists) to be
settable via `--ez/--ei/--es`.

## Measurement harness (built this effort — USE IT, all default-off / read-only)
Enable with `--ez vulkan_trace_draw_outcomes_per_frame true`; reads the `GPU draw outcomes/frame`
logcat line. Fields: `rendered`, `avg_vertices`, `gpu_frame_us` (Vulkan timestamp = real GPU time/frame),
`guest_ms` (scene key), per-phase CPU buckets (`cpu_issuedraw/process/tex/rt/pipe/bind`), barrier-break
attribution (`brk_open/buf/img_sr/img_oth`), draw composition (`prim[...]`, `vtx[...]`).
Diagnostic cvars: `gpu_edram_passes_dont_care`, `gpu_skip_edram_transfers`, `gpu_freeze_at_guest_ms`.
GPU-vendor profiling over ADB (no GUI): skill **`.agents/skills/xenia-thor-gpu-profile`** — KGSL
`gpu_busy_percentage`/`clock_mhz` triage. Guarded perfetto capture: `tools/thor/thor_gpu_perfetto.ps1`.

### GPU telemetry: what the Thor exposes vs what's reachable headless (verified 2026-06-02)
The Adreno/KGSL driver exposes a RICH ftrace event set under `/sys/kernel/tracing/events/` — far
more than `gpu_busy_percentage`. Notable groups/events:
- **`kgsl/` (command-batch lifecycle = real GPU submission timing):** `adreno_cmdbatch_queued` →
  `adreno_cmdbatch_submitted` → `adreno_cmdbatch_retired` (submitted→retired delta = GPU exec time
  per submission), `adreno_cmdbatch_ready/done/fault/recovery/sync`.
- **CPU↔GPU stalls (bubbles):** `kgsl_waittimestamp_entry`/`kgsl_waittimestamp_exit` (CPU blocking on
  a GPU timestamp), `adreno_drawctxt_wait_start`/`wait_done`/`sleep`/`wake`, `syncpoint_*`.
- **Clock/power:** `kgsl/gpu_frequency`, `kgsl_pwrlevel`, `kgsl_gmu_pwrlevel`, `kgsl_gpubusy`,
  `kgsl_bus`, `kgsl_clock_throttling`, `kgsl_bcl_clock_throttling`, `kgsl_thermal_constraint`.
- **Faults/preemption/memory:** `adreno_gpu_fault`, `kgsl_mmu_pagefault`, `adreno_hw_preempt_*`,
  `adreno_preempt_trigger/done`, `kgsl_mem_alloc/free/map/sync_cache`, `gpu_mem/`, `msm_vidc_events/`.
- Also `power/gpu_work_period` exists (per-uid GPU-active time).

**CRITICAL: none of these are armable headless on the retail Thor (no root).** Shell is uid 2000
(group `readtracefs` so it can *read* `/sys/kernel/tracing/trace`, but `echo 1 > .../events/<ev>/enable`
= **Permission denied**, `tracing_on=0`, no `su`). So `adb shell perfetto` connects to `traced` but
`traced_probes` cannot enable the events → traces come back **metadata-only / empty** (782–1026 B, zero
events). Do NOT trust a perfetto capture's existence as proof it has data — query it. To actually use
this event set you need EITHER root (then enable events + read `trace_pipe`, or perfetto with access)
OR — for the per-stage binning-vs-fragment split — the **full-ADB driver-path routes** in the
`xenia-thor-adb-gpu-stage-split` skill (Mesa/Turnip freedreno perfetto counter producer, gfxreconstruct
`.gfxr` capture+replay, in-engine per-pass Vulkan timestamps): these use the debuggable app's per-app
GPU-counter access, NOT the blocked kernel ftrace, so they do not need root or a GUI. The Snapdragon/AGI
GUI is the LAST resort, not the plan. For quick triage, KGSL sysfs busy%/clock + the cvar A/B still apply.

### MEASUREMENT RULE 0 — NEVER MEASURE MOVIES (intros/FMV/cutscenes play at FULL SPEED)
**Pre-rendered video (boot logos, intro movies, in-engine cutscenes, FMV) plays back at full speed
regardless of emulation performance — it's video decode, not real-time game rendering.** A game that
"runs full speed" during its intro is NOT working at speed. So:
- **A whole-capture `VdSwap/s` average is MEANINGLESS if the window includes any movie** — the
  full-speed movie frames inflate the number (e.g. a Gears menu mis-read as "~18fps" because the 75s
  VdSwap average bundled the full-speed intro logos with the slow menu).
- **ALWAYS skip past every intro/movie first** (mash `start`/`a` — see `hid_nop_button_sequence`),
  and **read the screenshot to CONFIRM the frame is an interactive MENU or live GAMEPLAY**, not a
  video. A movie frame looks cinematic/letterboxed and keeps changing every frame even when you give
  no input; a real menu/scene is static or input-driven.
- **Measure fps only over the steady interactive scene** (VdSwap counted across a movie-free window),
  OR read `gpu_frame_us` from a `GPU draw outcomes` line on a matched, confirmed-non-movie content
  frame. Cross-check the draw composition (`prim[...]`, `rendered`, `avg_vertices`): a real 3D scene
  has many geometry draws; a movie/menu is a handful of fullscreen-quad/blit draws.
See [[skip-intro-movies-is-paramount]], [[xbox360-intro-movie-skip-testing]].

### MEASUREMENT RULE (the #1 reliability lesson)
Blue Dragon's content is a function of **guest uptime**. Relaunch A/Bs are **scene-confounded** — a
faster config races into a different cinematic frame. **Always compare configs at matched `guest_ms`
AND equal `rendered` count.** An unmatched A/B produced a fake "9× load/store" result that was
retracted. Trust profile/counter deltas only on content-matched frames + read the screenshot.

## Current root-cause verdict (as of B60, 2026-05-31)
Blue Dragon heavy 3D scene is **GPU-BOUND on the geometry / per-draw / binning / state front-end**,
PROVEN by Adreno GPU busy% = **77-79% @ 615MHz** on the live heavy scene. Content-matched harness
ruled out: tile load/store ≈3%, render-pass breaks ≈12%, **fill/resolution 0%** (2× res = 4× pixels,
no change). `gpu_frame_us` is pixel-independent and **super-linear in geometry/draw count**
(~2000 tiny draws/frame, avg ~136 verts, ~2000 descriptor binds). So the lever is **reducing per-draw/
geometry/binning GPU work**, NOT the tiler pass/load-store rewrite (that targets only ~15%) and NOT
the CPU (CPU is ~84% idle during these frames). Full chronological detail + retracted hypotheses:
`docs/worklog/2026-05-30-uma-and-cross-game-3d.md` (entries B40-B60).

Shipped wins (default-on, device-verified): bulk PM4 type-0 parse; Blue Dragon draw-wait fastpath.
Black-3D resolved (stale-config artifact). Next: instrument what the ~2000 draws are (prim types +
vertex sizes), then cut draw count / state churn / binning pressure. For the binning-vs-rendering GPU
split, use the full-ADB routes in `xenia-thor-adb-gpu-stage-split` (driver-path counters, no GUI/root).

## Working philosophy (2026-06-03) — BOLD BUILD MODE, FORWARD ONLY
- **Ship boldly; git is the safety net.** We are at ~4-6fps — there is little to "protect". Implement the
  large refactors (the roadmap R-targets), don't hand-wring about regressing a slow baseline. Every perf
  refactor is **cvar-gated default-off**, so the default path is always best-known-good; experiments
  iterate forward until they earn `default-on`. The real failure is NOT shipping, not a not-yet-working
  experiment.
- **FORWARD ONLY — never `git revert`, never go backwards.** If a change underperforms or breaks: read the
  frame (png) + logs, use `git log`/history to understand what the good state did, and **FIX IT FORWARD**
  with a new improving commit. Iterate forward until it works or the hard limit is proven + documented.
  Progress is monotonic.
- **Measure clean.** Perf with `tools/thor/thor_gpu_capture.ps1 -NoDump` (RT-dump readbacks poison timing);
  classify GPU- vs CPU-bound with `-TopProfile` (per-thread `top`). Trust GPU busy% + per-thread top, NOT
  the derived `gpu_frame_us`. **Read the png every device fire** (black/broken = fix forward immediately).

## Working rules (do not violate)
- **Never thrash the Thor** (see top rule — this is the ONE hard physical-safety rule that BOLD MODE does
  NOT relax). Thermal gate STAYS: only launch via `thor_gpu_capture.ps1` (refuses >55°C, 64°C watchdog,
  ONE launch per fire, force-stops when hot). Device-test aggressively *within* that gate.
- **Never fabricate.** Every fps/number/scene claim must come from device output you read THIS turn;
  read the screenshot before asserting a visual state. If unmeasured, say "not measured".
- **Targeted `git add` only — NEVER `git add -A`.** Commit only the specific files you changed.
  Do not commit game ISOs/keys/dumps, screenshots, or the device config backup (`_xenia.config.backup.toml`).
- Work on the current branch (`master`); commit + push each durable, proof-backed result.
- End commit messages with: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- Recover ADB drops with `adb reconnect`. Prefer one device action per step; read the result before the next.
- Skills live in `.agents/skills/` (repo-local). Worklog of record: `docs/worklog/2026-05-30-uma-and-cross-game-3d.md`.
