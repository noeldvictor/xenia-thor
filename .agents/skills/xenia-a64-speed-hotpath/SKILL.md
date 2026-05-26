---
name: xenia-a64-speed-hotpath
description: Use for Xenia Thor ARM64 performance work on AYN Thor Max when Blue Dragon is CPU/JIT-bound, including A64 backend maturity, guest-state traffic, helper ABI, block linking, VMX128/NEON lowering, fastmem/addressing, and speed-profile evidence.
---

# Xenia A64 Speed Hotpath

Use this when the measured wall is Main Thread / generated A64 code. Keep this
skill procedural; detailed chronology lives in research notes.

## Current Speed Read

No sustained 30 FPS proof exists. Recent Blue Dragon route captures remain
CPU/JIT-bound, with `82282490`, `82281D28`, and `82287788` recurring in
body-time rows while GPU Commands stays well below the main thread. Do not make
Vulkan the main sprint lane until counters show present/submit/pipeline/GPU
work overtaking A64.

## Mature Backend Gate

Before another title-specific micro-fastpath, answer these from source,
profiler, and HIR evidence:

- **Register allocation/state:** are hot guest GPR/FPR/VMX/FPSCR values kept in
  host registers with explicit dirty, spill, helper, call, branch, and external
  visibility rules?
- **Helper ABI:** are calls forcing avoidable stackpoint, FPCR/FPSCR, scratch
  save/restore, or static-register churn?
- **Block linking:** are direct exits, recursive edges, and indirection paths
  linked or patched where safe?
- **Fastmem/addressing:** are hot guest loads/stores using the cheapest legal
  A64 path with slow fallback coverage?
- **VMX/FP lowering:** can a general opcode lowering remove broad NEON work
  while preserving NaN, denormal, FPCR/FPSCR, endian, and VMX128 semantics?
- **Harness:** can the hypothesis be audited offline before a Thor route run?

Use `docs/research/20260525-143937-mature-a64-emulator-backend-patterns.md`
and `docs/research/20260525-150348-82282490-82287788-register-cache-gap.md`
as the design baseline.

## Closed Immediate Lanes

Do not repeat these without new evidence:

- `82282490:82282598 -> 82287788` edge payload behavior; lifetime audit found
  no surviving `f[1]`/`r[3]` window.
- Pre/post-promotion `r1`/`r11` register-cache behavior from the 2026-05-25
  counters.
- Narrow stvewx lane folds, three-PC `MUL_ADD_V128`, standalone `f[1]`, fpscr
  dirty-cache behavior, local-only `822824F0` peepholes, broad CR fusion, and
  old emit-time context caches.

## Better A64 Bets

Favor changes that can affect many dynamic instructions:

- source-level register allocation or guest-state cache improvements with
  explicit CFG/lifetime rules;
- direct-call/block-linking dispatch cost reductions with normal fallback;
- helper ABI cleanup that removes repeated scratch/FPCR/FPSCR/state churn;
- general VMX128/NEON lowering for high-volume opcodes, not a single PC;
- fastmem/addressing improvements with correctness fallback;
- mixed/static hot-function variant experiments only when guest-visible state is
  explicit and normal entry remains correct.

Current structural decision:
`docs/research/20260526-001500-a64-register-allocation-audit-capture.md`
records FullDeploy plus a route-clean `0x82282490`
`arm64_register_allocation_audit` capture. It found no material allocator spill
pressure: `blocks_with_spills=0`, `local_slots_added=0`, and INT/FLOAT/VEC
`spill_requests=0`. Do not patch allocator spill behavior or run a quiet speed
A/B from this audit. `docs/research/20260526-004800-a64-helper-block-link-audit.md`
then found material dispatch volume in the warmed route:
`dispatch_delta_total=4216370`, `dispatch_delta_per_entry_delta=1.10`,
`resolve_misses_total=0`, and only `resolves_delta=5`. Do not target resolver
behavior next. Use existing call-edge profiling on
`82282490,82281D28,82287788` before any helper ABI or block-linking behavior
patch, or fall back to current route-stabilized PERMUTE / LOAD_VECTOR_SHL /
LOAD_VECTOR_SHR counters before VMX128 behavior work.

For the helper ABI / block-linking lane, run this offline audit before deciding
whether a Thor call-edge capture is justified:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_helper_block_link_audit.ps1
```

For the VMX128/NEON lane, run this before behavior work:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_vmx128_neon_family_audit.ps1
```

Current decision: PACK/UNPACK unit coverage is complete, and the route-volume
audit in `docs/research/20260525-233000-vmx128-route-volume-audit.md` closes
PACK/UNPACK for immediate behavior work: PACK is absent in the checked hot
functions, and UNPACK has zero weighted hot-block volume. If continuing VMX128,
prefer current route-stabilized counters for PERMUTE and LOAD_VECTOR_SHL/SHR
before any behavior patch. Non-stvewx EXTRACT/SPLAT needs either SPLAT tests or
broad route volume. Do not reopen exact stvewx or three-PC `MUL_ADD_V128` from
this lane.

## Thor Hardware

The Thor lane can use NEON/Advanced SIMD, dot-product, I8MM, BF16/FHM/FCMA,
CRC32, and LSE-style atomics when the opcode/source evidence matches. Do not
assume SVE/SVE2. GPU offload is for Xenos-like bulk work that can stay
GPU-resident: resolves, format conversion, deswizzle/tile transforms, vertex
fetch unpack, clears, copies, and postprocess.

## Capture Discipline

Use delayed body-time for opening-route CPU work:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 25 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Keep audit/disassembly/body-block captures separate from quiet speed A/Bs.
Speed A/Bs require matched APK, route, cvars, audit-off settings, and clean
fatal-marker searches.

## Acceptance

For a behavior patch, require:

- default-off or clearly safe default behavior;
- route-clean Thor proof;
- matched quiet A/B or control sandwich if claiming speed;
- worklog plus research note naming expected FPS mechanism;
- commit/push on `master`.

If the expected win is below noise or only affects one tiny PC, close the lane
instead of adding another cvar.
