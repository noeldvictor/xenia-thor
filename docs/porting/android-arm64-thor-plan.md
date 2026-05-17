# Android ARM64 Thor Max Port Plan

Created: 2026-05-17 12:42:57 -04:00

## Goal

Build a native Android ARM64 Xenia APK for the AYN Thor Max, then move from build bring-up to real PowerPC guest execution and Thor Max performance tuning. The user-facing milestone is Vulkan game execution on Thor Max, with Blue Dragon as the likely first legally owned test title.

## Target Profile

- Hardware: AYN Thor Max.
- SoC: Snapdragon 8 Gen 2.
- GPU: Adreno 740.
- Memory/storage: Max variants are 16 GB RAM with 512 GB or 1 TB UFS 3.1.
- OS baseline from AYN listing: Android 13.
- Display reality: top 6 inch AMOLED primary display and secondary 3.92 inch AMOLED touch display.
- Repo build baseline: Android API 24 minimum, SDK 33 target, NDK 25.0.8775105.

Observed device baseline on 2026-05-17:

- ADB serial `c3ca0370` reported `AYN Thor`, Android 13 / SDK 33, `kalama`, Adreno 740.
- Vulkan window demo rendered on device through `jp.xenia.emulator.WindowDemoActivity`.
- Xenia logged Vulkan instance 1.3.0, Adreno 740 device API 1.3.128, Qualcomm proprietary driver, and 1920x970 swapchain creation.

## Milestone 0: Baseline And Logs

- Initialize submodules.
- Generate Android build files.
- Attempt Gradle debug APK build.
- Save full command output summary in `docs/worklogs/YYYYMMDD.md`.
- Record exact failures rather than papering over them.

Exit criteria:

- A dated worklog has exact commands and first failure list.
- Generated build artifacts are understood well enough to avoid committing noisy generated files unless intentionally required.

## Milestone 1: Android Shell Bring-Up

- Preserve existing `WindowDemoActivity` and `GpuTraceViewerActivity` smoke tests.
- Confirm `libxenia-app.so` builds for `arm64-v8a`.
- Add or enable an Android activity path for the emulator app.
- Add Android document picker flow for `.xex`, `.iso`, and trace files.

Exit criteria:

- APK installs and opens on Thor Max.
- Vulkan window demo renders.
- Launcher can pass selected content URI/path intent data to native code.

Status 2026-05-17:

- APK install succeeded.
- Vulkan window demo rendered on Thor Max.
- Launcher now exposes `LAUNCH GAME`, `GPU TRACE VIEWER`, and `WINDOW DEMO`.
- Game launch passes selected document URIs to native code with `gpu=vulkan`, `cpu=arm64`, `apu=nop`, `hid=nop`, and `discord=false`.
- Direct ADB developer launches can pass top-level extras to `EmulatorActivity`; Java repackages them into the Bundle expected by native cvar parsing.
- Emulator launch reaches the intentional ARM64 JIT-not-implemented error instead of crashing.

## Milestone 2: ARM64 CPU Backend Skeleton

- Maintain `src/xenia/cpu/backend/arm64`.
- Maintain Premake inclusion and architecture filters for `Android-ARM64`.
- Keep `cpu=arm64` option and clean runtime errors for unimplemented execution.
- Confirm non-emulator utilities still build.

Exit criteria:

- Android ARM64 build no longer depends on x64 backend symbols.
- Emulator setup can choose an ARM64 backend class.
- Attempting to execute guest code fails with a precise "backend incomplete" error, not a crash.

Initial scaffold added 2026-05-17:

- `src/xenia/cpu/backend/arm64/arm64_backend.*`
- `src/xenia/cpu/backend/arm64/premake5.lua`
- `cpu=arm64` flag text.
- `Emulator::Setup` ARM64 selection behind `XE_ARCH_ARM64`.

## Milestone 3: AArch64 JIT Prototype

