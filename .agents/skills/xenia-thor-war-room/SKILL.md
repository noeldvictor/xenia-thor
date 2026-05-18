---
name: xenia-thor-war-room
description: Coordinate repo-local Xbox 360 PowerPC, AArch64 Snapdragon JIT, Vulkan/Adreno, Android, Ghidra, RenderDoc, and ADB evidence lanes for the experimental xenia-thor Blue Dragon bring-up. Use for broad "get Blue Dragon farther" tasks, unclear crashes, black screens, watchdogs, slow one-at-a-time debugging, or when multiple expert perspectives must be combined before editing code.
---

# Xenia Thor War Room

This is the default repo-local coordination skill for xenia-thor. It is not a
replacement for careful code reading; it is the triage ritual that keeps the
work from becoming one random crash at a time.

## Ground Rules

- This fork is experimental, unofficial, and unsupported by upstream Xenia.
- Work on `master` only unless the user changes the policy.
- Commit and push often after validated slices.
- Every session updates `docs/worklogs/YYYYMMDD.md`.
- Research goes in `docs/research/YYYYMMDD-HHMMSS-topic.md`.
- Do not commit game files, keys, extracted XEX files, private screenshots, or
  copyrighted content.
- Use the user's legal local Blue Dragon path only for device runs.

## First Five Minutes

1. Read the latest section of `AGENTS.md`.
2. Check `git status --short --branch`.
3. Find the newest `scratch/thor-debug/*-logcat.txt` and matching metadata.
4. Classify the current wall before editing:
   - Guest PPC / XEX / kernel export.
   - A64 JIT / thunk / code cache / signal handling.
   - GPU command processor / PM4 / Vulkan / Adreno / present.
   - Android shell / SAF / UI / permissions / ADB transport.
5. Pick exactly one primary lane and one secondary lane for the next run.

## Expert Lanes

- Use `.agents/skills/xbox360-xenon-ppc-architect/SKILL.md` when the evidence is
  guest function PCs, XEX imports, DbgPrint text, D3D watchdog logic, wait loops,
  kernel shim semantics, or Ghidra guest PPC analysis.
- Use `.agents/skills/aarch64-snapdragon-jit-port/SKILL.md` when the evidence is
  A64 generated code, host crashes, code-cache addresses, missing HIR lowering,
  thunk failures, Android memory mapping, or x64-to-A64 parity gaps.
- Use `.agents/skills/xenia-vulkan-adreno-renderdoc/SKILL.md` when the evidence
  is PM4 packets, ring pointers, writebacks, `VdSwap`, `PM4_XE_SWAP`,
  `Vulkan IssueSwap`, black frames after draws, or RenderDoc/AGI planning.
- Use `$ayn-thor-xenia-debug` for build/install/capture mechanics.
- Use `$xenia-ghidra-android-debug` for Ghidra headless imports and native or
  guest static-analysis setup.

## Evidence Contract

Do not claim progress without:

- commit hash or dirty-state note,
- APK hash when device-tested,
- launch command and cvars,
- logcat capture path,
- screenshot path or reason screenshot is useless,
- what changed from the previous capture.

## War-Room Loop

1. Summarize the newest signal in one sentence.
2. Decide which expert lane owns it.
3. Read that skill, then inspect only the relevant files/log ranges.
4. Prefer broad parity or invariant fixes over title hacks.
5. Add instrumentation only when it answers a specific next question.
6. Build the narrowest lane:
   - APK/UI only: `tools\thor\thor_build.ps1 -Mode ApkShell`
   - native only: `tools\thor\thor_build.ps1 -Mode NativeCore`
   - release to Thor: `tools\thor\thor_build.ps1 -Mode FullDeploy`
7. Launch with scripted cvars, capture, classify, and update the worklog.

## Blue Dragon Default Run

Use this shape unless a specific experiment needs different flags:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragon -DeviceSerial c3ca0370 -EmitInlineMmioChecks true -GpuTraceSwap true -GpuTracePacketBudget 260 -GpuInterruptOnRingIdle false -GpuBlueDragonKickWaitToken false
```

Then capture:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode Capture -DeviceSerial c3ca0370
```

## Decision Bias

- If the log shows guest watchdog text, inspect guest PPC and GPU completion
  semantics together before forcing interrupts.
- If the log shows a native signal in code cache, inspect A64 thunk/exception
  evidence before changing kernel or GPU code.
- If PM4 packets are zero or stale, inspect Android memory aliasing before
  adding GPU hacks.
- If real PM4 draws happen but screen stays black, inspect `VdSwap`,
  `PM4_XE_SWAP`, texture-cache swap path, and presenter callbacks before more
  PPC work.
- If the loop feels slow, create a script or repo-local skill rather than
  relying on memory.
