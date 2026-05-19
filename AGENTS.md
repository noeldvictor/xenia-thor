# Agent Instructions

## Mission

Port this Xenia fork to native Android ARM64 for the AYN Thor Max while keeping performance work visible from the first day.

This repository is a local research experiment, not official Xenia. Do not send
fork-specific crashes, compatibility claims, RenderDoc traces, or half-built
ARM64 backend questions to upstream Xenia, the Xenia Discord, or unrelated
open-source maintainers unless the problem is independently reproduced on an
official upstream build.

## Communication Style

- Be concise and direct. Prefer short, concrete status, findings, and next
  actions over broad narration.
- Avoid AI-slop language: no filler praise, vague hype, fake certainty,
  inflated metaphors, or generic "game changer" phrasing.
- Write like an engineer leaving useful notes for the next run: what changed,
  what proved it, what is still unknown, and what to try next.
- Keep speculation clearly labeled, and tie technical claims to logs, captures,
  source references, or dated research notes.

## Branch And Commit Rules

- Work on `master` only unless the user explicitly changes this policy.
- Do not create `codex/` feature branches for this repo while this rule is in effect.
- Commit and push often after validated Android UI, native ARM64/Vulkan core,
  debug automation, and device capture slices.
- Keep each commit tied to a dated worklog entry. Research-driven changes must
  also have a dated research note.
- New Codex skills for this project must be repo-local under `.agents/skills/`.
  Do not create or update global skills for xenia-thor unless the user explicitly
  reverses this rule.

Primary target:

