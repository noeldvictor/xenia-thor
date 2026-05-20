# PowerPC To ARM64 Source Harvest

Date: 2026-05-20 18:01 EDT
Branch: `master`
Device priority: AYN Thor Max / Snapdragon 8 Gen 2 / Adreno 740
Target title: Blue Dragon Disc 1 on the Thor SD card

## Question

Are there public PowerPC-to-AArch64 notes, codebases, or source patterns we can
use to mature the Xenia Thor ARM64 backend faster than one peephole at a time?

## Short Answer

Yes. There are useful public sources, but none is a drop-in Xbox 360 Xenon to
Android/Adreno solution.

The best immediate sources are:

- Dolphin `JitArm64` for direct PowerPC-to-AArch64 JIT structure.
- QEMU TCG for translation-block linking, PPC semantics, atomics, and AArch64
  host-backend maturity.
- RPCS3 PPU/LLVM/AArch64 work for a higher-level LLVM route and PPU analysis
  patterns.
- IBM/AIX PowerPC docs for exact CR/LR/CTR branch semantics.

Important license note: Dolphin, QEMU, and RPCS3 are GPL-family projects. This
Xenia fork has been treating Xenia-derived permissive/BSD code differently from
GPL emulator code. Use these sources as design references, audit guides, test
oracles, and architecture notes unless the project explicitly chooses a
license-compatible import strategy. Do not blindly copy implementation bodies
into this fork.

## Sources Checked

Accessed on 2026-05-20:

- Dolphin JitArm64 source:
  <https://github.com/dolphin-emu/dolphin/tree/master/Source/Core/Core/PowerPC/JitArm64>
- Dolphin CPU emulation overview:
  <https://www.mintlify.com/dolphin-emu/dolphin/architecture/cpu-emulation>
- QEMU translator internals:
  <https://www.qemu.org/docs/master/devel/tcg.html>
- QEMU TCG IR:
  <https://www.qemu.org/docs/master/devel/tcg-ops.html>
- QEMU multi-threaded TCG:
  <https://www.qemu.org/docs/master/devel/multi-thread-tcg.html>
- QEMU PPC target:
  <https://github.com/qemu/qemu/tree/master/target/ppc>
- QEMU AArch64 TCG backend:
  <https://github.com/qemu/qemu/tree/master/tcg/aarch64>
- RPCS3 ARM64 bring-up blog:
  <https://blog.rpcs3.net/2024/12/09/introducing-rpcs3-for-arm64/>
- RPCS3 PPU source:
  <https://github.com/RPCS3/rpcs3/tree/master/rpcs3/Emu/Cell>
- RPCS3 AArch64 backend helpers:
  <https://github.com/RPCS3/rpcs3/tree/master/rpcs3/Emu/CPU/Backends/AArch64>
- IBM AIX branch processor docs:
  <https://www.ibm.com/docs/en/aix/7.3.0?topic=storage-branch-processor>
- IBM AIX `bclr` / conditional LR branch docs:
  <https://www.ibm.com/docs/en/aix/7.1.0?topic=set-bclr-bcr-branch-conditional-link-register-instruction>

Sparse local checkouts used for source inspection:

- `scratch/upstream/dolphin` at
  `7b69949cec7afcd1691a9a046d26d5c4a99ce4fc`
- `scratch/upstream/qemu` at
  `e89049b3ba5f1f0468bc0d294173345597514a1b`
- `scratch/upstream/rpcs3` at
  `67464f97df8679d5d540256987551f34fe00d4cc`

## What Dolphin Gives Us

Dolphin is the closest public reference because its `JitArm64` is a direct
PowerPC-to-AArch64 JIT used on Android and Apple Silicon.

Useful inspected files:

- `Source/Core/Core/PowerPC/JitArm64/Jit.cpp`
- `Source/Core/Core/PowerPC/JitArm64/JitArm64_RegCache.h`
- `Source/Core/Core/PowerPC/JitArm64/JitArm64_RegCache.cpp`
- `Source/Core/Core/PowerPC/JitArm64/JitArm64_Branch.cpp`
- `Source/Core/Core/PowerPC/JitArm64/JitArm64Cache.cpp`
- `Source/Core/Core/PowerPC/JitArm64/JitArm64_SystemRegisters.cpp`

