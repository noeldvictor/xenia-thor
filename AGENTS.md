# Agent Instructions

## Mission

Port this Xenia fork to native Android ARM64 for the AYN Thor Max while keeping performance work visible from the first day.

This repository is a local research experiment, not official Xenia. Do not send
fork-specific crashes, compatibility claims, RenderDoc traces, or half-built
ARM64 backend questions to upstream Xenia, the Xenia Discord, or unrelated
open-source maintainers unless the problem is independently reproduced on an
official upstream build.

## Branch And Commit Rules

- Work on `master` only unless the user explicitly changes this policy.
- Do not create `codex/` feature branches for this repo while this rule is in effect.
- Commit and push often after validated Android UI, native ARM64/Vulkan core,
  debug automation, and device capture slices.
- Keep each commit tied to a dated worklog entry. Research-driven changes must
  also have a dated research note.

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
- The only production CPU backend present is `src/xenia/cpu/backend/x64`.
- `src/xenia/cpu/backend/arm64` is an experimental scaffold with a tiny helper-backed AArch64 mini-JIT plus interpreter fallback for bring-up only; it is not a compatibility-grade backend.
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
- Use `tools/thor/thor_xenia_debug.ps1` for repeatable ADB install, launch, and capture.
- `tools/thor/thor_xenia_debug.ps1` now retries flaky ADB transports for the
  known Thor serial and records reconnect events in capture metadata.
- Use `tools/arm64/hir_coverage_report.ps1` to summarize latest Thor logcats
  into HIR opcode counts, unimplemented opcodes, mini-JIT fallback reasons,
  slow interpreter functions, guest crash PCs, ARM64 guest-store watch hits,
  and PPC global-reference scanner hits.
- Use `tools/arm64/arm64_jit_gap_report.ps1` to compare HIR opcodes against
  ARM64 mini-JIT and interpreter switch coverage, then attach recent Thor log
  watchdog/fallback signals to a dated Markdown report.
- Use `tools/arm64/arm64_conversion_audit.ps1` before broad ARM64 backend work
  or after suspicious Thor runs. It summarizes x64-vs-ARM64 backend services,
  HIR switch coverage, helper-heavy mini-JIT surface, reject/error signals, and
  recent Thor evidence into a dated Markdown report.
- Use `tools/thor/ghidra_headless_import.ps1` for repeatable Ghidra headless imports once `GHIDRA_HOME`, `-GhidraHome`, or `-AnalyzeHeadless` points to a real Ghidra install.
- Use `tools/thor/thor_renderdoc.ps1` for Android Vulkan layer setup, RenderDoc status, cleanup, and capture pulling.

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
   - Keep ARM64 backend scaffolding honest: it may compile and interpret some guest code, but it must clearly identify itself as non-playable research scaffolding until the AArch64 JIT exists.
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

5. Optimize for Thor Max early.
   - Treat Adreno 740 / Vulkan driver behavior as a real target, not an afterthought.
   - Track CPU, GPU, thermal, fan, battery, and frame pacing observations in worklogs.
   - Preserve correctness before speed; every speed experiment needs a comparable baseline.

## Debug Automation Rules

- Keep APK shell rebuilds and native core rebuilds separate:
  - UI/manifest/resource changes: `thor_build.ps1 -Mode ApkShell`.
  - C++/Vulkan/ARM64 backend changes: `thor_build.ps1 -Mode NativeCore`.
  - Release-to-device validation: `thor_build.ps1 -Mode FullDeploy`.
- Prefer scripted Thor debug loops over manual clicking once a path is known:
  `FindContent`, `LaunchBlueDragon`, `LaunchEmulator`, `LaunchWindowDemo`, and `Capture`.
- Always clear logcat before a launch and capture a full log, filtered log, screenshot, metadata file, APK hash, branch, commit, process id, focused activity, and target path.
- For ARM64 mini-JIT risk control, use:
  - `-Arm64MiniJit false` to force interpreter from the Thor launch script.
  - `-Arm64MiniJitBlacklist "826A23C8"` to blacklist exact guest functions.
  - `-Arm64ForceInterpreterRanges "826A0000-826AFFFF"` to force a guest range
    through the interpreter.
  - `-Arm64GuestStoreWatch "82785548"` to log ARM64 backend stores that touch
    a suspect guest global or range.
- For focused guest-code and wait debugging, use:
  - `-DisassembleFunctionFilter "8246DBB0,8246B408"` with
    `-DisassembleFunctions true` to dump only matching guest functions.
  - `-Arm64CompiledCallTraceFunctions "8246B408"` to sample a hot compiled
    guest function.
  - `-Arm64BlueDragonDrawWaitProbe true` to log the current Blue Dragon draw
    wait state.
  - `-XboxkrnlThreadWaitTrace true` and `-XboxkrnlEventTrace true` for kernel
    wait/event proof.
  - `-ClearMemoryPageState true` for Canary-style GPU shared-memory page-state
    experiments.
  - `-GpuInterruptOnRingIdle true` and `-GpuBlueDragonKickWaitToken true` only
    for clearly documented Blue Dragon research runs; both are default-off
    hacks, not compatibility fixes.
  - `-GpuTraceSwap true` to trace guest video setup, runtime `VdSwap`, PM4
    `XE_SWAP`, and Vulkan `IssueSwap` proof. Import/symbol lines mentioning
    `VdSwap` are not runtime call proof; use the explicit `GPU swap trace:`
    lines for runtime evidence.
