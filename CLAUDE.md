# CLAUDE.md — xenia-thor (Xbox 360 emulator on the AYN Thor)

## Goal
Make Xbox 360 games fast + playable on the AYN Thor (Snapdragon 8 Gen 2 / Adreno 740) via this xenia fork.
Priority: **Blue Dragon → 30fps**; then Burnout, Gears, Lost Odyssey, Banjo → 30-60. Ship every win as a
cvar-gated, stacking `XeniaOptimizations` toggle.

## ⭐ PRIMARY BUILD (authorized 2026-06-26): static-recomp LLVM backend
The dominant CPU tax = guest PPC registers round-trip through PPCContext MEMORY at every basic-block
boundary (xenia's per-block regalloc resets all host regs per block). Device-proven this session: BD's
hottest fn `guest_824694A0` pays **~1770 LOAD/STORE_CONTEXT ops**, and a kill-test
(`scratch/thor-debug/residency_killtest.c`, aarch64-gcc -O2) showed register LOCALIZATION takes a guest
hot loop from ~4 context round-trips/iteration to **ZERO**. The in-JIT residency retrofit
(`arm64_register_cache_inherit`) CRASHES (back-edge soundness; default-off, do not chase). **The clean
win = a whole-function recompiler where the host compiler's allocator keeps guest regs resident —
RPCS3 / XenonRecomp model.** User authorized 2026-06-26: "do the static recomp llvm backend", "work
24/7 max".
- **Architecture:** PPC → HIR (REUSE xenia's frontend) → **LLVM IR** (new lowering; PPCContext field
  accesses → `llvm load/store` that mem2reg/GVN promote to SSA across the whole function = residency)
  → LLVM whole-function opt + ARM64 codegen → native, **PRECOMPILED AT LOAD** (RPCS3-style; extend the
  shipped load-window precompiler, [[parallel-jit-precompiler-wall]]).
- **New backend:** `src/xenia/cpu/backend/llvm/` (model on `backend/a64/`). Default-off cvar
  `cpu_backend=llvm`; hybrid (LLVM for hot fns, a64 JIT for the rest) sharing the dispatcher.
- **Hard parts:** link libLLVM in the NDK ARM64 build (the first hurdle); precise CR/XER/FPSCR (lazy
  flags); indirect branches/jump tables (DolRecomp-style PC-switch entry); memory model (membase +
  opaque guest-mem helpers so locals stay resident); runtime/dispatcher/kernel-HLE interop; SMC fallback.
- **Validation:** the qemu-a64 differential harness ([[a64-qemu-harness]]) + RequireTransparent
  OFF==ON byte-identical, then device. Go/no-go kill-test in docs/research/20260626-static-recomp-residency-eval.md.
- This **SUPERSEDES** the per-block JIT residency grind as the BD/Gears CPU lever. Serves every
  CPU-bound title (Burnout is scene-dependent CPU-bound too; UMA is NOT the lever —
  [[uma-safe-but-not-bd-heavy-lever]]).

## Autonomous mode (standing user directive)
Pick the highest-value unit yourself and execute end-to-end (implement → build-verify → device-test →
commit → next). Don't ask which task / re-confirm direction / analysis-paralyze. A big effort is a reason
to start, not to ask. Surface only genuine external blockers. The thermal + no-fabrication rules below are
safety, not "asking" — they always hold.

## ⚠️ PLAN FROM FIRST PRINCIPLES — device A/Bs here LOOP on confounds
**Derive the structural cause from CODE + the known 360/Xenon/Adreno ARCHITECTURE + the DBT/emulation
literature FIRST. Form a falsifiable hypothesis. ONLY THEN fire the device — to CONFIRM that one
hypothesis, never to "explore."** Device experiments here have repeatedly produced a wrong number → a
wrong verdict → weeks chasing a non-lever (GPU-bound⇄CPU-bound flip-flops, "pixel-independent", "FDM is
THE lever" → FDM was device-DEAD). The real structural taxes are DERIVABLE without the device and don't
need firing to find: per-block register round-trips through PPCContext memory, CPU↔GPU fence
serialization, the global guest-atomic lock, the single-threaded guest, the EDRAM RAM-round-trip — all
code-facts + arch-mismatches. **Build the model from code/arch/literature; the device only validates the
FINAL hypothesis. The emulation gap (~5× beyond typical) is a STACK of compounding ~2× structural taxes,
not one bug — attack the stack, derived not measured.**

### Measurement is the #1 trap (for when you must measure)
**BD's GPU scene complexity changes ~4× in 500ms of guest time** (267 draws @ guest_ms 14007 → 1126 @
14516, device-measured 2026-06-25). Consequences, learned the hard way:
- **Cross-fire / relaunch A/Bs are WORTHLESS.** Two separate launches reach different guest_ms = different
  scenes = invalid comparison. EVERY "config X = N fps vs baseline M" from separate runs is confounded —
  including the historical "BD GPU-bound on binning", "pixel-independent / 480p inert", "thinning 1.7×",
  "VRS −22%", "fence-fix +46%". **Treat all such numbers as UNVALIDATED until re-proven the rigorous way.**
- **`gpu_frame_us` is unreliable** — a SPAN that includes GPU-idle time (RT-dump readbacks also poison it).
  Never reason from its absolute value. KGSL `gpu_busy_percentage` (busy-vs-idle) is the trustworthy signal.
- **The ONLY rigorous perf A/B = single-run, IN-PLACE ALTERNATING:** freeze ONE frame, flip the cvar in
  ~16-frame windows within that one run, compare the per-phase delta. Infra: `gpu_freeze_ab_alternate_vrs`
  (vulkan_command_processor.cc ~2280). Mirror it to test any new lever.
- **Confirm the frozen frame is GPU-BUSY** (KGSL busy% high) before trusting a delta — a frozen LIGHT scene
  is GPU-idle (busy ~2%) and its numbers are meaningless. Freezing a precise guest_ms is unreliable (boot
  speed varies) — prefer the alternator's "free-running past guest_ms" trigger.
- **RULE 0: never measure movies/intros** (full-speed video decode inflates fps). Confirm via screenshot
  it's live gameplay, not a cutscene/menu (a movie keeps changing with no input; many geometry draws = real).
- **VdSwap/s** = real user-visible fps, but only meaningful at a MATCHED scene (cross-fire can't give that).
- **Until a lever is confirmed by a single-run alternating A/B on a busy frame, it is UNVALIDATED — say so;
  do not claim a fps number.** Reliable evidence: code facts (file:line), host cpu-tests (byte-identical),
  qemu-a64, single-run alternating deltas, pixel-correctness (screenshot), commit hashes / what shipped.

### ⚠️⚠️ TEST WITH ALL OPTIMIZATIONS ON — the silent-default-off confound (learned the hard way 2026-06-25)
The single source of truth for shipped optimizations is **`XeniaOptimizations.java`** (global, auto-applied by
the app UI launch) + **`GameProfiles.java`** (per-game overrides). **`am start --ez` test launches BYPASS BOTH**
— they set ONLY the cvars you explicitly pass. So any device test that does not pass the FULL validated stack
is CONFOUNDED: it silently runs WITHOUT flat_membase, the rlwinm/CR/vector fast-paths, `vulkan_gate_rt_update`
(+34% Burnout), `opt_prime_core_router` (+25%), `vulkan_lazy_completion_polls` (the Turnip fence fix, +46-78%),
constants-arena, etc. This invalidated a whole session of "Burnout 8.3 / BD 9.9 / GPU-paced" measurements; with
the real stack on, the Burnout RACE hits **46.2 fps** (device-validated 2026-06-25, VdSwap 462/10s, 0 faults) —
it was reported 8.3, then a STILL-partial 14.83 that was itself missing `gate_rt_update` + the GPU stack. The lesson
compounded: even a "stack" measurement is confounded unless it is the COMPLETE validated set. BD heavy field 7.9→11.67.
- **Effective-value layering (highest wins):** app launch Bundle [`XeniaOptimizations` global → `GameProfiles`
  per-game → user `--ez`] **beats** the persisted `files/xenia.config.toml` **beats** the compiled `DEFINE_bool`
  default. ONLY the launch Bundle (intent extras / `--ez`) beats the device config — the "phantom config" gotcha.
- **A cvar's compiled default can silently disagree with the registry's intended-on** (the fence fix was
  `DEFINE_bool(...,false,...)` AND pinned `=false` in the device config). When a validated win seems absent,
  check BOTH the `DEFINE_bool` default AND `run-as <pkg> grep <cvar> files/xenia.config.toml`.
- **THE TEST STACK:** every device A/B must `--ez` the full validated set — reuse the `$opts` array in
  `scratch/thor-debug/measure_const_promo.ps1`. Bisect off that, don't build up from nothing.
- **VALIDATED-SETTING PROPAGATION — do ALL of these EVERY time a game's best settings are found.** This is the
  only way the device actually gets the win; skipping step 3 is exactly what shipped a 10fps Burnout to the UI
  while `--ez` tests read 46:
  1. **Per-game win → add the cvar to that title's `GameProfiles` profile** (persists + beats the config, with the
     device-validated "why"). **Cross-game win → flip `XeniaOptimizations` `defaultEnabled=true`.**
  2. **Frame cap per game = the title's NATIVE rate** via `gpu_frame_limit_fps` in its profile (BD / Gears / Lost
     Odyssey / Banjo = 30, Burnout Revenge = 60). **Resolution per game** via `kernel_display_resolution` (BD = 720p
     — its heavy field is CPU/lock-bound so resolution is near-free; sharper image at ~same fps).
  3. **REBUILD the APK and REINSTALL it.** The registry + profiles are Java — they DO NOT reach the device until the
     APK is repackaged and installed. A stale APK silently runs the OLD defaults = the silent-default-off confound,
     ON the device. (Root cause of the 10fps Burnout: the installed APK predated the default-on + fence-fix commits.)
  4. **VERIFY from the in-app UI launch (NOT `--ez`):** pick the game in-app and confirm it reproduces the measured
     fps. If menus run uncapped (hundreds of fps) the registry/profile is NOT being applied → the install is stale.