- Device: AYN Thor Max.
- OS: Android native, not Termux or a Windows compatibility layer.
- ABI: `arm64-v8a` first. Keep `x86_64` only when it helps desktop Android emulator or regression comparison.
- SoC: Snapdragon 8 Gen 2 / Adreno 740.
- Graphics path: Vulkan first.
- CPU path: PowerPC guest code must eventually run through an AArch64 host backend, not the existing x64 backend.
- User milestone: Vulkan game running on Thor Max, with Blue Dragon as the likely first legally owned test title.
- First known legal Blue Dragon target on the Thor SD card:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`.
- Priority is ARM64 on AYN Thor Max. Windows/x64 is useful as a control or
  semantics reference, but do not default to Windows-first workflows unless the
  exact question needs it.

## Thor Max Translation Model

Use this mental model when planning work: this fork is not trying to be a
cycle-accurate Xbox 360 hardware clone on Android. The practical path is
translation plus HLE:

1. Xbox 360 PowerPC guest code -> AArch64 JIT/DBT code cache on Snapdragon.
2. Xenos GPU packets, shaders, resolves, and EDRAM behavior -> Vulkan/SPIR-V
   work on Adreno 740.
3. Xbox kernel, XAM, files, input, audio, timers, threads, events, and devices
   -> high-level emulation with correctness probes where games depend on exact
   behavior.

Thor Max is a good research target because it has Snapdragon 8 Gen 2-class
CPU cores, Adreno 740, Vulkan 1.3 support, 16 GB RAM in the Max models, active
cooling, Android developer access, microSD content storage, and real handheld
controls. Treat the hardware headroom as real but not automatically fungible:
raw CPU/GPU/RAM ratios can be far above Xbox 360-era hardware, but the effective
speed depends on A64 codegen quality, endian-aware memory lowering, VMX128/FP
lowering, guest/host transition cost, kernel timing, Xenos-to-Vulkan translation,
shader compilation, EDRAM/resolve emulation, synchronization, and Adreno driver
behavior.

Use "about 20x more powerful" as an intuition for why this is a plausible
translation-based research target, not as a performance promise. Validate every
claim with Thor speed captures, screenshots, APK hashes, cvars, and worklog
notes. Prefer translation/HLE hot-path fixes over exact-cycle hardware modeling
unless a concrete game-correctness bug proves that the lower-level detail is
required.

## Hard Documentation Rules

- Every research note must be a Markdown file in `docs/research/` with this filename shape:
  `YYYYMMDD-HHMMSS-topic.md`.
- Every work session must update a Markdown worklog in `docs/worklogs/` with this filename shape:
  `YYYYMMDD.md`.
- Worklogs must include start/end timestamps, branch name, commands run, files changed, findings, blockers, and next actions.
- Do not leave important research only in chat. Put sources, dates, confidence, and decisions in a dated research file.
- When a source may have changed recently, re-check it and include the access date in the research note.
- Clearly mark this fork as an extremely unstable research-only experiment in user-facing docs.
- Do not imply official Xenia support or compatibility.

## Repo Facts As Of 2026-05-17

- Origin is SSH: `git@github.com:noeldvictor/xenia-thor.git`.
- The Android Studio project is under `android/android_studio_project`.
- Android Gradle config currently uses:
  - `compileSdkVersion 33`
  - `targetSdkVersion 33`
  - `minSdkVersion 24`
  - `ndkVersion '25.0.8775105'`
  - `abiFilters 'arm64-v8a', 'x86_64'`
- Premake defines Android platforms `Android-ARM64` and `Android-x86_64`.
- The production desktop CPU backend remains `src/xenia/cpu/backend/x64`.
- The Android ARM64 CPU path is now a hard aX360e/Edge-style A64 backend
  import under `src/xenia/cpu/backend/a64`, exposed through the
  `src/xenia/cpu/backend/arm64` compatibility wrapper.
- The previous helper-backed ARM64 mini-JIT scaffold was removed on
  2026-05-18. Do not spend new work on mini-JIT helper patches unless the user
  explicitly asks to resurrect that path for comparison.
- `src/xenia/emulator.cc` can select the ARM64 scaffold on `XE_ARCH_ARM64` or via `cpu=arm64` launch arguments.
- `src/xenia/cpu/cpu_flags.cc` advertises `any`, `x64`, and `arm64`.
- `src/xenia/app/premake5.lua` includes `xenia_main.cc` for Android and skips the HID demo in the Android single-library bundle.
- Android currently has Java/native shell pieces for windowed apps, a launcher, a game activity, a window demo, and the Vulkan trace viewer.
- On 2026-05-17, `app-github-debug.apk` installed on ADB device `c3ca0370` and `WindowDemoActivity` rendered the Vulkan ImGui demo on Adreno 740.
- On 2026-05-17, the launcher showed `LAUNCH GAME`, `GPU TRACE VIEWER`, and `WINDOW DEMO`; launching the emulator path reached the intended ARM64 JIT-not-implemented blocker rather than a Java/native crash.
- On 2026-05-17, Blue Dragon Disc 1 launched into `EmulatorActivity` and executed guest XThreads through the ARM64 HIR interpreter scaffold. It is not playable or visibly rendering game frames; the current wall is interpreter speed and missing AArch64 JIT/emitter work.
- Use `tools/thor/thor_build.ps1` to split build lanes:
  - `FullApk`: native core plus APK packaging.
  - `NativeCore`: C++/Vulkan/CPU backend library only.
  - `ApkShell`: Java/XML/resources/manifest package only, using the previous native library.
  - Nonzero Gradle or ADB exits must stop the script; do not trust an install
    after a failed native build.
- Use `tools/thor/thor_xenia_debug.ps1` for repeatable ADB install, launch, and capture.
- `tools/thor/thor_xenia_debug.ps1` now retries flaky ADB transports for the
  known Thor serial and records reconnect events in capture metadata.
- `tools/thor/thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture` launches
  Blue Dragon, keeps live logcat open during the run, writes filtered logcat,
  metadata, and a PNG screenshot, and is preferred over dump-afterward logcat
  for early GPU/presenter probes.
- `tools/thor/thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture` is the
  preferred speed lane. It runs Blue Dragon with the known nop HID sequence,
  disables trace-heavy flags, uses error-only Xenia logging, avoids live logcat,
  writes timed perf snapshots, then captures logcat, metadata, and a screenshot.
- As of 2026-05-18, Blue Dragon's VdSwap frontbuffers `1CA1C000` and
  `1CDB4000` are still all-zero on Thor, but forced-presenting resolve
  candidate `1C340000` as 640x360 format 7 produces a visible dark-blue guest
  surface. This is not title, gameplay, or compatibility; use it only as a
  debug checkpoint for render-source selection.
- Use `tools/arm64/hir_coverage_report.ps1` to summarize latest Thor logcats
  into HIR opcode counts, unimplemented opcodes, legacy mini-JIT fallback
  reasons from older captures, slow interpreter functions, guest crash PCs,
  ARM64 guest-store watch hits, and PPC global-reference scanner hits.
- Use `tools/arm64/arm64_jit_gap_report.ps1` to compare HIR opcodes against
  the legacy ARM64 mini-JIT and interpreter switch coverage when reviewing
  pre-swap captures, then attach recent Thor log watchdog/fallback signals to a
  dated Markdown report.
- Use `tools/arm64/arm64_conversion_audit.ps1` before broad ARM64 backend work
  or after suspicious Thor runs. It summarizes x64-vs-ARM64 backend services,
  HIR switch coverage, legacy helper-heavy mini-JIT surface, reject/error
  signals, and recent Thor evidence into a dated Markdown report.
- Use `tools/thor/ghidra_headless_import.ps1` for repeatable Ghidra headless imports. Local Ghidra was found at:
  `C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC`.
  The script auto-detects that path, but `GHIDRA_HOME`, `-GhidraHome`, and
  `-AnalyzeHeadless` still override it.
- Use `tools/thor/thor_renderdoc.ps1` for Android Vulkan layer setup, RenderDoc status, cleanup, and capture pulling.
- Current translation strategy digest:
  `docs/research/20260519-153016-xbox360-thormax-translation-report.md`.
  It says Blue Dragon performance work should treat CPU/A64 dispatch, helper
  fallbacks, XMA/audio cost, and debug overhead as the first wall; GPU/Adreno
  deep dives should stay narrow until evidence points back to rendering.

## Current Porting Priorities

1. Establish reproducible Android build generation.
   - Initialize submodules before build work:
     `git submodule update --init --recursive`.
   - Generate Android makefiles with:
     `python xenia-build setup --target_os=android` or `python xenia-build premake --target_os=android`.
   - Build from `android/android_studio_project` with Gradle after generated makefiles exist.
   - If the repo path contains spaces, run Gradle through a no-space `subst` drive because `ndk-build` may reject `APP_BUILD_SCRIPT` paths with spaces.
   - Install and smoke-test on Thor with:
     `powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode Install`
     and then `-Mode LaunchWindowDemo` or `-Mode LaunchLauncher`.

2. Make Android ARM64 build failures explicit.
   - Keep x64 backend code behind `XE_ARCH_AMD64` and architecture filters.
   - Keep the imported A64 backend honest: it now compiles as an Android native
     backend, but it is still non-playable research until device logs prove a
     retail game reaches title.
   - Do not fake guest execution by silently falling back to `NullBackend` for the emulator path.

3. Bring up the native Android emulator app.
   - Keep `xenia_main.cc` wired into the Android shared library.
   - Maintain the document/file selection flow for `.xex`, `.iso`, and direct developer ADB target extras.
   - Keep the existing Vulkan trace viewer and window demo working as smoke tests.

4. Build the AArch64 CPU backend.
   - Start from the shared backend interfaces in `src/xenia/cpu/backend`.
   - Mirror x64 backend structure only where it maps cleanly.
   - Decide on an AArch64 code emitter library through a dated research note before vendoring anything.
   - Validate instruction cache flushing, executable memory policy, signal/exception handling, and breakpoint behavior on Android.
   - Treat the target design as PPC/HIR to AArch64 DBT with code cache, direct
     block chaining, fast indirect branch lookup, pinned guest context and
     memory base, endian-aware memory lowering, and VMX128-to-NEON lowering.
   - Measure helper fallbacks, guest-to-host transitions, thunk entries,
     code-cache lookups, direct links, indirect branch misses, recompiles, and
     top guest PCs before guessing at hot fixes.
   - Do not judge speed with broad debug tracing, disassembly dumps, shader
     dumps, or compiled-call tracing enabled.
   - Do not treat VMX128 as "just NEON"; lane order, saturation, NaNs,
     denormals, and Xenos packed formats are correctness traps.

5. Optimize for Thor Max early.
   - Treat Adreno 740 / Vulkan driver behavior as a real target, not an afterthought.
   - Track CPU, GPU, thermal, fan, battery, and frame pacing observations in worklogs.
   - Preserve correctness before speed; every speed experiment needs a comparable baseline.
   - Keep trace-heavy correctness runs and trace-off speed runs separate. Mixing
     them gives bad answers.

## Translation Strategy From 2026-05-19 Report

- Highest-value current order:
  1. Keep the quiet Blue Dragon speed lane canonical, with strict metadata.
  2. Add A64 block dispatch, helper fallback, transition, and code-cache
     profiling that is cheap enough to leave on in speed captures.
  3. Keep XMA/audio shortcuts as explicit bring-up probes, not correctness
     fixes.
  4. Narrow the signed `2_10_10_10` / Adreno format fallback work only where
     Blue Dragon evidence points.
  5. Add real Android input mapping and repeatable menu/opening scripts.
- CPU work should improve the DBT spine before title-specific hacks:
  direct block chaining, indirect branch lookup, endian load/store fusion,
  common branch/condition lowering, VMX128 inline lowering, and reduced
  guest-to-host transitions.
- GPU work should be command-stream, shader-microcode, EDRAM/resolve, and
  format-specific. Do not rely on optional Vulkan formats without probing
  Adreno support, and do not treat EDRAM as normal host images.
- System work should keep kernel/XAM/thread/timer/storage/input/audio HLE
  visible and measurable because Blue Dragon can be CPU-ready while blocked by
  device-service behavior.
- Hot trace stitching, background optimization, pipeline-cache tuning, AGI, and
  RenderDoc are useful later, but the report ranks them below CPU/A64 and audio
  cost until the speed lane shows those are no longer the dominant wall.

## Debug Automation Rules

- Before a risky cvar, backend shortcut, GPU hack, timing tweak, or
  title-specific probe, run the repo experiment gate:
  `.agents/skills/xenia-thor-experiment-gate/SKILL.md`.
- Keep APK shell rebuilds and native core rebuilds separate:
  - UI/manifest/resource changes: `thor_build.ps1 -Mode ApkShell`.
  - C++/Vulkan/ARM64 backend changes: `thor_build.ps1 -Mode NativeCore`.
  - Release-to-device validation: `thor_build.ps1 -Mode FullDeploy`.
- Prefer scripted Thor debug loops over manual clicking once a path is known:
  `FindContent`, `LaunchBlueDragon`, `LaunchBlueDragonLiveCapture`,
  `LaunchBlueDragonSpeedCapture`, `LaunchEmulator`, `LaunchWindowDemo`, and
  `Capture`.
- Always clear logcat before a launch and capture a full log, filtered log, screenshot, metadata file, APK hash, branch, commit, process id, focused activity, and target path.
- For every speed note, record build hash, APK hash, cvars, run duration,
  thermal/fan/battery if available, screenshot path, process id, thread sample,
  and whether the run was trace-off or trace-heavy.
- Do not enable broad shader dumps, checksums, live logcat, broad GPU packet
  traces, disassembly dumps, or compiled-call tracing in the speed lane unless
  the research note explicitly says the speed result is invalid for comparison.
- Legacy ARM64 mini-JIT flags (`-Arm64MiniJit`, `-Arm64MiniJitBlacklist`,
  `-Arm64ForceInterpreterRanges`, and `-Arm64GuestStoreWatch`) belong to the
  removed scaffold path. Do not rely on them for current aX360e A64 backend
  runs.
- For current A64 backend bring-up, prefer:
  - `-MmapAddressHigh 8` for the aX360e-style fixed high memory/code-cache
    layout on Thor.
  - `a64_max_stackpoints` and
    `a64_enable_host_guest_stack_synchronization` only when debugging the
    imported A64 stackpoint path.
- For focused guest-code and wait debugging, use:
  - `-DisassembleFunctionFilter "8246DBB0,8246B408"` with
    `-DisassembleFunctions true` to dump only matching guest functions.
  - `-Arm64CompiledCallTraceFunctions "8246B408"` to sample a hot compiled
    guest function.
  - `-Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 16`
    to enable the low-noise A64 speed profiler in a speed capture. This emits
    periodic summaries for function-entry deltas, direct/indirect guest calls,
    guest-to-host transitions, runtime resolves, and top guest PCs. It is
    default-off and preferred before noisy compiled-call tracing.
  - `-A64InlinePpcThreadFieldLeafHelpers false` to roll back the A64 inline
    for tiny PPC leaf helpers matching `lwz r11,D(r13); lwz r3,D(r11); blr`.
    Blue Dragon's hot draw-wait route calls one of these helpers at
    `8248B040`; keep the inline pattern-based rather than address-only.
  - `-Arm64BlueDragonDrawWaitProbe true` to log the current Blue Dragon draw
    wait state.
  - `-XboxkrnlThreadWaitTrace true` and `-XboxkrnlEventTrace true` for kernel
    wait/event proof.
  - `-ClearMemoryPageState true` for Canary-style GPU shared-memory page-state
    experiments.
  - `-LogLevel 0` for speed captures where only Xenia errors should hit
    logcat; restore higher logging only for focused trace runs.
  - `-XmaTraceContextState true`, `-GpuTraceTextureCacheActions true`, and
    `-VulkanTracePipelineCreation true` only when those noisy subsystems are
    the target of the run.
  - `-XmaFastSilence true` is a bring-up-only audio shortcut. It may help
    compare XMA cost, but it is not an audio correctness path or compatibility
    fix.
  - `-GpuInterruptOnRingIdle true` and `-GpuBlueDragonKickWaitToken true` only
    for clearly documented Blue Dragon research runs; both are default-off
    hacks, not compatibility fixes.
  - `-GpuTraceInterrupts true` to trace `VdSetGraphicsInterruptCallback`,
    `PM4_INTERRUPT`, VBlank, source-1 dispatch begin/end, and Blue Dragon token
    memory without forcing a compatibility behavior.
  - `-GpuInterruptOnSwap true` only as a narrow research experiment that
    dispatches source-1 after `PM4_XE_SWAP`; it is safer to test than the broad
    ring-idle interrupt, but still not a compatibility fix.
  - `-TimeScalar 0.25` or similar only as a temporary watchdog/timing probe; it
    slows guest time so the Thor can reveal whether Blue Dragon is failing due
    to slow first-frame GPU/CPU progress rather than a missing completion event.
  - `-GpuTraceSwap true` to trace guest video setup, runtime `VdSwap`, PM4
    `XE_SWAP`, and Vulkan `IssueSwap` proof. Import/symbol lines mentioning
    `VdSwap` are not runtime call proof; use the explicit `GPU swap trace:`
    lines for runtime evidence.
  - `-VulkanPresentForcedResolveOnSwap true` plus
    `-VulkanPresentForcedResolveAddress`, width, height, pitch, and format to
    force-present a specific resolve source. This is a research-only probe for
    Blue Dragon black/blue frames, not a compatibility fix.
- `-VulkanPresentScoredResolveOnSwap true` to use slow resolve readback stats
  for picking a non-clear present candidate. Pair with `-TimeScalar 0.5` and
  keep `-VulkanPresentScoredResolveBudget` small enough for a Thor run.
  Candidate `size` is visible surface size and `pitch` is memory pitch; Blue
  Dragon has an 80x45 resolve with pitch 96, so do not collapse those fields.
- For Blue Dragon black-screen runs, first prove the split:
  `-GpuTraceSwapFrontbufferChecksum true -VulkanTraceCopyState true
  -VulkanTraceResolveChecksum true`. If `1CA1C000` / `1CDB4000` frontbuffers
  are zero while `IssueCopy` emits nonzero resolves, focus on resolve content,
  format, and render-target causality rather than Android presenter bring-up.
- To inspect final fullscreen vertex data without burning the trace budget on
  setup draws, use:
  `-VulkanTraceVertexFetchChecksum true -VulkanTraceVertexFetchShaderFilter 5CF1EE2E3C82546B -VulkanTraceVertexFetchChecksumBudget 24`.
  This is a slow research-only trace. The 2026-05-19 Blue Dragon run ruled out
  bad final-blit vertex position, UV, or color data as the immediate black
  frame cause.
- For upstream Blue Dragon render-chain probes, use shader-hash filters so
  draw-state, constants, and texture-source budgets land on the suspected
  passes:
  `-VulkanTraceDrawShaderFilter`, `-VulkanTraceShaderConstantsShaderFilter`,
  and `-VulkanTraceTextureSourceShaderFilter`. Current useful pixel hashes are
  `05775DE8A2B0B3F5`, `0ABADD9DA4373CBA`, `2A0674C564A8A8C5`,
  `57B736C8B5D4E953`, `B02CC5F55AD0D140`, and `9567C79307ACC6F5`.
- Known diagnostic forced-present probe: `1D88F000+00385000`, `720x720`, pitch
  `1280`, format `6` can turn the surface white on Thor. It is proof the
  presenter can show resolve memory, not proof the game reached title.
- Use `StopNoise` before game runs if another emulator or graphics app is stealing focus or polluting logcat.
- Use the default Blue Dragon path only for the user's local Thor SD card. Do not assume other machines or devices have the same mount UUID.
- Keep Blue Dragon attempts honest: until ARM64 JIT exists, guest code may execute slowly in the interpreter scaffold, but the expected result is still not a playable game.
- For native Android crashes, analyze the unstripped `arm64-v8a/libxenia-app.so` from `android/android_studio_project/app/build/intermediates/ndkBuild/githubDebug/obj/local/arm64-v8a/`.
- For guest-code static analysis, use Ghidra only on legally owned and locally extracted/decrypted code. Do not commit game binaries, extracted XEX files, ISO contents, keys, or private screenshots.
- Keep Ghidra projects, extracted guest code, generated JIT blobs, and temporary
  analysis products under ignored `scratch\ghidra\`:
  `native`, `guest`, `generated-jit`, `reports`, and `scripts`.
- Ghidra tracks:
  - Native ARM64: `libxenia-app.so`, JNI bridge, Vulkan path, ARM64 backend, crashes, and symbolized stack traces.
  - Guest PPC: Xbox 360/Xenon PowerPC disassembly, XEX metadata, entry points, import thunks, and functions implicated by Xenia logs.
- Use Ghidra as an OODA accelerator, not as a whole-disc guessing machine:
  runtime capture -> address queue -> smallest Ghidra import -> inferred
  function/import/global/timer purpose -> one next experiment.
- Create or update repo-local Codex skills when a debug loop repeats. Validate
  skill folders with `quick_validate.py` when available; otherwise do a basic
  frontmatter/readability check and keep skill bodies concise.
- Current repo-only skills live under `.agents/skills/` and should be read when
  their topic is active:
  - `.agents/skills/xenia-thor-war-room/SKILL.md` is the default coordinator
    for broad Blue Dragon/Thor bring-up, unclear crashes, black screens, and
    multi-subsystem triage.
  - `.agents/skills/xbox360-xenon-ppc-architect/SKILL.md` is the guest expert
    for Xenon PowerPC, VMX128, XEX imports, kernel shims, D3D watchdogs, and
    Ghidra guest PPC analysis.
  - `.agents/skills/aarch64-snapdragon-jit-port/SKILL.md` is the host CPU
    expert for x64-to-AArch64 parity, aX360e/xenia-edge donor adaptation,
    Android code cache, thunks, signal recovery, and Snapdragon constraints.
  - `.agents/skills/xenia-vulkan-adreno-renderdoc/SKILL.md` is the GPU expert
    for Xenos PM4, ring pointers, writebacks, `VdSwap`, `PM4_XE_SWAP`, Vulkan
    `IssueSwap`, Adreno 740, RenderDoc, and black-frame debugging.
  - `.agents/skills/xenia-a64-speed-hotpath/SKILL.md` is the speed expert for
    Thor ARM64 2-FPS runs, A64 speed-profile counters, hot guest PCs,
    helper/prologue churn, guest-to-host transitions, direct/indirect call
    overhead, XMA/audio cost, and Snapdragon profiling.
  - `.agents/skills/xenia-thor-experiment-gate/SKILL.md` is the one-variable
    gate for risky ARM64, GPU, audio, input, timing, debug-prop, or
    title-specific experiments.
  - `.agents/skills/xenia-blue-dragon-route-capture/SKILL.md` is the route and
    proof expert for loading Blue Dragon Disc 1, using nop HID START/A
    sequences, labeling title/opening progress, and capturing screenshots/logs.
  - `.agents/skills/xenia-ghidra-ooda-loop/SKILL.md` is the repo-local static
    analysis coordinator for mapping Thor runtime evidence to Ghidra/native
    ARM64/guest PPC/generated-JIT queues and dated triage reports.

## Agent Skill Donor Audit

- On 2026-05-19, the Thor agent-skill layout was compared against:
  - `https://github.com/noeldvictor/Vita3K-Thor/tree/master/.agents/skills`
    at commit `4059339e7434f9f3866696ebc14ac024febdb4b8`.
  - `https://github.com/noeldvictor/rpcsx-ui-android-thor/tree/master/.agents/skills`
    at commit `71e6a963fed5267cc263215521462420cfefba00`.
