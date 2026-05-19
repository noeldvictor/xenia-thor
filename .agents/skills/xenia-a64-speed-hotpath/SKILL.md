---
name: xenia-a64-speed-hotpath
description: Use for Xenia Thor ARM64 performance work on AYN Thor Max, especially Blue Dragon 2 FPS runs, A64 speed-profile counters, hot guest PCs, PPC helper/prologue churn, guest-to-host transitions, direct/indirect call overhead, XMA/audio cost, and Snapdragon 8 Gen 2 profiling. AYN Thor ARM64 is the priority; Windows is only a control lane when it answers a specific semantics question.
---

# Xenia A64 Speed Hotpath

Use this skill when Blue Dragon gets past boot/title/opening but runs too slow,
or when the next question is "CPU or GPU?" on Thor. This is the speed lane for
the imported aX360e/Edge-style A64 backend, not the removed helper mini-JIT.

## Priority

- AYN Thor Max ARM64 truth comes first.
- Windows/x64 is a control microscope only when a suspected emulator semantics
  bug needs a faster comparison.
- Do not spend a speed run on broad GPU traces, shader dumps, disassembly dumps,
  live logcat, or RenderDoc unless the experiment explicitly targets that
  subsystem.
- Keep trace-heavy correctness captures separate from trace-off speed captures.

## Baseline Run

Build and deploy only when the native core changed:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Run the canonical profiled speed capture:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 100 -PerfSampleSeconds "45,90" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1
```

Then summarize the latest evidence:

```powershell
rg -n "A64 speed profile|__savegprlr|__restgprlr|8246B408|8248B040|827294CC|8272A3A4|Fatal signal|AndroidRuntime|VK_ERROR_DEVICE_LOST|GPU is hung|XMA|top_threads" scratch\thor-debug
```

## Classification

Read the final speed-profile interval first.

- If `__savegprlr_*` or `__restgprlr_*` dominate, prove whether they are guest
  ABI helper leaf routines. Candidate work is guest helper inlining, direct
  link cleanup, or cheaper call/return lowering.
- If direct guest calls roughly track function entries, inspect direct block
  chaining and guest function boundary overhead before GPU work.
- If indirect calls are high, inspect indirect branch lookup, hash/cache shape,
  and branch target metadata.
- If guest-to-host or extern calls are high, inspect HLE, kernel waits, XMA,
  file/device I/O, and MMIO paths.
- If resolves or resolve misses are high, inspect function lookup, code-cache
  registration, and invalidation.
- If thread samples show XMA or audio ahead of the guest CPU, use
  `-XmaFastSilence true` only as an A/B cost probe, not as a fix.
- If GPU/composer threads are not hot and the screen is merely slow, stay in
  CPU/A64 until evidence changes.

## Files To Inspect

- `src/xenia/cpu/backend/a64/`
- `src/xenia/cpu/backend/x64/`
- `src/xenia/cpu/hir/`
- `src/xenia/cpu/ppc/`
- `src/xenia/kernel/xboxkrnl/`
- `src/xenia/apu/`
- `tools/thor/thor_xenia_debug.ps1`
- `docs/research/20260519-153016-xbox360-thormax-translation-report.md`
- `docs/research/20260519-162000-a64-speed-profile-counters.md`

## Static Lane

Use Ghidra only after a runtime profile gives a concrete guest PC, helper name,
or guest wait loop. The static question should fit in one sentence, such as:

- "What does guest function `8246B408` wait on?"
- "Are `__savegprlr_29` and `__restgprlr_29` compiler ABI helpers we can
  inline or shortcut safely?"
- "Which caller loop creates this direct-call storm?"

Use `$xenia-ghidra-android-debug` for setup and keep extracted guest content
out of git.

## Acceptance

A speed patch is not a win until the notes include:

- before and after capture directories from the same scene/route;
- commit hash and APK SHA-256;
- profiler cvars and logging mode;
- final A64 counter interval;
- thread sample summary;
- screenshot path or route status;
- whether the run crashed, hung, or changed visible progress;
- next hotpath if the current patch helped only part of the wall.
