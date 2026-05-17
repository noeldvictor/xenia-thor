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
- The only functional CPU backend present is `src/xenia/cpu/backend/x64`.
- `src/xenia/cpu/backend/arm64` is a scaffold only; it is not a working AArch64 JIT.
- `src/xenia/emulator.cc` can select the ARM64 scaffold on `XE_ARCH_ARM64`, but initialization intentionally fails until the JIT exists.
- `src/xenia/cpu/cpu_flags.cc` advertises `any`, `x64`, and `arm64`.
- `src/xenia/app/premake5.lua` includes `xenia_main.cc` for Android and skips the HID demo in the Android single-library bundle.
- Android currently has Java/native shell pieces for windowed apps, a launcher, a game activity, a window demo, and the Vulkan trace viewer.
- On 2026-05-17, `app-github-debug.apk` installed on ADB device `c3ca0370` and `WindowDemoActivity` rendered the Vulkan ImGui demo on Adreno 740.
- On 2026-05-17, the launcher showed `LAUNCH GAME`, `GPU TRACE VIEWER`, and `WINDOW DEMO`; launching the emulator path reached the intended ARM64 JIT-not-implemented blocker rather than a Java/native crash.
- Use `tools/thor/thor_build.ps1` to split build lanes:
  - `FullApk`: native core plus APK packaging.
  - `NativeCore`: C++/Vulkan/CPU backend library only.
  - `ApkShell`: Java/XML/resources/manifest package only, using the previous native library.
- Use `tools/thor/thor_xenia_debug.ps1` for repeatable ADB install, launch, and capture.
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
- Keep ARM64 backend scaffolding honest: it may compile, but it must fail clearly at runtime until implemented.
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
- Use `StopNoise` before game runs if another emulator or graphics app is stealing focus or polluting logcat.
- Use the default Blue Dragon path only for the user's local Thor SD card. Do not assume other machines or devices have the same mount UUID.
- Keep the first Blue Dragon attempts honest: until ARM64 JIT exists, the expected result is the explicit AArch64 backend blocker.
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

## Android ARM64 Risk Register

- CPU backend: only an AArch64 scaffold exists; no instruction emitter, code cache, or guest execution exists yet.
- JIT memory: Android executable memory and cache coherency must be tested on device.
- Guest memory layout: verify fixed mappings and any 32-bit guest assumptions on Android.
- Vulkan: the manifest requires Vulkan, but runtime feature probing still needs Thor Max logs.
- Input: Android currently falls back to nop HID for emulator app paths; real controls need mapping.
- Audio: Android currently excludes SDL audio and falls back to nop audio unless an Android audio path is added.
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