- Useful ideas adapted for xenia-thor:
  - experiment gate before risky probes;
  - durable worklog/research ledger instead of chat memory;
  - Android/Thor route and screenshot proof;
  - hotpath profiler lane tied to runtime evidence;
  - ADB safety and capture metadata discipline.
- Do not blindly copy donor skill text or scripts. Synthesize Xenia-specific
  workflows, preserve source attribution in dated research notes, and keep
  Xenia's priority different: AYN Thor ARM64 first, Windows only as a control.

## RenderDoc Rules

- RenderDoc is for Vulkan frame capture only. It is not proof that guest CPU execution is correct.
- Enable RenderDoc layers only for capture runs:
  `powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode EnablePerApp`.
- Disable layers immediately after capture:
  `powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode Disable`.
- Check the actual installed RenderDoc layer package with:
  `adb shell pm list packages | grep -i renderdoc`.
- Do not upstream RenderDoc findings from this fork unless reproduced on official Xenia.

## Android GUI And OSD Direction

- Treat the Android app as a real handheld emulator shell, closer to Dolphin/Azahar than a desktop debug window.
- Keep ImGui for internal dev demos, trace tooling, and diagnostics only. Do not make ImGui the main Android user interface.
- Build user-facing Android UI with native Android activities, XML resources, adaptive icons, drawables, and Java/Kotlin glue unless a stronger reason appears.
- Keep the launcher dark, controller-friendly, and dense enough for repeated emulator use: clear game launch, trace viewer, Vulkan demo, and future settings/library entries.
- On AYN Thor, the default game picker location should be the SD-card Xbox 360 folder using DocumentsUI initial URI document id `2664-21DE:roms/xbox360`.
- Maintain a Xenia Thor Android identity:
  - Adaptive launcher icon with a recognizable Xenia/Android/Thor mark.
  - `Xenia Thor` app surface naming for Android bring-up builds.
  - README warning that the fork is extremely unstable.