- **BUILD GOTCHA — the repo path has a space ("New project 8")** so ndk-build's `$(wildcard)` fails the native
  configure ("unknown file"). The documented `subst X:` is NOT visible to background gradle tasks; use a directory
  JUNCTION instead: `cmd /c mklink /J C:\xt "<repo>"`, then build from `C:\xt\android\android_studio_project`.
  `mergeResources` is flaky with `!directory.isDirectory` on a fresh model — run `:app:mergeGithubDebugResources`
  once in isolation to prime it, then `assembleGithubDebug`. (You do NOT need to recompile the native `.so` for a
  Java-only registry/profile change — but a stale `.cc` makes gradle try, so the junction path is required anyway.)
- **ALWAYS MEASURE temp before assuming thermal saturation** — the Thor cools to ~38°C within ~90s of idle; never
  say "thermally blocked" without reading `/sys/class/kgsl/kgsl-3d0/temp`.
- **`screencap` can grab the SECONDARY display** (post-reboot the Thor shows the launcher on a 2nd display, so a
  screenshot looks like the home screen while the game renders fine on the primary). Confirm rendering via the
  `GPU draw outcomes/frame` logcat line + `dumpsys activity activities | grep topResumedActivity`, not the shot alone.

## ⚠️ Never thrash the Thor (hard safety rule — heavy firing crashed it once)
Before ANY launch read `/sys/class/kgsl/kgsl-3d0/temp` (milli-°C) + `gpu_busy_percentage`; launch only if
temp < 50-55°C and busy low. ONE launch per cooldown; force-stop `jp.xenia.emulator.github.debug` when temp
climbs past ~70°C. The device degrades under heavy firing (boot stalls after ~6 launches) — batch fixes,
build once, fire once, fill cooldowns with device-free work. The BUILD is the primary verification.

