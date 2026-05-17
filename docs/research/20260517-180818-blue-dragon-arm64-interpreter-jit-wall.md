# 2026-05-17 18:08 - Blue Dragon ARM64 Interpreter/JIT Wall

## Context

Blue Dragon Disc 1 was launched on the AYN Thor Max from:

`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

This note records the state after adding ARM64 interpreter support for scalar
and vector min/max, PPC FPSCR-driven dynamic rounding, HIR `round`, and
`set_rounding_mode`.

## Evidence

Device:

- ADB serial: `c3ca0370`
- App package: `jp.xenia.emulator.github.debug`
- Branch: `master`
- APK SHA-256 in capture metadata:
  `1ED7245FE008C7BE72B080969D214C4B1C37339B0E4EB70E3634D22651895E5D`

Artifacts:

- `scratch/thor-debug/20260517-180553-logcat.txt`
- `scratch/thor-debug/20260517-180553-logcat-filtered.txt`
- `scratch/thor-debug/20260517-180553-meta.txt`
- `scratch/thor-debug/20260517-180553-screenshot.png`
- Follow-up verification after log-budget and metadata fixes:
  - `scratch/thor-debug/20260517-181152-logcat.txt`
  - `scratch/thor-debug/20260517-181152-logcat-filtered.txt`
  - `scratch/thor-debug/20260517-181152-meta.txt`
  - `scratch/thor-debug/20260517-181152-screenshot.png`

Observed state:

- No new `ARM64 interpreter unimplemented HIR opcode` error appeared in the
  18:05 capture after the rounding patch.
- Android stayed in `EmulatorActivity`, process `11356`.
- The screenshot still shows a static Java OSD warning, `AArch64 JIT pending`.
  That text is not a native crash; it comes from
  `android/android_studio_project/app/src/main/res/values/strings.xml`.
- `adb shell top -H -b -n 1 -p 11356` showed active guest threads, especially:
  - `Main XThread (F...`: about 103% CPU, 1:36 CPU time.
  - `XThread3A212CB0`: about 23% CPU.
  - `XThread3C218CB0`: about 11.5% CPU.
- Later live logcat showed:
  `ARM64 interpreter slow function 826E53A8: step 500000, pc 126, lr 826E53DC, ctr 826E52E8, r3 00000000, r4 7039FE60, r5 00000000`.
- The 18:11 verification metadata correctly preserved the target path after the
  `last-target.txt` capture-script fix.
- The 18:11 filtered log showed repeated slow interpreter hot functions
  `822710D0`, `82285470`, and `82285AB0` without a new missing-opcode crash in
  the short verification window.

## Interpretation

The immediate missing-opcode wall moved forward. Blue Dragon is now running
guest code through the ARM64 HIR interpreter scaffold rather than stopping at
`OPCODE_MIN` or `OPCODE_SET_ROUNDING_MODE`.

The next wall is architectural/performance-related:

- The current ARM64 backend is still an interpreter scaffold, not an AArch64
  JIT/emitter.
- The interpreter can keep guest threads alive but is too slow for the user's
  "Vulkan game running" milestone.
- The current Android OSD is intentionally native Android UI layered over the
  surface, but the warning is static and must become runtime-driven later.

## Decisions

- Continue using the interpreter only as a correctness and HIR-surface discovery
  tool.
- Start the real AArch64 JIT/emitter path next, with VIXL still acceptable per
  user answer.
- Keep Thor captures separated:
  - `NativeCore` for C++/Vulkan/CPU backend changes.
  - `ApkShell` for Java/XML/resources-only changes.
  - `FullDeploy` before device proof.
- Cap HIR program log spam so long-running captures surface late errors, slow
  functions, and GPU/native signals.

## Next Debug Questions

- Is `826E53A8` a hot guest wait loop, scheduler loop, or real game init work?
- Which HIR opcodes dominate the hottest guest functions once logging is
  budgeted?
- Does Ghidra guest PPC analysis of `826E53A8` identify a wait condition that
  can be used as a targeted correctness test while the AArch64 emitter starts?