- The emulator activity should have a Dolphin-like OSD layer above the render surface:
  - Native overlay panels for target title, backend/runtime status, warnings, and future FPS/frame pacing.
  - No dependency on ImGui for normal in-game overlays.
  - Native OSD must be safe over Vulkan surfaces and easy to hide or update from native code later.
- Future OSD data should come from native runtime events rather than static Java text: FPS, frame time, backend, title ID, shader compilation, thermal/performance warnings, and fatal setup errors.
- Keep layout responsive for the Thor top screen in landscape first; avoid text overlap and verify screenshots on device after UI changes.

## Current Blue Dragon / ARM64 State

- Latest validated Thor captures:
  `scratch\thor-debug\20260519-141908-*` reached the Blue Dragon title prompt
  with Android OSD hidden, `vulkan_debug_texture_fetch_disable_exp_adjust=false`,
  commit `d9576ec4c`, and APK SHA-256
  `09F480292F913D6132F3A288C7FCAB758E02FF6BAD519B1DF089BB3EF40B1224`.
- The previous `0x826A23E8` Blue Dragon null-thunk crash was traced to
  `Sound::SOUNDBANK::Load XACTCreateSoundBank()` while Android was running
  with `apu=nop`.
- `NopAudioSystem` now creates a silent driver instead of returning
  `X_STATUS_NOT_IMPLEMENTED`, allowing the guest XACT/sound-bank path to
  initialize far enough to write `0x82785548`.
- Post-fix evidence: the store watch hit
  `826A2550@826A2598->82785548`, Blue Dragon created draw and sound threads,
  and no guest crash PC appeared in the 10:09, 10:11, 11:08, or 11:13
  captures.
- The former ARM64 mini-JIT coverage work was useful for finding missing HIR
  surfaces, but it has been superseded by the donor A64 backend import.
- As of `docs/research/20260518-164150-ax360e-a64-hard-swap.md`, the active
  ARM64 backend is the copied aX360e/Edge A64 backend with local compatibility
  shims for logging, HIR helpers/opcodes, cvars, Capstone naming, POSIX unwind
  placement, and disabled info-cache MMIO recording.
- Native core validation for that hard swap passed on 2026-05-18 for both
  Android `arm64-v8a` and `x86_64`.
- The KTHREAD timer wall at guest function `8246B408` has been identified and
  moved: `arm64_update_kthread_time` writes guest uptime to the current
  KTHREAD `+0x58`, and the draw wait `global_tick` now advances.
- The command processor now mirrors guest-visible ring pointers into
  `CP_RB_RPTR` / `CP_RB_WPTR` (`0x01C4` / `0x01C5`). Blue Dragon's D3D dump now
  shows matching drained ring pointers instead of a stale zero read pointer.
- Current milestone: Blue Dragon reaches the `press START` title prompt on Thor
  with `vulkan_debug_texture_fetch_disable_exp_adjust=false` after the SPIR-V
  texture fetch translator was fixed to read result exponent adjustment from
  fetch constant dword 3, not dword 4. `vulkan_force_signed_2101010_unorm_fallback=true`
  is still part of the validated run. This is title-screen progress, not
  compatibility.
- Current milestone extension: sequenced nop HID pulses can press START, select
  English on the language screen, and reach Blue Dragon's opening scene in
  `scratch\thor-debug\20260519-144529-*`.
- Current blocker: real Android controller mapping, longer scripted input, and
  performance. The latest thread sample shows the main guest CPU thread and XMA
  decoder as the top consumers, with GPU command work below them. Treat the
  current speed wall as CPU/JIT/audio/debug-overhead first, GPU second.
- Focused PPC dumps show the graphics interrupt callback at `8246DBB0` and draw
  wait function `8246B408`; token-kick experiments prove token movement alone
  does not satisfy the game.
- Runtime swap tracing now shows real PM4 packets, `DRAW_INDX_2`, texture
  creation/loading, runtime `GPU swap trace: VdSwap` calls, `PM4_XE_SWAP`, and
  Vulkan `IssueSwap`. `VdSwap` in import/symbol listings is not runtime call
  proof; only explicit `GPU swap trace:` runtime lines count.
- Forced-presenting a known format-6 resolve can visibly change the Android
  surface, so the presenter is not the current wall. Next probes should link
  render target state, resolve output, candidate format, and official
  frontbuffer clearing/population.