## Device
AYN Thor, ADB `c3ca0370`. Snapdragon 8 Gen 2 (QCS8550), Android 13, ~16GB UMA. ADB at
`C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe`. Package `jp.xenia.emulator.github.debug`.
- **CPU:** 8× ARMv9.0-A heterogeneous — 1× Cortex-X3 @3.19GHz (prime, cpu7), 4× A715/A710 @2.8GHz, 3× A510
  @2.0GHz. NEON is the ONLY SIMD (no SVE). asimddp/i8mm/bf16 = int8/bf16 matrix (heuristics only, NEVER
  guest FP32 geometry — it black-screens, [[approx-math-guest-visible-vs-heuristics]]); fphp/asimdhp = FP16
  (pixel-shader only); flagm (NZCV flags), atomics (LSE locks), rcpc.
- **GPU:** Adreno 740v2, TBDR, Vulkan 1.3, **Turnip (Mesa)** = the fast+correct driver path. On-chip GMEM =
  the EDRAM-emulation target. SINGLE graphics+compute queue. LRZ early-Z (defeated by alpha-test/discard).
  EDRAM resolves are per-pixel COMPUTE dispatches. Turnip exposes ~80 extensions, most unused (audit doc
  docs/research/20260620-adreno-turnip-feature-gap-audit.md). Absent: fragment_shader_interlock,
  external_memory_host, mesh shaders.

