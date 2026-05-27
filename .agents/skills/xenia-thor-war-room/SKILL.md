---
name: xenia-thor-war-room
description: Coordinate repo-local Android usability, ADB remote debugging, Xbox 360 PowerPC, AArch64 Snapdragon JIT, Vulkan/Adreno, Ghidra, RenderDoc, speed profiling, and experiment-gate lanes for the experimental xenia-thor bring-up. Use for app-picker crashes, bad OSD/input/scaling, unclear game crashes, black screens, watchdogs, slow one-at-a-time debugging, or when multiple expert perspectives must be combined before editing code.
---

# Xenia Thor War Room

This is the default repo-local coordination skill for xenia-thor. It is not a
replacement for careful code reading; it is the triage ritual that keeps the
work from becoming one random crash at a time.

## Ground Rules

- This fork is experimental, unofficial, and unsupported by upstream Xenia.
- AYN Thor / Thor Max ARM64 is the priority target; Windows is a control lane only
  when it answers a specific semantics question.
- Work on the current branch unless the user changes the policy.
- Commit and push often after validated slices.
- Every session updates `docs/worklogs/YYYYMMDD.md`.
- Research goes in `docs/research/YYYYMMDD-HHMMSS-topic.md`.
- Do not commit game files, keys, extracted XEX files, private screenshots, or
  copyrighted content.
- Use only the user's legal local test content for device runs.
- Keep Blue Dragon speed work paused unless the user explicitly restarts it.

## First Five Minutes

1. Read the latest section of `AGENTS.md`.
2. Check `git status --short --branch`.
3. If the user says "check now" or reports a fresh visual issue, capture the
   current screen/log before clearing logcat or force-stopping anything.
4. Find the newest relevant `scratch/thor-debug/*-logcat.txt`, remote-debug
   packet, and matching metadata.
5. Classify the current wall before editing:
   - Guest PPC / XEX / kernel export.
   - A64 JIT / thunk / code cache / signal handling.
   - GPU command processor / PM4 / Vulkan / Adreno / present.
   - Android shell / SAF / UI / permissions / ADB transport.
6. Pick exactly one primary lane and one secondary lane for the next run.

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
- Use `.agents/skills/xenia-a64-speed-hotpath/SKILL.md` when the evidence is
  low FPS, A64 speed-profile counters, hot guest PCs, helper prologue/epilogue
  churn, direct/indirect call overhead, XMA cost, or CPU/GPU split questions.
- Use `.agents/skills/xenia-thor-experiment-gate/SKILL.md` before risky code
  changes, debug cvars, title-specific probes, or repeated hypotheses.
- Use `.agents/skills/xenia-thor-remote-debug/SKILL.md` for live Thor testing,
  screenshots/videos/UI dumps, crash bundles, scrcpy or Android Studio
  mirroring, and app-picker/controller/OSD evidence.
- Use `.agents/skills/xenia-blue-dragon-route-capture/SKILL.md` for loading
  Blue Dragon, route labels, nop HID START/A sequencing, screenshots, and
  title/opening proof only when Blue Dragon speed is explicitly active.
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
2. Run the experiment gate if the next move changes behavior or toggles a
   risky probe.
3. Decide which expert lane owns it.
4. Read that skill, then inspect only the relevant files/log ranges.
5. Prefer broad parity or invariant fixes over title hacks.
6. Add instrumentation only when it answers a specific next question.
7. Build the narrowest lane:
   - APK/UI only: `tools\thor\thor_build.ps1 -Mode ApkShell`
   - native only: `tools\thor\thor_build.ps1 -Mode NativeCore`
   - release to Thor: `tools\thor\thor_build.ps1 -Mode FullDeploy`
8. Launch with scripted cvars, capture, classify, and update the worklog.

## Android App Usability Run

Use this for current launcher, controller, OSD, scaling, and crash repro work:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchLauncher
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenshot
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenrecord -Seconds 30
```

Use `-Mode CrashBundle` after a reproducible crash, not for every exploratory
tap.

## Blue Dragon Speed Run

Blue Dragon speed is paused by default. Use this only after the user restarts
that sprint.

Use this for current "2 FPS" and hotpath work:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 100 -PerfSampleSeconds "45,90" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1
```

## Blue Dragon Trace Run

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