- Choose AArch64 emitter library after a dated research note.
- Implement code cache allocation, write/protect/execute transition, and instruction cache flush.
- Port minimal backend thunks and call ABI handling.
- Translate a tiny subset of HIR needed by existing CPU tests or a micro homebrew `.xex`.

Exit criteria:

- Host AArch64 generated code can be emitted, protected, flushed, and called on-device.
- A minimal translated function returns expected values.
- CPU test path can run at least one controlled case on Android/ARM64.

User approval:

- The user approved adding an external AArch64 emitter dependency such as AsmJit or VIXL if research supports it.
- Initial research recommendation is VIXL first, with AsmJit as fallback. See `docs/research/20260517-142224-arm64-jit-emitter-asmjit-vixl.md`.

## Milestone 4: Vulkan/Adreno Performance Track

- Capture Thor Max Vulkan feature dump.
- Identify render path features used by Xenia that are risky on Adreno.
- Compare `gpu=vulkan` trace viewer behavior against desktop.
- Add logging for pipeline creation, shader translation stalls, frame pacing, and thermal observations.

Exit criteria:

- A repeatable trace-viewer test exists.
- At least one GPU trace or simple content path has a baseline FPS/frametime note.
- Performance experiments list correctness checks and rollback conditions.

## Milestone 5: Controls, Audio, And Handheld UX

- Add Android gamepad/input mapping for Thor controls.
- Add Android audio backend or validated low-latency path.
- Add landscape/fullscreen handling for the top display.
- Defer secondary display strategy until the primary launch path is stable.

Exit criteria:

- Thor Max controls map to XInput-like guest input.
- Audio is not nop in normal emulator runs.
- Orientation and surface lifecycle survive pause/resume.

## Recommended First Build Commands

```powershell
git submodule update --init --recursive
python xenia-build setup --target_os=android
python xenia-build premake --target_os=android
cd android/android_studio_project
.\gradlew.bat :app:assembleGithubDebug
```

Known Windows path issue:

- Running Gradle directly from `C:\Users\leanerdesigner\Documents\New project 8\xenia-thor` fails during `:app:configureNdkBuildDebug[arm64-v8a]` because `ndk-build` reports `build\xenia.wks.Android.mk` as unknown.
- The file exists; the failure is caused by path handling with spaces in the workspace path.
- Workaround verified 2026-05-17: map the repo to a no-space drive with `subst X: "<repo>"`, run Gradle from `X:\android\android_studio_project`, then remove the mapping.

## First Files To Inspect During Implementation

- `premake5.lua`
- `src/xenia/app/premake5.lua`
- `src/xenia/emulator.cc`
- `src/xenia/cpu/cpu_flags.cc`
- `src/xenia/cpu/backend/x64/*`
- `src/xenia/cpu/backend/arm64/*`
- `src/xenia/base/memory_posix.cc`
- `src/xenia/base/memory.cc`
- `src/xenia/ui/windowed_app_context_android.*`
- `android/android_studio_project/app/build.gradle`
- `android/android_studio_project/app/src/main/AndroidManifest.xml`
- `android/android_studio_project/app/src/main/java/jp/xenia/emulator/*.java`

## Decision Points

- Keep NDK r25 for initial bring-up or upgrade after baseline?
- Use AsmJit, VIXL, or a local emitter for AArch64?
- Build a temporary interpreter/debug backend before full JIT, or go straight to JIT?
- Keep x86_64 Android builds as a comparison target, or remove them from Thor-focused CI?
- Which first legal test title or homebrew sample should define the first functional milestone?

Current answer:

- Blue Dragon is the likely first retail target, but a homebrew/test `.xex` should still be used as the smallest CPU bring-up proof before attempting Blue Dragon.
- Use VIXL first for an AArch64 emitter spike; keep AsmJit as fallback if integration or instruction coverage becomes a blocker.
- The first Android UX milestone is now the game picker plus direct ADB launch path; the next blocker is actual AArch64 guest execution.