High-value patterns:

- Pinned host registers for memory base, PPC state pointer, and dispatcher PC.
- A real GPR/FPR/CR register cache that tracks dirty state, last use, discard,
  and flush mode.
- CR fields are cached as first-class guest state, not treated only as
  arbitrary memory bytes.
- Branch lowering handles CTR decrement/test, CR bit test, LR/CTR indirect
  exits, idle-loop detection, and conditional fallthrough without flushing
  everything every instruction.
- Block linking patches exits to already-compiled destination blocks.
- The compile loop uses PPC analysis data (`regsIn`, `regsOut`, `crIn`,
  `crOut`, discardable sets, last-use) to flush only what must be flushed.

Why this maps to our pain:

- Blue Dragon hot function `8272A3A4` is still losing time to context traffic,
  CR compare/store churn, branch glue, and helper/dispatcher overhead.
- Dolphin's strongest lesson is not one magic opcode. It is that PPC state
  needs to stay live in host registers across a block/function and be flushed
  only at proven boundaries.
- Our HIR-level A64 backend cannot copy Dolphin's PPC decoder, but it can copy
  the idea: introduce a PPC context/state traffic cache around hot HIR shapes,
  especially CR bytes, LR/CTR, and high-frequency GPR context slots.

Limits:

- Dolphin targets GameCube/Wii Gekko/Broadway, not Xbox 360 Xenon.
- It does not solve VMX128, Xbox kernel/HLE, Xenos GPU, or 3-core Xenon timing.
- GPL licensing means we should treat it as a design reference unless we make a
  deliberate license-compatible import decision.

## What QEMU Gives Us

QEMU is not an emulator-tuned game-console JIT, but its TCG stack is a mature
dynamic translator with a PowerPC frontend and AArch64 host backend.

Useful inspected files:

- `docs/devel/tcg.rst`
- `docs/devel/tcg-ops.rst`
- `docs/devel/multi-thread-tcg.rst`
- `target/ppc/translate.c`
- `target/ppc/translate/vmx-impl.c.inc`
- `tcg/aarch64/tcg-target.c.inc`

High-value patterns:

- Translation blocks record CPU-state assumptions and only reuse code when the
  state matches.
- Direct block chaining avoids returning to the main dispatch loop for known
  direct branch targets.
- `lookup_and_goto_ptr` handles dynamic targets by helper lookup plus direct
  jump when a destination block exists.
- `cpu_env` is a fixed global variable kept live in host code.
- Helpers have metadata about whether they read or write globals. That matters
  because a helper call can force expensive state stores/reloads.
- PPC CR fields are modeled as separate `crf[8]` globals.
- PPC reservation instructions are modeled explicitly (`reserve_addr`,
  `reserve_length`, `reserve_val`) and conditional stores use atomic compare
  exchange plus CR0 result updates.

Why this maps to our pain:

- The current Edge-style reservation helper import is directionally consistent
  with QEMU's explicit reservation model, but it is not a Blue Dragon speed
  win yet.
- QEMU's block-chaining model is directly relevant to our direct guest call and
  dispatcher overhead.
- The helper metadata idea suggests a practical next audit: identify helpers
  that do not mutate PPC context and stop flushing/reloading state around them.

Limits:

- TCG is GPLv2 and too large/general to embed into this BSD-style Xenia fork.
- TCG's portability priorities do not equal Blue Dragon hot-loop performance.
- QEMU PPC is a semantics oracle and design reference, not a donor backend.

## What RPCS3 Gives Us

RPCS3 is useful because PS3 PPU is also PowerPC-family and RPCS3 has active
ARM64/LLVM work.

Useful inspected files:

