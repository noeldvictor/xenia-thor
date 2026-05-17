# Thor Max Android ARM64 Port Research

Accessed: 2026-05-17 12:41:17 -04:00

## Summary

The AYN Thor Max target is a native Android ARM64 handheld with Snapdragon 8 Gen 2 and Adreno 740. The current repo already contains Android/NDK/Vulkan scaffolding, but it does not yet contain a functional Android emulator app path or an AArch64 CPU backend. The key work is not "turn on ARM64" only; it is CPU backend creation, Android app wiring, Vulkan-on-Adreno validation, input/audio implementation, and executable-memory correctness on Android.

## Device Findings

Official AYN product page:

- Thor is a dual-screen Android gaming handheld.
- Max variants are listed as `8Gen2 16+512GB (UFS3.1)` and `8Gen2 16+1TB (UFS3.1)`.
- Product page lists Android 13, 6000 mAh battery, Hall sticks, 6 inch primary AMOLED touch screen, and 3.92 inch bottom AMOLED touch display.
- Source: https://www.ayntec.com/products/ayn-thor

Supporting public coverage:

- Android Central and HotHardware reported the Thor Base/Pro/Max models use Snapdragon 8 Gen 2, with Lite using Snapdragon 865.
- Retro Handhelds setup guide states Base, Pro, and Max share Snapdragon 8 Gen 2 and differ mainly by memory/storage.
- Sources:
  - https://www.androidcentral.com/gaming/android-games/ayn-announces-dual-screen-thor-and-next-gen-odin-3-handhelds
  - https://hothardware.com/news/ayn-thor-dual-screen-handheld-oled-and-snapdragon
  - https://retrohandhelds.gg/ayn-thor-setup-guide/

## Snapdragon / Vulkan Findings

Qualcomm's Snapdragon 8 Gen 2 product page lists:

- CPU architecture: 64-bit.
- GPU: Qualcomm Adreno.
- Graphics APIs: Vulkan 1.3, OpenGL ES 3.2, OpenCL 2.0 FP.
- Source: https://www.qualcomm.com/smartphones/products/8-series/snapdragon-8-gen-2-mobile-platform

Android developer documentation says:

- Vulkan is available on Android from Android 7.0 / API level 24.
- Android platform support by version is 1.0.3 on API 24+, 1.1 on API 29+, and 1.3 on API 33+, but actual device driver support must still be queried at runtime.
- Source: https://developer.android.com/games/develop/vulkan/native-engine-support

Implication:

- The repo's `minSdkVersion 24` is coherent for Vulkan availability, but Thor Max runtime probing still matters. Android 13 plus Snapdragon 8 Gen 2 suggests Vulkan 1.3 is plausible, but agents must trust device `vkEnumerateInstanceVersion` and `VkPhysicalDeviceProperties`, not assumptions.

## Android ABI Findings

Android NDK ABI documentation says:

- `arm64-v8a` is the Android ABI for 64-bit ARM CPUs.
- `arm64-v8a` supports AArch64.
- Gradle `abiFilters` can restrict supported ABIs.
- Source: https://developer.android.com/ndk/guides/abis

Repo state:

- `android/android_studio_project/app/build.gradle` already includes `abiFilters 'arm64-v8a', 'x86_64'`.
- Premake declares `Android-ARM64` and `Android-x86_64` platforms.
- At clone time, `src/xenia/cpu/backend` contained only `x64` plus null backend scaffolding. A non-functional ARM64 scaffold was added later in this session to create an honest selectable backend path.

Implication:

- The build system knows about Android ARM64, but guest CPU execution cannot work until an AArch64 backend exists.

## Android JIT / Executable Memory Findings

Android NDK memory docs state:

- `ASharedMemory_create` is available since API 26.
- The resulting file descriptor can be mapped with `PROT_READ | PROT_WRITE | PROT_EXEC`.
- Source: https://developer.android.com/ndk/reference/group/memory

Repo state:

- `src/xenia/base/memory_posix.cc` already dynamically loads `ASharedMemory_create` on Android API 26+ and falls back to ashmem below API 26.
- `src/xenia/base/memory.cc` already has `XE_ARCH_ARM64` guarded inclusion of `<arm_neon.h>`.
- No ARM64 instruction-cache flush path was found in the first scan.

Implication:

- AArch64 JIT implementation must explicitly test write/protect/execute transitions and instruction cache coherency on Thor Max. Expect to need `__builtin___clear_cache` or an equivalent platform path after code emission.

## AArch64 Emitter Options

AsmJit:

- Current docs list X86/X64 and AArch64 as supported backends.
- The AArch64 backend includes baseline instructions and ASIMD extensions.
- Source: https://asmjit.com/
- Source: https://asmjit.com/doc/group__asmjit__build.html

VIXL:

- AArch64 runtime code generation library with assembler, disassembler, and simulator components.
- Android's source tree has carried VIXL under `platform/external/vixl`.
- Source: https://android.googlesource.com/platform/external/vixl/

Initial hypothesis:

- AsmJit deserves a serious look because it currently documents AArch64 support, has a C++ API, and may map better to a new backend than hand-rolled encoding.
- VIXL deserves comparison because it has Android/runtime-code-generation history.
- Do not vendor either until a focused research note compares licensing, binary size, Android build integration, AArch64 feature coverage, cache flush expectations, and maintenance risk.

## Repo-Specific Findings

Important files:

- At clone time, `premake5.lua` included `src/xenia/cpu/backend/x64` and had no ARM64 backend project. This session added an ARM64 backend scaffold.
- At clone time, `src/xenia/app/premake5.lua` linked `xenia-cpu-backend-x64` only under `architecture:x86_64`, and excluded `xenia_main.cc` from Android with a TODO. This session added an ARM64 backend link filter.
- At clone time, `src/xenia/emulator.cc` only instantiated `X64Backend` under `XE_ARCH_AMD64`. This session added `Arm64Backend` selection under `XE_ARCH_ARM64`.
- At clone time, `src/xenia/cpu/cpu_flags.cc` defined CPU options as `[any, x64]`. This session added `arm64` to the flag text.
- `android/android_studio_project/app/build.gradle` uses Gradle/NDK external native build with `xenia.Application.mk`.
- `android/android_studio_project/app/src/main/AndroidManifest.xml` requires Vulkan feature/version.
- `LauncherActivity.java` currently launches GPU trace viewer and window demo, not games.

## Confidence

- High confidence: Thor Max uses Snapdragon 8 Gen 2, Adreno-class GPU, Android, ARM64, and 16 GB RAM in Max variants.
- High confidence: current repo lacks a real AArch64 CPU backend.
- Medium confidence: current Android app scaffolding can be used as the base for a real launcher after build issues are handled.
- Medium confidence: AsmJit or VIXL are the leading AArch64 emitter candidates.
- Unknown until device testing: exact Vulkan driver version, writable/executable mapping behavior, thermal ceiling, secondary screen integration behavior, and controller input event layout.

## Next Research Questions

- Which AArch64 emitter has the least risky Android NDK integration for this codebase?
- What is the cleanest minimal ARM64 backend skeleton that keeps test utilities compiling?
- How should generated code memory be flushed and protected on Android 13/Thor Max?
- What exact Android input events do Thor controls emit?
- Does the second screen appear to Android as a second display, a fixed system surface, or vendor-managed multitasking only?