- The visible OSD badge now reports `aX360e A64 backend research`.
- Strategy as of 2026-05-18 14:12 EDT: stop using Thor as the only unit test.
  Run a broad x64-to-ARM64 conversion pass first, then use Thor/Blue Dragon as
  milestone evidence.

## ARM64 Conversion Audit Loop

- Do not interpret "all HIR opcodes have a case label" as "the backend is
  ported." It only proves switch coverage.
- Before more game-specific fixes, batch the x64-to-ARM64 conversion work:
  host-to-guest and guest-to-host thunks, resolve thunk, breakpoint support,
  exception/signal recovery, code-cache indirection and metadata, source maps,
  unwind info where available, native scalar/control/memory lowering, native
  vector/pack/unpack lowering, and differential tests.
- After the 2026-05-19 translation report, broad ARM64 passes should include
  cheap runtime counters before more one-off Thor fixes: guest function/block
  entries, helper calls by opcode/category, guest-to-host transitions,
  code-cache lookup/link/miss/recompile stats, and top Blue Dragon guest PCs.
- Use Thor runs as milestone checkpoints after broad backend batches, not as
  the only unit test.
- Current conversion audit:
  `docs/research/20260518-140436-arm64-x64-conversion-audit.md`.

## ARM64 Fork Audit Decision

- Latest audit: `docs/research/20260517-183520-xenia-arm64-fork-audit.md`.
- Current working-fork reality check:
  `docs/research/20260518-142302-arm64-xenia-working-forks.md`.
- Some ARM64 Xenia-derived projects appear to work in limited senses:
  `aenu1/ax360e` is the strongest Android ARM64/Vulkan evidence, and
  XeniOS/xenia-mac are Apple ARM64 evidence. None is a proven drop-in path for
  Blue Dragon on Thor.
- The helper-backed ARM64 mini-JIT scaffold has been removed from the active
  backend. Use the donor A64 backend as the only forward ARM64 path unless a
  future comparison explicitly needs old-history archaeology.
- Prioritize correctness coverage and differential tests first; add native
  hot-path lowering where CPU slowness alone could cause watchdogs.
- Primary AArch64 JIT source to study/port: `has207/xenia-edge`, branch `edge`, because it has the most current xbyak_aarch64 A64 backend shape, CMake wiring, POSIX code cache, and broad HIR opcode table coverage.
- Primary Android app-side reference: `aenu1/ax360e`, branch `main`, because it shows a real Android package with nested Xenia Canary, A64 backend, Android CMake glue, SAF/document-file access, Android HID, Adreno options, and `libadrenotools`.
- AArch64 assembler dependency: `third_party/xbyak_aarch64` is pinned to Fujitsu `xbyak_aarch64` commit `59c6a7e7368cd2dbb07c46abe83239ace4d58849`, matching the `has207/xenia-edge` submodule gitlink observed during the audit. License: Apache-2.0.
- Secondary provenance and portability references: `wmarti/xenia-mac`, `Wunkolo/xenia`, and `joetheshmoe/xenia-winarm`.
- Treat `edilsongg/xenia-arm64` and `fillips67fissile/xenia-arm64-backend` as Wunkolo duplicates unless a future diff proves unique work.
- Do not import from `SbossTreeSitty`, `Xenia-creator2025/Xenia-emulator-android`, or the `ProyectoXenia` repos for JIT work.
- Before copying any third-party code, verify the specific file license, preserve attribution, and keep this fork clearly marked as unofficial research.
- First import slice: `docs/research/20260518-143943-arm64-upstream-import-slice.md`.
  - Adapted Xenia Edge A64 `BRK #0` breakpoint / illegal-instruction handler
    behavior into the current `arm64` backend.
  - Adapted aX360e Android build hardening: frame pointers, unwind tables,
    16 KiB max page-size linker option, and disabled native heap pointer
    tagging.
  - Post-import Blue Dragon regression capture `scratch/thor-debug/20260518-144723-*`
    shows no native crash, but still hits the frame-0 D3D watchdog with drained
    ring pointers. Do not claim game progress from this slice.
  - Next backend parity gaps are Edge-style host-to-guest/guest-to-host/resolve
    thunks, code-cache indirection/commit metadata, and data/unwind placement.
  - Do not wholesale-copy aX360e SAF/audio/HID/AdrenoTools or the full Edge
    emitter until each batch has a clear build boundary and attribution note.
- Second Edge import slice:
  `docs/research/20260518-145131-arm64-code-cache-edge-brk-fill.md`.
  - The ARM64 code cache fills unwritten generated-code memory with `BRK #0`,
    matching Xenia Edge A64 debug guard behavior.
  - This is debug hardening only; it is not the full Edge code-cache,
    indirection, unwind, or thunk import.
  - Thor validation capture `scratch/thor-debug/20260518-145425-*` on commit
    `9be7323b8` shows no new native crash and the same Blue Dragon D3D watchdog.
- Third Edge-shaped backend slice:
  `docs/research/20260518-151702-arm64-thunk-cache-scaffold.md`.
  - ARM64 now has x64/Edge-shaped thunk slots, a resolve helper, sparse encoded
    indirection table metadata, `CommitExecutableRange`, `AddIndirection64`,
    `PlaceData`, unwind placeholder metadata, and generated-code commit marks.
  - The conversion audit's backend/code-cache capability-name gaps are closed,
    but this was later superseded by the hard donor A64 backend swap.
  - Thor validation capture `scratch/thor-debug/20260518-152107-*` shows the
    indirection table allocated on device with no new native crash, and Blue
    Dragon still stops at the known D3D frame-0 watchdog. Do not claim title
    progress from this slice.
- Fourth Edge-shaped backend slice:
  `docs/research/20260518-152759-arm64-generated-transition-thunks.md`.
  - ARM64 now generates host-to-guest, guest-to-host, and resolve thunks with
    `xbyak_aarch64`, places them in the ARM64 code cache, sets the default
    indirection target to the resolve thunk, commits the force-return range,
    and logs generated thunk addresses at startup.
  - This is not yet the full Edge A64 ABI. Backend context, stackpoint/vector
    save details, unwind data, exception recovery, and native emitter call
    routing are still incomplete.
  - Thor validation capture `scratch/thor-debug/20260518-152749-*` shows the
    generated thunks on device with no new native crash, and Blue Dragon still
    stops at the known D3D frame-0 watchdog. Do not claim title progress from
    this slice.
- First concrete aX360e memory-layout import:
  `docs/research/20260518-154350-ax360e-fixed-high-memory-layout.md`.
  - Added opt-in `mmap_address_high`, default `0`, to try aX360e-style fixed
    guest memory at `mmap_address_high << 32`.
  - When set, ARM64 code cache tries the matching high window:
    indirection at `((mmap_address_high + 2) << 32) | 0x80000000` and code at
    `((mmap_address_high + 2) << 32) | 0xA0000000`.
  - Use `tools/thor/thor_xenia_debug.ps1 -MmapAddressHigh 8` for Thor
    experiments. Validation capture `scratch/thor-debug/20260518-154338-*`
    proves fixed guest memory, fixed code cache, and fixed thunk addresses on
    Thor with no new native crash.
  - Blue Dragon still stops at the known D3D frame-0 watchdog; do not claim
    title progress from this slice.
- Second concrete aX360e/Edge runtime import:
  `docs/research/20260518-155736-ax360e-guest-trampolines.md`.
  - Added aX360e `BitMap::AcquireFromBack`, backend context lifecycle hooks,
    `GuestTrampolineGroup`, `CreateGuestTrampoline`, and `FreeGuestTrampoline`.
  - Thread contexts now reserve a 256-byte backend prefix before `PPCContext`,
    matching the A64 backend family assumption.
  - ARM64 now allocates an executable guest trampoline host pool, emits the
    aX360e/Edge `movz/movk` + `br x9` trampoline sequence, flushes instruction
    cache, and publishes 64-bit trampoline targets through the ARM64
    indirection table.
  - Validation capture `scratch/thor-debug/20260518-155837-*` proves the
    trampoline pool on Thor:
    `trampolines=000000775BD04000-000000775BF24000`, with no new native crash.
  - This slice does not wire kernel/runtime callback paths to backend-created
    trampolines yet. Blue Dragon still stops at the known D3D frame-0 watchdog;
    do not claim title progress from this slice.