- `rpcs3/Emu/Cell/PPUTranslator.h`
- `rpcs3/Emu/Cell/PPUTranslator.cpp`
- `rpcs3/Emu/Cell/PPUAnalyser.cpp`
- `rpcs3/Emu/CPU/CPUTranslator.h`
- `rpcs3/Emu/CPU/Backends/AArch64/AArch64ASM.cpp`

High-value patterns:

- PPU translation is function/module oriented through LLVM IR, not just a tiny
  per-instruction emitter.
- It separates globals and locals for PPU GPR/FPR/VR/CR/LR/CTR/XER state.
- On ARM64, it registers an AArch64 transform pass with fixed base register
  lookup for PPU blocks and helpers.
- The ARM64 bring-up blog shows the path was not instant: interpreter support,
  LLVM support, leaf-node handling, non-x86 RSX fixes, and settings cleanup all
  landed in stages.

Why this maps to our pain:

- This supports a second backbone if direct Xbyak-aarch64 work stalls: compile
  stable hot guest functions or traces through LLVM/AArch64, then compare
  generated code against our direct A64 emitter.
- RPCS3's PPU analyzer/module structure is useful for Ghidra/OODA work:
  function boundaries, leaf blocks, imports, relocations, and stateful thread
  context all matter.

Limits:

- PS3 PPU is not Xbox 360 Xenon and RPCS3's architecture is not Xenia's HIR.
- LLVM bring-up is likely a larger integration project than a short speed
  patch.
- GPL licensing means conceptual use unless the license/import strategy changes.

## What IBM PowerPC Docs Give Us

IBM's branch processor docs are a useful sanity check for the stuff that keeps
burning us: CR, LR, and CTR.

The docs confirm the user-mode branch processor state we keep seeing in hot
code:

- Condition Register
- Link Register
- Count Register

They also document that `bclr` branches through LR, that BO/BI encode combined
CTR decrement/test and CR-bit conditions, and that CR/LR behavior is precise
enough that broad "looks equivalent" fusions are risky.

Why this maps to our pain:

- Our failed broad compare-branch fusion and successful narrow UGT/EQ CR branch
  peephole are exactly the right lesson: branch/CR transforms must be
  semantics-guarded by exact CR offsets, use counts, polarity, and branch
  shape.

## Concrete Plan For Xenia Thor

Do not chase another blind donor copy. Use the source harvest to build a bigger
backend maturity lane:

1. Add an A64 context-traffic audit for a selected guest function.
   - First target: `8272A3A4`.
   - Count emitted `LOAD_CONTEXT`, `STORE_CONTEXT`, CR stores, CR loads,
     LR/CTR traffic, helper calls, direct exits, indirect exits, endian
     load/store swaps, and dispatcher returns.
   - Output compact rows in the Thor speed capture so this stays fast.

2. Build a Dolphin-style PPC state cache at the HIR/A64 boundary.
   - Start with CR bytes and common GPR context slots, not every PPC register.
   - Track dirty/live state.
   - Flush at helpers, exits, exceptions, and unknown memory/context aliasing.
   - Preserve exact semantics before turning it on globally.

3. Use QEMU-style helper metadata to reduce unnecessary flush/reload.
   - Classify helpers called from the hot path as no-context-write,
     no-context-read, or full barrier.
   - Keep hot PPC state live across helpers proven not to mutate it.

4. Improve direct block/function linking before another GPU deep dive.
   - Count direct-link hits/misses and dispatcher returns in Blue Dragon.
   - Patch constant target exits more aggressively when state assumptions match.
   - Keep indirect branch lookup measurable.

5. Keep LLVM as a parallel research route, not the next tiny patch.
   - Good candidate: compile one hot stable guest function/trace through LLVM
     AArch64 as a comparison oracle.
   - Do not replace the direct A64 backend until the integration cost is clear.

## Decision

The next speed sprint should be a measured "state traffic reduction" sprint, not
another small constant-lowering peephole. The public sources point at the same
bigger answer: keep PPC state live, link blocks/functions more directly, and
only flush when semantics require it.

For Blue Dragon, that means the next work item is an audit and first cache pass
for `8272A3A4`, not more GPU speculation and not a one-off title hack.
