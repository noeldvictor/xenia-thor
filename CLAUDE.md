# CLAUDE.md — xenia-thor (Xbox 360 emulator on the AYN Thor)

## Goal
Make Xbox 360 games fast + playable on the AYN Thor (Snapdragon 8 Gen 2 / Adreno 740) via this xenia fork.
Priority: **Blue Dragon → 30fps**; then Burnout, Gears, Lost Odyssey, Banjo → 30-60. Ship every win as a
cvar-gated, stacking `XeniaOptimizations` toggle.

## Autonomous mode (standing user directive)
Pick the highest-value unit yourself and execute end-to-end (implement → build-verify → device-test →
commit → next). Don't ask which task / re-confirm direction / analysis-paralyze. A big effort is a reason
to start, not to ask. Surface only genuine external blockers. The thermal + no-fabrication rules below are
safety, not "asking" — they always hold.

## ⚠️ MEASUREMENT IS THE #1 TRAP — most past A/Bs here were CONFOUNDED (garbage)
**Device-measured 2026-06-25: BD's GPU scene complexity changes ~4× in 500ms of guest time** (267 draws @
guest_ms 14007 → 1126 @ 14516). Consequences, learned the hard way:
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

## Config + git rules
- Device persists `files/xenia.config.toml` which OVERRIDES compiled cvar defaults (only `--ez/--ei/--es`
  beat it). When a default seems ignored, clear it. New cvars MUST be allowlisted in EmulatorActivity.java.
- **Never fabricate** — every number from device output read THIS turn; read the screenshot before asserting
  a visual; if unmeasured say "not measured"; if confounded say "unvalidated".
- **Targeted `git add` only — never `-A`.** Never commit ISOs/keys/dumps/screenshots/config backups. Work on
  `master`; commit + push durable results. End commit messages: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- **Forward-only** — never `git revert`; fix forward. Skills in `.agents/skills/`; knowledge in the memory
  files (MEMORY.md index); consult Codex/Gemini for hard rearch ([[consult-hard]]).
