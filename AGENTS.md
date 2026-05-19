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
- Use `tools/thor/thor_xenia_debug.ps1` for repeatable ADB install, launch, and capture.
- `tools/thor/thor_xenia_debug.ps1` now retries flaky ADB transports for the
  known Thor serial and records reconnect events in capture metadata.
- `tools/thor/thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture` launches
  Blue Dragon, keeps live logcat open during the run, writes filtered logcat,
  metadata, and a PNG screenshot, and is preferred over dump-afterward logcat
  for early GPU/presenter probes.
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
  `FindContent`, `LaunchBlueDragon`, `LaunchBlueDragonLiveCapture`,
  `LaunchEmulator`, `LaunchWindowDemo`, and `Capture`.
- Always clear logcat before a launch and capture a full log, filtered log, screenshot, metadata file, APK hash, branch, commit, process id, focused activity, and target path.
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
  - `-Arm64BlueDragonDrawWaitProbe true` to log the current Blue Dragon draw
    wait state.
  - `-XboxkrnlThreadWaitTrace true` and `-XboxkrnlEventTrace true` for kernel
    wait/event proof.
  - `-ClearMemoryPageState true` for Canary-style GPU shared-memory page-state
    experiments.
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
- Known diagnostic forced-present probe: `1D88F000+00385000`, `720x720`, pitch
  `1280`, format `6` can turn the surface white on Thor. It is proof the
  presenter can show resolve memory, not proof the game reached title.
- Use `StopNoise` before game runs if another emulator or graphics app is stealing focus or polluting logcat.
- Use the default Blue Dragon path only for the user's local Thor SD card. Do not assume other machines or devices have the same mount UUID.
- Keep Blue Dragon attempts honest: until ARM64 JIT exists, guest code may execute slowly in the interpreter scaffold, but the expected result is still not a playable game.
- For native Android crashes, analyze the unstripped `arm64-v8a/libxenia-app.so` from `android/android_studio_project/app/build/intermediates/ndkBuild/githubDebug/obj/local/arm64-v8a/`.
- For guest-code static analysis, use Ghidra only on legally owned and locally extracted/decrypted code. Do not commit game binaries, extracted XEX files, ISO contents, keys, or private screenshots.
- Ghidra tracks:
  - Native ARM64: `libxenia-app.so`, JNI bridge, Vulkan path, ARM64 backend, crashes, and symbolized stack traces.
  - Guest PPC: Xbox 360/Xenon PowerPC disassembly, XEX metadata, entry points, import thunks, and functions implicated by Xenia logs.
- Create or update local Codex skills when a debug loop repeats. Validate skill folders with `quick_validate.py` and keep skill bodies concise.
- Current global skills to use:
  - `$ayn-thor-xenia-debug` for build/install/ADB capture loops.
  - `$xenia-ghidra-android-debug` for Ghidra native/guest analysis planning and headless imports.
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
  `scratch\thor-debug\20260518-220838-*` and
  `scratch\thor-debug\20260518-221334-*`.
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
- Current blocker: Blue Dragon still does not visibly reach title. The latest
  long run stayed alive for 180 seconds with thousands of `VdSwap` / `XE_SWAP`
  lines, no fatal/AndroidRuntime/GPU-hung lines, and no ARM64 fallback lines.
  The official `VdSwap` frontbuffers `1CA1C000` / `1CDB4000` are zero at swap,
  while Vulkan `IssueCopy` produces nonzero resolve candidates.
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
