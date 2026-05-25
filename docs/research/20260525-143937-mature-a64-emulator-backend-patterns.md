# Mature A64 Emulator Backend Patterns

Date: 2026-05-25

Prompt: Blue Dragon still cannot claim sustained 30 FPS on AYN Thor. Current
Thor evidence says Main Thread / A64 generated-code work is the wall, not
GPU Commands or Vulkan. Research how mature emulator ports get good AArch64
CPU speed and turn that into a better xenia-thor process.

## Primary Sources Checked

- Dolphin official `JitArm64` source tree:
  <https://github.com/dolphin-emu/dolphin/tree/master/Source/Core/Core/PowerPC/JitArm64>
- Dolphin `JitArm64_RegCache.h`, including dedicated state registers,
  lock/unlock, dirty state, last-used tracking, and block register stats:
  <https://raw.githubusercontent.com/dolphin-emu/dolphin/master/Source/Core/Core/PowerPC/JitArm64/JitArm64_RegCache.h>
- PPSSPP official ARM64 JIT source tree:
  <https://github.com/hrydgard/ppsspp/tree/master/Core/MIPS/ARM64>
- PPSSPP ARM64 IR JIT and register cache sources, including static registers,
  block linking, IR compile loop, invalidation, and static-register save/load:
  <https://raw.githubusercontent.com/hrydgard/ppsspp/master/Core/MIPS/ARM64/Arm64IRJit.cpp>
  <https://raw.githubusercontent.com/hrydgard/ppsspp/master/Core/MIPS/ARM64/Arm64IRRegCache.h>
- Dynarmic design documentation:
  <https://raw.githubusercontent.com/lioncash/dynarmic/master/docs/Design.md>
- Dynarmic ARM64 backend source tree:
  <https://github.com/lioncash/dynarmic/tree/master/src/dynarmic/backend/arm64>
- FEX official development wiki on signals/backpatching and tests:
  <https://wiki.fex-emu.com/index.php/Development%3ADebugging_FEX_with_Signals>
  <https://wiki.fex-emu.com/index.php/Development%3ASetting_up_FEX>
- DuckStation official README, as a sanity check that mature emulators ship
  both CPU recompilers and multiple renderers as separate capability lanes:
  <https://github.com/stenzek/duckstation>
- Arm AAPCS64 ABI specification:
  <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>

## What Mature A64 Ports Have In Common

The pattern is not "port Vulkan and hope." Mature ports separate CPU and GPU
lanes, then make the CPU dynarec boringly strong:

- Register cache first: guest GPR/FPR/vector state is kept in host registers
  with dirty tracking, last-used/spill policy, call locking, and explicit flush
  rules. Dolphin and PPSSPP both have first-class ARM64 register-cache code.
- Stable host registers: hot state pointers, memory base pointers, dispatch
  PCs, downcount, and scratch registers are assigned deliberately instead of
  rediscovered through title-specific helpers.
- Block cache and linking: exits are patched or linked to compiled blocks when
  possible, with invalidation paths that overwrite or unlink safely.
- Fast memory and fault/backpatch discipline: direct guest memory paths are
  used when legal, while faults or slow cases patch or fall back without making
  every access a helper call.
- IR before machine code: Dynarmic and PPSSPP both use an IR layer that can
  optimize redundant context get/set traffic before host emission.
- ABI/helper discipline: helper calls are surrounded by known save/restore,
  locked-register, and static-register rules. AArch64 ABI pressure is treated
  as a design constraint, not an afterthought.
- Vector/FP state policy: SIMD lowering is paired with explicit FP status,
  NaN/denormal, and rounding behavior contracts. The fast path is allowed only
  after the cold semantic paths are proven cold or are preserved.
- Deterministic harnesses: FEX exposes ASM and IR test harnesses, and mature
  dynarecs keep correctness tests near backend changes instead of relying only
  on long game captures.

## What This Means For Xenia Thor

Blue Dragon's current captures show CPU/JIT pressure: latest route-clean
evidence still has Main Thread around one full core and GPU Commands far below
that. The repeated narrow cvars are useful for proof, but they are not a
maturity strategy.

Before another title-specific speed patch, the default path should now be:

1. Produce or update a structural A64 gap report for the hot function/span.
2. Rank work by backend subsystem, not only by guest PC:
   register cache, helper ABI/call discipline, block linking/dispatcher,
   fastmem/address translation, vector/FP lowering, code-cache/invalidation,
   and test-harness coverage.
3. Only then choose one variable:
   a backend-wide improvement if the gap is structural, or a title/function
   gate if the evidence proves the issue is truly local.
4. Add deterministic unit/offline tests for the backend behavior whenever the
   change can be isolated away from Thor.
5. Use Thor route captures for runtime truth and FPS proof, not as the only
   way to discover every idea.

## Immediate Process Change

Skills updated from this note should steer future Blue Dragon work away from
"one weird PC at a time" unless the profiler proves a local patch is the right
next cut. The next sprint slice should either build an A64 mature-backend gap
report for the `82282490 -> 82287788` route, or make a backend-harnessed
change in one of the structural lanes above.

Do not pivot Blue Dragon to broad Vulkan work until a capture shows GPU
Commands, present/submit, pipeline creation, shader compile, or draw thread
stalls overtaking Main Thread/A64 generated-code work.