- Use `StopNoise` before game runs if another emulator or graphics app is stealing focus or polluting logcat.
- Use the default Blue Dragon path only for the user's local Thor SD card. Do not assume other machines or devices have the same mount UUID.
- Keep Blue Dragon attempts honest: until ARM64 JIT exists, guest code may execute slowly in the interpreter scaffold, but the expected result is still not a playable game.
- For native Android crashes, analyze the unstripped `arm64-v8a/libxenia-app.so` from `android/android_studio_project/app/build/intermediates/ndkBuild/githubDebug/obj/local/arm64-v8a/`.
- For guest-code static analysis, use Ghidra only on legally owned and locally extracted/decrypted code. Do not commit game binaries, extracted XEX files, ISO contents, keys, or private screenshots.
- Ghidra tracks:
  - Native ARM64: `libxenia-app.so`, JNI bridge, Vulkan path, ARM64 backend, crashes, and symbolized stack traces.
  - Guest PPC: Xbox 360/Xenon PowerPC disassembly, XEX metadata, entry points, import thunks, and functions implicated by Xenia logs.
- Create or update local Codex skills when a debug loop repeats. Validate skill folders with `quick_validate.py` and keep skill bodies concise.
- Current local skills to use:
  - `$ayn-thor-xenia-debug` for build/install/ADB capture loops.
  - `$xenia-ghidra-android-debug` for Ghidra native/guest analysis planning and headless imports.

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
  `scratch\thor-debug\20260518-135816-*` and
  `scratch\thor-debug\20260518-140109-*`.
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
- ARM64 mini-JIT scalar bring-up now covers raw float32/float64 load/store,
  context/local/memory slots, casts, converts, float add/sub/mul/div,
  mul-add/mul-sub, neg/abs/sqrt, float compares, `IS_NAN`, MMIO load/store,
  and `ATOMIC_COMPARE_EXCHANGE`.
- ARM64 mini-JIT vec128 bring-up now has a 16-byte aligned slot-layout table,
  local type tracking, helper-backed vec128 local/context/memory load-store,
  constant vector operands, vector shifts, unpack, pack, insert, extract,
  splat, permute, swizzle, dot3/dot4, vector conversions, vector compare,
  vector min/max, vector add/sub, vector average, vec128 select, vec128 unary
  operations, and vec128 mul-add/sub.
- As of `docs/research/20260518-134832-arm64-jit-gap-device-checkpoint.md`,
  all 113 HIR opcodes have ARM64 mini-JIT switch coverage and interpreter
  switch coverage. This does not mean the backend is fast or fully correct; it
  means the current blocker is no longer a missing switch-case surface.
- ARM64 mini-JIT research cvars currently include:
  - `arm64_enable_mini_jit`
  - `arm64_mini_jit_blacklist`
  - `arm64_force_interpreter_guest_ranges`
  - `arm64_mini_jit_max_stack_bytes`
  - `arm64_jit_code_cache_mode`
  - `arm64_jit_code_cache_mb`
- The KTHREAD timer wall at guest function `8246B408` has been identified and
  moved: `arm64_update_kthread_time` writes guest uptime to the current
  KTHREAD `+0x58`, and the draw wait `global_tick` now advances.
- The command processor now mirrors guest-visible ring pointers into
  `CP_RB_RPTR` / `CP_RB_WPTR` (`0x01C4` / `0x01C5`). Blue Dragon's D3D dump now
  shows matching drained ring pointers instead of a stale zero read pointer.
- Current blocker: Blue Dragon still does not visibly reach title. The wall is
  now the guest D3D watchdog path:
  `The GPU is hung! D3D version is 3529.0 retail, kernel is 65535, frame is 0`.
- Focused PPC dumps show the graphics interrupt callback at `8246DBB0` and draw
  wait function `8246B408`; token-kick experiments prove token movement alone
  does not satisfy the game.
- Runtime swap tracing reaches engine and ring initialization, but does not show
  runtime `GPU swap trace: VdSwap`, PM4 `XE_SWAP`, or Vulkan `IssueSwap`.
  `VdSwap` in import/symbol listings is not runtime call proof.
- The visible OSD badge now reports `AArch64 mini-JIT research` instead of the
  stale `AArch64 JIT pending` text.
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
- Use Thor runs as milestone checkpoints after broad backend batches, not as
  the only unit test.
- Current conversion audit:
  `docs/research/20260518-140436-arm64-x64-conversion-audit.md`.

## ARM64 Fork Audit Decision

- Latest audit: `docs/research/20260517-183520-xenia-arm64-fork-audit.md`.
- Primary AArch64 JIT source to study/port: `has207/xenia-edge`, branch `edge`, because it has the most current xbyak_aarch64 A64 backend shape, CMake wiring, POSIX code cache, and broad HIR opcode table coverage.
- Primary Android app-side reference: `aenu1/ax360e`, branch `main`, because it shows a real Android package with nested Xenia Canary, A64 backend, Android CMake glue, SAF/document-file access, Android HID, Adreno options, and `libadrenotools`.
- AArch64 assembler dependency: `third_party/xbyak_aarch64` is pinned to Fujitsu `xbyak_aarch64` commit `59c6a7e7368cd2dbb07c46abe83239ace4d58849`, matching the `has207/xenia-edge` submodule gitlink observed during the audit. License: Apache-2.0.
- Secondary provenance and portability references: `wmarti/xenia-mac`, `Wunkolo/xenia`, and `joetheshmoe/xenia-winarm`.
- Treat `edilsongg/xenia-arm64` and `fillips67fissile/xenia-arm64-backend` as Wunkolo duplicates unless a future diff proves unique work.
- Do not import from `SbossTreeSitty`, `Xenia-creator2025/Xenia-emulator-android`, or the `ProyectoXenia` repos for JIT work.
- Before copying any third-party code, verify the specific file license, preserve attribution, and keep this fork clearly marked as unofficial research.

## Android ARM64 Risk Register

- CPU backend: the AArch64 path now has a tiny helper-backed mini-JIT, but much
  of it is correctness-first helper calls rather than optimized native code.
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