- Third concrete aX360e/Edge runtime import:
  `docs/research/20260518-160807-ax360e-arm64-backend-context.md`.
  - Added `Arm64ReserveHelper`, `Arm64BackendStackpoint`,
    `Arm64BackendContext`, `Clock::GetGuestTickCountPointer`, and ARM64
    backend context lifecycle methods.
  - `Arm64Backend::SetGuestRoundingMode` now caches FPCR state and writes host
    FPCR only on ARM64 builds, while still updating PPC `fpscr` state.
  - Validation captures `scratch/thor-debug/20260518-160908-*` and
    `scratch/thor-debug/20260518-160947-*` show no new native crash and the
    same Blue Dragon D3D frame-0 watchdog.
  - Do not enable full stackpoint synchronization or switch thunks to `x19`
    backend context until the real A64 function prolog/epilog path lands.
- Hard aX360e A64 backend swap:
  `docs/research/20260518-164150-ax360e-a64-hard-swap.md`.
  - Removed the active helper-backed `arm64` mini-JIT implementation files.
  - Imported the donor A64 backend to `src/xenia/cpu/backend/a64` and kept
    `src/xenia/cpu/backend/arm64/arm64_backend.h` as a wrapper alias.
  - Added donor `CodeCacheBase`, ARM64 platform feature helpers, and HIR
    compatibility helpers/opcodes needed by the donor emitter.
  - Current quick shims: donor tracer logging maps to this fork's logging API;
    donor `DEFINE_int64` cvars use supported cvar types; Capstone AArch64 maps
    to this tree's ARM64 naming; POSIX unwind uses the reserved unwind buffer;
    the donor XexModule info-cache MMIO recorder is stubbed until imported.
  - Native core build passes for Android `arm64-v8a` and `x86_64`.
  - Next proof needed: FullDeploy to Thor, launch Blue Dragon with
    `-MmapAddressHigh 8`, then capture logcat/screenshot and fix the first
    runtime crash or missing sequence.
- Blue Dragon current GPU wall:
  `docs/research/20260518-223723-blue-dragon-format-six-present-probe.md`.
  - Blue Dragon now runs long Thor captures with thousands of `VdSwap` /
    `XE_SWAP` events and no native fatal, AndroidRuntime crash, GPU hang, or
    ARM64 fallback lines in the latest runs.
  - The official `VdSwap` frontbuffers `1CA1C000` and `1CDB4000` are still
    zero at swap in checksum probes.
  - Forced and scored presentation prove Android/Vulkan can display guest
    resolve memory, so the remaining wall is not the APK surface or Vulkan
    presenter.
  - `-VulkanPresentScoredResolveRequiredFormat 6` follows dynamic format-6
    resolves and currently presents a white clear-like `720x720` source
    (`1D88F000+00385000`, first sampled value `00FFFFFF`).
  - Use the required-format knob only as a comparison probe. It is not a
    compatibility fix. The next GPU work should correlate render-target
    state, clear/mask state, draws, resolves, and candidate content before
    sweeping more addresses.
- Blue Dragon draw/resolve proof:
  `docs/research/20260518-230320-blue-dragon-draw-state-flat-resolves.md`.
  - `vulkan_present_scored_resolve_reject_clear_like=true` now rejects repeated
    or low-variation resolve candidates instead of selecting white or dark
    clear-like surfaces.
  - With clear-like rejection enabled, all sampled scored present candidates in
    the latest 800-sample Thor run were rejected as clear-like. That means
    source selection is not the immediate wall.
  - `vulkan_trace_draw_state=true` proves real Vulkan draw calls execute with
    rasterization, pixel shaders, color writes, and `normalized_color_mask=000F`.
  - Resolved shared-memory output after those draws remains zero or flat /
    clear-like, while official `VdSwap` frontbuffers remain zero.
  - Next GPU lane: instrument the Vulkan render-target dump / resolve boundary
    so we can prove whether host render targets are already flat, or whether
    pixels are lost while dumping EDRAM or copying to shared memory.
- Blue Dragon EDRAM dump and FSI probe:
  `docs/research/20260518-231925-blue-dragon-edram-dump-and-fsi-probe.md`.
  - `vulkan_trace_edram_checksum=true` can read back the host-render-target
    EDRAM dump span before the shared-memory resolve copy, then reopen the
    Vulkan submission for the normal resolve.
  - Latest evidence shows EDRAM-after-dump is already zero or near-uniform for
    the Blue Dragon present candidates, and the later shared-memory resolve
    preserves that flat content.
  - The AYN Thor Adreno driver does not expose fragment shader sample or pixel
    interlock, so `render_target_path_vulkan=fsi` falls back to `fbo`.
  - The next lane is host render-target image / draw-output correctness:
    prove whether the RT image itself is flat before dump, or whether the dump
    shader / format path reads it incorrectly.
- Blue Dragon raster/dump-state proof:
  `docs/research/20260518-233120-blue-dragon-raster-dump-state.md`.
  - The visible fullscreen/composite draw family has sane scissor rectangles
    for 640x360, 320x180, 160x90, 80x45, and 1280x720 passes.
  - Those textured draws are not obviously depth-killed; they reach
    `pixel=true`, `ps_writes=1`, and `normalized_color_mask=000F`.
  - Dump routing matches the expected render-target keys, formats, rectangles,
    and dispatch sizes, but EDRAM/shared-memory samples remain flat.
  - Next GPU lane: trace texture inputs and shader bindings for the textured
    passes, especially `used_textures=0000007F` on the 1280x720 pass and
    `used_textures=00000001` on the downscale/composite passes.
- Blue Dragon texture-input proof:
  `docs/research/20260518-234356-blue-dragon-texture-input-trace.md`.
  - The visible 1280x720 fullscreen draw reaches `pixel=true`, writes all color
    channels, and uses pixel texture mask `0000007F`; all seven traced fetches
    have valid Vulkan texture bindings and loaded unsigned texture objects.
  - Downscale/composite draws also have valid pixel texture bindings; no traced
    used fetch reports `valid=false`, `has_unsigned=false`, or `scaled=true`.
  - The resolved output is still flat or clear-like, so the wall moved from
    texture descriptor plumbing to source texture population, pixel shader
    translation/constants, or the earlier render pass that produces the sampled
    sources.
  - Next GPU lane: split source data from shader output with active-texture
    source checksums and a targeted shader/constant trace for the visible
    fullscreen pixel shader hashes.
- Blue Dragon texture-source checksum proof:
  `docs/research/20260518-235501-blue-dragon-texture-source-checksum.md`.
  - `vulkan_trace_texture_source_checksum=true` is routed through Android and
    the Thor script and can read back active pixel texture source ranges before
    a draw samples them.
  - The visible fullscreen shaders have valid source ranges, but the large
    render-chain sources are already flat or clear-like before the draw.
  - The later `used_textures=000001FF` shader sees non-clear packed DXT source
    data, but the resolved output remains clear-like.
  - Next GPU lane: trace shader constants and shader-source semantics for the
    visible fullscreen pixel shader family before changing render-target code.
- Blue Dragon shader-constant proof:
  `docs/research/20260519-000221-blue-dragon-shader-constant-trace.md`.
  - `vulkan_trace_shader_constants=true` is routed through Android and the Thor
    script and logs active pixel shader constant maps and values.
  - The visible fullscreen pixel shaders have present, finite-looking constants
    where expected; they do not depend on bool or loop constants.
  - This moves the immediate wall past descriptor setup, source-memory readback,
    and gross constant upload.
  - Next GPU lane: dump and inspect Xenos microcode / translated SPIR-V for the
    visible fullscreen shader hashes to see which fetches and constants really
    drive exported color.