## Build / deploy / run
- Path has spaces → `subst X: "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor"` for builds.
- APK (foreground; bg tasks don't see X:): `cmd /c "X:\android\android_studio_project\gradlew.bat -p
  X:\android\android_studio_project :app:assembleGithubDebug"` (native ~1-2.5min). APK:
  `android/android_studio_project/app/build/outputs/apk/github/debug/app-github-debug.apk`. Install:
  `adb -s c3ca0370 install -r <APK>`.
- BD launch: ISO `/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`,
  turnip driver flags, `--es hid nop --es hid_nop_button_sequence '<start/a sequence>'`; heavy field
  ~120-135s after launch. Reusable scripts in `scratch/thor-debug/*.ps1`.
- Trace cvar: `--ez vulkan_trace_draw_outcomes_per_frame true` → the `GPU draw outcomes/frame` logcat line
  (rendered, gpu_frame_us [unreliable], guest_ms, cpu_* buckets, fopen[], draw composition).

## Optimization framework
Every win = a cvar-gated engine flag + a `XeniaOptimizations` registry entry (auto-appears in the in-app
Settings UI, auto-wires the cvar) + the cvar **allowlisted in `EmulatorActivity.java`** (copyBooleanExtra/
copyIntExtra/copyStringExtra — REQUIRED or `--ez/--ei/--es` silently no-op). Default-off until validated;
per-game defaults via the GameProfile system. Two tracks: CPU (codegen/JIT/locks — CPU-bound titles) + GPU
(overdraw/draws/EDRAM — GPU-bound scenes). The BD bottleneck is currently UNRESOLVED (the prior verdicts
were confounded — re-measure with single-run alternating before building a lever on it).

## Game patches
`.patch.toml` (src/xenia/patcher/, in xenia-core): be8/16/32/64 writes into guest memory, matched by title_id
+ build hash ("Module Hash" in logcat). Applied in `KernelState::LoadUserModule` after Dump() and BEFORE
execution → patches take effect (JIT compiles lazily, post-patch). Author via the `xenia-thor-ghidra-game-patch`
skill. Banjo dirty-disc = a guest-side false verification (deep RE; current patch sites are wrong-target).

## Device-free testing (prefer over firing)
- **Host x64 cpu-tests** (backend-independent HIR/kernel/VFS): `MSBuild build\xenia-cpu-tests.vcxproj
  /p:Configuration="Debug Windows" /p:Platform=x64`; run `build/bin/Windows/Debug/xenia-cpu-tests.exe "FILTER_*"`
  (Catch2). Add a test = `NAME_test.cc` + a ClCompile line in the gitignored vcxproj; commit only the .cc.
- **qemu-a64** (real ARM64 backend, no device): WSL + aarch64-linux-gnu-g++ + qemu-aarch64; `make -C build
  config=debug_linux xenia-cpu-tests CXX=aarch64-linux-gnu-g++`. Recipe: [[a64-qemu-harness]].
- Mine existing captures (log-grep) instead of re-firing where possible.

## Guest RE + CPU hot-spot tooling (the BD CPU is guest-game-logic-bound)
Confound-free simpleperf at the BD field (2026-06-25, 480p): guest-JIT **48%** (the guest main
XThread running game-logic = **67%** of samples), CP worker 16.8%, kernel 7.4%, libc 5.4%. The
guest-JIT is a **hot CLUSTER, not flat** — top: `guest_8273EF74`+`8273EF84` (7.6% of ALL cpu),
`826EE7C0`+`826EE728` (4.0%), the `8270B1F8/2D8/298` cluster (3.9%), `826FF288`+`298` (2.2%). So
the BD CPU lever = RE/accelerate BD's hottest guest code (codegen quality on a 48% chunk), not the CP.
- **Find hot guest fns (confound-free, the method that works):** `simpleperf record --app <pkg> -g
  --call-graph fp -f 1000` at a confirmed field; pull `jit.map` (`cpu_perf_map_path` →
  `files/jit.map`); bin the `unknown[+host]` samples by guest fn via the map
  (`scratch/thor-debug/_bin.py`). Symbolize host C++ offline w/ the unstripped `.so` +
  `llvm-symbolizer`. %-of-samples is reliable; `gpu_frame_us` is NOT.
- **Dump a hot fn's codegen (PPC + RawHIR + OptHIR + a64 machine code) straight from the JIT:** launch
  with `--es disassemble_function_filter "8273EF74,826EE7C0-826EE800"` (allowlisted; single addrs or
  `start-end` ranges) → grep logcat `Filtered function dump`. Shows EXACTLY how the JIT lowers it
  (register round-trips, flag stores) — the codegen-inefficiency, directly. `scratch/thor-debug/dump_hot_disasm.ps1`.
- **Semantic RE (device-free):** Ghidra **12.0.4_PUBLIC** at
  `C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC`
  (`support\analyzeHeadless.bat`; JDK `…\toolchains\jdk-21.0.11+10`). Scripts `scratch/ghidra/scripts/*.java`;
  projects `scratch/ghidra/proj/` (banjo, sylpheed — **BD NOT imported yet**, import `scratch/blue-dragon/default.xex`
  via XEXLoaderWV). Run headless in the MAIN loop w/ LONG timeout (600000), never in a workflow subagent —
  see the `xenia-ghidra-ooda-loop` + `xenia-thor-ghidra-game-patch` skills.

## Config + git rules
- Device persists `files/xenia.config.toml` which OVERRIDES compiled cvar defaults (only `--ez/--ei/--es`
  beat it). When a default seems ignored, clear it. New cvars MUST be allowlisted in EmulatorActivity.java.
- **Never fabricate** — every number from device output read THIS turn; read the screenshot before asserting
  a visual; if unmeasured say "not measured"; if confounded say "unvalidated".
- **Targeted `git add` only — never `-A`.** Never commit ISOs/keys/dumps/screenshots/config backups. Work on
  `master`; commit + push durable results. End commit messages: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- **Forward-only** — never `git revert`; fix forward. Skills in `.agents/skills/`; knowledge in the memory
  files (MEMORY.md index); consult Codex/Gemini for hard rearch ([[consult-hard]]).
