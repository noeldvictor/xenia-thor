---
name: aarch64-snapdragon-jit-port
description: Port and debug Xenia's x64/Edge/aX360e CPU backend behavior to Android AArch64 on Snapdragon 8 Gen 2, including ARM64 ABI, code cache, generated thunks, xbyak_aarch64, HIR lowering, memory aliases, signal recovery, FP/vector state, and differential parity against x64 and donor ARM64 forks.
---

# AArch64 Snapdragon JIT Port

Use this skill for the host CPU backend lane. The goal is not merely to make
the donor A64 backend compile; it must become correct and fast enough on Thor.
AYN Thor Max ARM64 is the priority; Windows/x64 is a parity reference only when
it answers a specific backend semantics question.

## Strategy

Use both sources of truth:

- Port proven behavior from `src/xenia/cpu/backend/x64`.
- Import or adapt cleanly licensed, attributed patterns from aX360e and
  xenia-edge when they are better suited to Android AArch64.

Do not resurrect the removed helper mini-JIT unless the user explicitly asks for
an archaeology comparison.

## First Checks

```powershell
git status --short --branch
rg -n "Fatal signal|SIGILL|SIGSEGV|SIGTRAP|A64 backend trap|no mapped guest function|Host PC|Guest registers|unimplemented|assertion" scratch\thor-debug\*-logcat.txt
powershell -ExecutionPolicy Bypass -File tools\arm64\arm64_conversion_audit.ps1 -LatestLogs 8
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 100 -PerfSampleSeconds "45,90" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1
```

For code changes, inspect the matching x64 implementation first:

- `src/xenia/cpu/backend/x64/`
- `src/xenia/cpu/backend/a64/`
- `src/xenia/cpu/backend/code_cache_base.h`
- `src/xenia/cpu/backend/backend.h`
- `src/xenia/cpu/thread_state.*`
- `src/xenia/memory.cc`
- `src/xenia/base/memory_posix.cc`

## A64 Risk Areas

- Android executable memory policy, page size, and instruction-cache flushing.
- Guest physical/virtual aliasing through POSIX shared mappings.
- Code-cache commit growth; never remap over existing generated code on POSIX.
- Host-to-guest, guest-to-host, resolve thunks, trampolines, and indirection.
- PPC context layout, backend prefix, stackpoints, FPCR/FPSCR, and vector state.
- Signal/exception recovery for generated code and BRK guards.
- Atomics, reservations, memory ordering, endian loads/stores, and MMIO checks.
- HIR scalar, vector, pack/unpack, compare, branch, and call lowering parity.

## Current Speed Signals

- Use `.agents/skills/xenia-a64-speed-hotpath/SKILL.md` for profiled speed
  runs before guessing at hot A64 changes.
- Treat `__savegprlr_*` and `__restgprlr_*` dominating the A64 speed profile as
  a call/ABI helper overhead problem until guest PPC evidence proves otherwise.
- Treat high direct-call and entry deltas as a dispatch/direct-linking problem
  before jumping to GPU work.
- Treat high extern or XMA thread cost as HLE/audio cost to measure with
  explicit A/B probes.
- Do not use trace-heavy GPU or disassembly runs for speed claims.

## Donor Import Rules

- Before copying code, identify source repo, branch/commit, file paths, and
  license in a dated research note.
- Prefer small slices with build boundaries over wholesale blind imports.
- Preserve attribution comments already present in donor files.
- Keep compatibility shims local and documented.
- After import, validate Android `arm64-v8a` and do not assume x86_64 still
  builds without checking.

## Snapdragon / Android Notes

- Target is AYN Thor Max: Snapdragon 8 Gen 2, Android 13, Adreno 740.
- Keep frame pointers and unwind tables in debug builds.
- Treat disconnecting ADB as expected; use the repo Thor scripts.
- Snapdragon speed work starts after correctness proof: first title screen, then
  hotspot traces, then register allocation/vector/native lowering.

## Validation Ladder

1. `tools\thor\thor_build.ps1 -Mode NativeCore -DeviceSerial c3ca0370`
2. `tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`
3. Blue Dragon launch with focused A64 flags or the canonical speed profile.
4. Log search for native signals, unmapped code-cache PCs, and A64 profile
   counters.
5. Compare against the previous Thor capture and update the worklog.

## Output

End with:

- host backend subsystem touched,
- x64 or donor source compared,
- parity gap closed or discovered,
- validation command and capture path,
- next broad conversion batch before another Thor-only fix.