- Blue Dragon shader-dump and presenter proof:
  `docs/research/20260519-001514-blue-dragon-shader-dump-presenter-proof.md`.
  - `dump_shaders` is now routeable from ADB extras, and
    `tools/thor/thor_xenia_debug.ps1 -DumpShaders auto` creates/pulls a
    timestamped shader dump directory alongside the capture.
  - Vulkan now dumps translated SPIR-V binaries when `dump_shaders` is enabled;
    Xenos ucode binary/text dumps are produced by the existing shader-analysis
    path.
  - The `0ABADD9DA4373CBA` composite shader's non-clear DXT fetch 7 is gated
    out in the observed draw by `c26.x = 2.0`, so the final chain is mostly
    sampling already-flat render targets.
  - `vulkan_debug_solid_guest_output=true` produces a visible magenta frame on
    Thor, proving the Android/Vulkan presenter is alive. The black-frame wall is
    upstream of presentation.
- Blue Dragon targeted vertex-fetch proof:
  `docs/research/20260519-003650-blue-dragon-targeted-vertex-fetch-trace.md`.
  - `vulkan_trace_vertex_fetch_checksum=true` is routed through Android and the
    Thor script, with `vulkan_trace_vertex_fetch_shader_filter` for
    shader-hash-focused captures.
  - The final `5CF1EE2E3C82546B` / `9567C79307ACC6F5` fullscreen blit has sane
    position and UV attributes, and its vertex color multiplier is not zero.
  - The 1280x720 source feeding that final pass is already zero or clear-like
    before the blit resolves to `1D14C000` and the official frontbuffers.
  - The next GPU lane added targeted shader filters for upstream draw-state,
    shader-constant, and texture-source probes.
- Blue Dragon targeted shader-filter proof:
  `docs/research/20260519-005540-blue-dragon-targeted-shader-filter-trace.md`.
  - `vulkan_trace_draw_shader_filter`,
    `vulkan_trace_shader_constants_shader_filter`, and
    `vulkan_trace_texture_source_shader_filter` are routed through Android and
    the Thor script.
  - `05775DE8A2B0B3F5` writes nonzero clear-like `1DC14000` content, then
    `0ABADD9DA4373CBA` samples that source and resolves `1DC14000` back to
    zero.
  - Later post passes and the final `9567C79307ACC6F5` blit sample already-zero
    inputs, so the first concrete zeroing point is upstream of final blit and
    presentation.
  - Blue Dragon also successfully opens `D:\pack\!necessity.ipk`, then probes
    missing loose `D:\!necessity\font\...` paths and uses `cache:\pack`; audit
    VFS/cache behavior before assuming a pure shader translator bug.
- Blue Dragon pixel-output override proof:
  `docs/research/20260519-012358-blue-dragon-pixel-output-override-probe.md`.
  - `vulkan_debug_pixel_shader_output_filter` and
    `vulkan_debug_pixel_shader_output_mode` are routed through Android and the
    Thor script for research-only shader export overrides.
  - Forcing `0ABADD9DA4373CBA` to magenta makes the `1DC14000` resolve
    nonzero (`first_sample=FF03F0FF`), proving that shader/color export can
    write the target.
  - A following copy/draw/resolve sequence to `raw_dest_base=1DC14000` turns the
    target back to zero before `9567C79307ACC6F5` samples it for the final
    blit.
  - The current GPU wall is therefore the post-`0ABADD9DA4373CBA` zeroing
    writer, not Android presentation, final-blit vertex data, or the targeted
    shader's ability to export nonzero color.
  - Next GPU lane: tag copy/resolve trace lines with recent draw sequence and
    shader hashes, then identify the exact writer that clears `1DC14000`.
- Blue Dragon post-`0ABADD9DA4373CBA` shader-chain proof:
  `docs/research/20260519-014135-blue-dragon-post-abadd-shader-chain.md`.
  - Vulkan copy trace lines now include copy sequence, recent draw sequence,
    recent vertex shader hash, and recent pixel shader hash.
  - Forced `0ABADD9DA4373CBA` writes `1DC14000` nonzero, but the following
    `57B736C8B5D4E953` pass writes it back to zero.
  - Forced `57B736C8B5D4E953` writes `1DC14000` nonzero and shows a visible
    debug frame, but the following `B02CC5F55AD0D140` pass writes it back to
    zero.
  - Forced `B02CC5F55AD0D140` writes `1DC14000` nonzero; the final
    `9567C79307ACC6F5` pass samples it, resolves `1D14C000` nonzero, and then
    writes an official frontbuffer nonzero.
  - The older generic frontbuffer checksum reads CPU guest memory and can be
    misleading for Vulkan shared-memory state. Prefer Vulkan resolve,
    texture-source, copy-state, and swap shared-memory checksums for present
    proof.
  - The current GPU wall is the unforced `B02CC5F55AD0D140` shader pass or its
    texture/constant/boolean state, especially format 7 `2_10_10_10`, DXT
    input, branch booleans, log/exp/NaN handling, and export clamping.
- Blue Dragon title exp-adjust proof:
  `docs/research/20260519-022333-blue-dragon-title-exp-adjust-probe.md`.
  - B02 raw unsigned sample output is nonzero, but the post-processed fetch
    output is black because the active fetch path applies a `-16` result
    exponent adjustment to values already normalized by the temporary
    `2_10_10_10` fallback.
  - `vulkan_debug_texture_fetch_disable_exp_adjust=true` reached the visible
    `press START` title prompt in `scratch\thor-debug\20260519-022127-*`.
  - Keep this cvar default-off and research-only. This proof was superseded by
    the dword3 result exponent fix below.
- Blue Dragon dword3 result exponent fix:
  `docs/research/20260519-133516-blue-dragon-dword3-title-fix.md`.
  - The SPIR-V translator was using fetch constant dword 4 for result exponent
    adjustment. That word carries LOD/gradient exponent bias in the same bit
    range.
  - The title path now reads result exponent adjustment from dword 3.
  - Validated title captures: `scratch\thor-debug\20260519-132943-*` and
    post-push `scratch\thor-debug\20260519-141908-*`.
  - Validated cvar state: `disable_fetch_exp_adjust=false`,
    `force_signed_2101010_unorm=true`.
- Blue Dragon nop HID sequence proof:
  `docs/research/20260519-144858-blue-dragon-nop-hid-sequence.md`.
  - `hid_nop_button_sequence` supports entries like
    `start@45000:3000;a@68000:3000`.
  - `scratch\thor-debug\20260519-144021-*` reached the language menu.
  - `scratch\thor-debug\20260519-144529-*` reached the opening scene after
    language selection.
  - This is scripted research input only; it is not real Android controller
    mapping yet.
- Blue Dragon performance triage:
  `docs/research/20260519-151305-blue-dragon-performance-triage.md`.
  - Trace-heavy proof runs are not speed samples.
  - Trace-off run `scratch\thor-debug\20260519-150821-*` stayed alive in the
    opening sequence.
  - Live `top -H` showed main guest CPU and XMA decoder ahead of GPU command
    thread CPU use.
- Xbox 360 to Thor Max translation report:
  `docs/research/20260519-153016-xbox360-thormax-translation-report.md`.
  - Blue Dragon title/language/opening proof is real progress, but not a
    compatibility claim.
  - The immediate speed wall is CPU/A64 dispatch, helper fallbacks,
    guest-to-host transitions, XMA/audio cost, and debug overhead first; GPU is
    second unless a focused capture proves otherwise.
  - Next probes should produce cheap counters for guest functions/blocks,
    helper categories, code-cache links/misses, indirect branches, recompiles,
    and top guest PCs in trace-off runs.
  - Use `apu=nop` or `xma_fast_silence` only as comparison lanes, never as the
    main correctness path.
- A64 speed profile counters:
  `docs/research/20260519-162000-a64-speed-profile-counters.md`.
  - New default-off cvars:
    `arm64_speed_profile_interval_ms`,
    `arm64_speed_profile_top_functions`, and
    `arm64_speed_profile_min_delta`.
  - First profiled Blue Dragon run
    `scratch\thor-debug\20260519-161600-*` built, deployed, stayed alive, and
    produced top-function summaries without fatal, device-lost, GPU-hung, or
    AndroidRuntime crash lines.
  - Final interval was dominated by direct guest calls and top deltas in
    `__savegprlr_29`, `__restgprlr_29`, `8248B040`, and known draw-wait
    function `8246B408`. Next CPU optimization should inspect whether the A64
    backend can safely inline/special-case the PPC save/restore compiler
    helpers.
- A64 GPR/LR helper inline:
  `docs/research/20260519-170744-a64-gprlr-helper-inline.md`.
  - `a64_inline_gprlr_helpers` is routed through Android and the Thor script
    and is default-on in the A64 backend.
  - `__savegprlr_*` is inlined as big-endian guest stack stores for GPRs plus
    the 32-bit LR slot from `r12`.
  - `__restgprlr_*` is inlined as big-endian guest stack loads, but must compare
    the restored LR to the current guest return address. Equal LR returns
    through the current epilog; mismatch LR falls back to the normal A64
    tail-jump/code-cache path.
  - The naive "always epilog" restore shortcut is wrong; it stayed alive but
    blocked the visible Blue Dragon route in `scratch\thor-debug\20260519-170818-*`.
  - Corrected validation `scratch\thor-debug\20260519-171625-*` reached the
    visible opening scene, had no crash/device-loss markers, and removed
    `__savegprlr_*` / `__restgprlr_*` from the top profile.
  - Next speed work should inspect `8246B408` / `8248B040` and consider FPR/VMX
    helper-family inlining.
- Blue Dragon speed-loop noise budget:
  `docs/research/20260519-184650-blue-dragon-speed-loop-noise-budget.md`.
  - Current fast-lane command should explicitly include
    `-A64InlinePpcThreadFieldLeafHelpers true -Arm64BlueDragonDrawWaitProbe true`
    when comparing Blue Dragon opening-scene speed.
  - `arm64_blue_dragon_draw_wait_probe_stride` exists and defaults to `1`.
    Larger power-of-two strides are an A/B probe; stride `4096` was not a speed
    win in the first Thor run.
  - `arm64_blue_dragon_draw_wait_inline_tick_step` exists as a research-only
    wait accelerator, but step `1` was not a speed win. Keep it off unless that
    exact variable is under test.
  - `gpu_unknown_register_log_budget` and
    `xboxkrnl_nt_create_file_fail_log_budget` are routed through Android and
    the Thor script. Blue Dragon speed defaults set both to `0` so warning logs
    do not drown profiler captures.
  - `MountCache true` is now testable because speed defaults no longer force it
    off, but the first 110s A/B did not improve the current Blue Dragon route.
  - Latest quiet validation `scratch\thor-debug\20260519-184120-*` reached the
    opening scene with zero unknown-register lines, zero `NtCreateFile failed`
    lines, and `8246B408` still dominating the final A64 speed profile.
- Blue Dragon draw-wait fastpath:
  `docs/research/20260519-192038-blue-dragon-draw-wait-fastpath.md`.
  - `arm64_blue_dragon_draw_wait_fastpath` is routed through Android and
    `tools/thor/thor_xenia_debug.ps1`, but is default-off and title-specific.
  - The direct body for `8246B408` compiles to about `448` bytes versus the
    generic `1632`-byte body and can reach the visible opening scene when paired
    with the real KTHREAD clock probe.
  - `-Arm64BlueDragonDrawWaitInlineTickStep 1` and
    `-Arm64BlueDragonDrawWaitProbeStride 4096` both parked on black-screen
    routes, so do not treat either as a speed default.
  - The useful fastpath currently moves the bottleneck into guest-to-host clock
    updates. Next work should make guest uptime cheap in generated A64 or move
    the update closer to the original PPC load-site semantics.
- Blue Dragon host-counter time fastpath:
  `docs/research/20260519-193313-blue-dragon-host-counter-time.md`.
  - `arm64_blue_dragon_draw_wait_fastpath_host_counter_time` is routed through
    Android and the Thor script, default-off.
  - It reads `CNTVCT_EL0` / `CNTFRQ_EL0` in generated A64, subtracts
    `A64BackendContext::host_uptime_millis_base`, and writes the result to
    `KTHREAD+0x58` without a native clock thunk.
  - Best proof so far:
    `scratch\thor-debug\20260519-192732-*` plus follow-up screenshot
    `scratch\thor-debug\20260519-193236-screenshot.png` reached rendered
    Blue Dragon opening/game content on AYN Thor.
  - Current Blue Dragon command should include
    `-Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitProbe true -MountCache true`.
  - The remaining wall is still `8246B408`, now as generated A64 busy-wait
    cost rather than per-entry native clock thunk cost.

## Codex Hooks / Automation

- Research note: `docs/research/20260519-133516-codex-hooks-blue-dragon-loop.md`.
- Project hooks live in `.codex/hooks.json` and must be reviewed/trusted by the
  Codex environment before they run.
- `.codex/hooks/blue_dragon_stop.ps1` is passive by default. It only
  auto-continues when `scratch\thor-debug\continue-until-blue-dragon-title.flag`
  exists and stops once `scratch\thor-debug\latest-title-proof.json` exists.
- The Blue Dragon Stop hook is bounded to 12 attempts so ADB/device/build
  failures become a blocker report instead of an endless loop.
- Prefer a Codex thread automation only for scheduled later wakeups; use the
  project Stop hook for active-session bounded continuation.

## Android ARM64 Risk Register

- CPU backend: the active AArch64 path is now a donor aX360e/Edge A64 backend
  import. It builds, but runtime correctness on Thor is unproven after the hard
  swap.
- JIT memory: Android executable memory and cache coherency must be tested on device.
- Guest memory layout: verify fixed mappings and any 32-bit guest assumptions on Android.
- Vulkan: the manifest requires Vulkan, but runtime feature probing still needs Thor Max logs.
- Input: Android currently falls back to nop HID for emulator app paths; real controls need mapping.
- Audio: Android currently uses 5 ms paced silent nop audio for bring-up. This
  is enough to satisfy early XACT driver registration, but not a real Android
  audio backend.
- UI/app flow: Android launcher has a first-pass game file picker, but user experience, permissions, errors, and direct path handling still need hardening.
- Build dependencies: submodules are not initialized after a fresh clone.

## Code Style And Safety

- Follow `docs/style_guide.md`.
- Prefer small, reviewable changes with a worklog entry for each session.
- Do not run destructive Git commands.
- Do not bypass DRM, distribute copyrighted game content, or add workflows that assume pirated games.
- Use legally owned Xbox 360 content only.
- Never claim a game works without a device log, build hash, settings, and a reproducible launch path.
- For Blue Dragon or any retail game, document only the user's legally owned local dump path and never commit game files, keys, screenshots containing private account data, or copyrighted content.

## Useful Commands

```powershell
git status --short --branch
git submodule update --init --recursive
python xenia-build setup --target_os=android
python xenia-build premake --target_os=android
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullApk
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell
```

No-space path workaround used successfully on 2026-05-17:

```powershell
subst X: "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor"
cd X:\android\android_studio_project
.\gradlew.bat :app:assembleGithubDebug
subst X: /D
```

Thor debug loop:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode FindContent
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchBlueDragon
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode Capture
```

Ghidra native import, after pointing to a real Ghidra install:

```powershell
$env:GHIDRA_HOME = "C:\path\to\ghidra"
powershell -ExecutionPolicy Bypass -File tools\thor\ghidra_headless_import.ps1
```

## Resolved User Answers

- Device is the Thor Max tier; exact storage size is not important for current debugging.
- First retail target is the user's legally owned Blue Dragon Disc 1 on the SD card.
- The user accepts adding an AArch64 emitter dependency such as VIXL or AsmJit after research.
