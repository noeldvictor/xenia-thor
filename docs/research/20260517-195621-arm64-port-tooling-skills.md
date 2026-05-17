# ARM64 Port Tooling and Skills

Date: 2026-05-17 19:56 EDT

Scope: research-only tooling plan for speeding up the xenia-thor Android ARM64
port. This is not official Xenia guidance.

## Main Conclusion

Do not build a generic "x86 to ARM" converter. Xenia's mature x64 backend is
useful as a reference, but the faster path is to port Xenia's guest PowerPC/HIR
semantics to AArch64 sequences with deterministic parity tests.

The right tooling should answer four questions quickly:

- What HIR opcodes still fall back or miscompile?
- What x64 sequence already expresses the intended semantics?
- What AArch64 did we emit for the same HIR?
- Does the AArch64 result match interpreter/x64 behavior for registers,
  memory, flags, exceptions, byte swaps, MMIO, and guest thread state?

## Internet Findings

- Xbyak_aarch64 is already aligned with the current repo direction: it is a
  C++ runtime assembler for AArch64 with GNU-assembler-like syntax and SVE
  support.
  Source: https://github.com/fujitsu/xbyak_aarch64
- AsmJit now has AArch64 API documentation including assembler, builder,
  compiler, registers, memory operands, and instruction database surfaces. It is
  worth evaluating later, but switching emitters right now would slow the Blue
  Dragon bring-up unless Xbyak_aarch64 blocks us.
  Source: https://asmjit.com/doc/annotated.html
- Capstone supports X86, ARM64/AArch64, PowerPC, and many other ISAs with a
  simple architecture-neutral API and instruction details such as implicit
  register reads/writes. This is a good generated-code disassembly layer for our
  scripts.
  Source: https://github.com/capstone-engine/capstone
- Dynarmic is a useful design reference for JIT architecture and testing style,
  but it is an ARM guest recompiler, not a Xbox 360 PowerPC-to-AArch64 answer.
  Source: https://github.com/yuzu-mirror/dynarmic
- QEMU TCG proves dynamic translation across architectures is viable, but QEMU
  is GPLv2 and architecturally too large to embed into this BSD Xenia fork as a
  shortcut. Use it for design comparison only.
  Source: https://github.com/qemu/QEMU
- DynamoRIO supports IA-32/AMD64/ARM/AArch64 instruction manipulation on Android
  and can inspire instrumentation ideas, but it is not a direct Xenia backend
  port path.
  Source: https://github.com/DynamoRIO/dynamorio
- llvm-mca can statically estimate throughput, IPC, and resource pressure for
  assembly sequences using LLVM scheduling models. This can help optimize hot
  AArch64 sequences after correctness.
  Source: https://www.llvm.org/docs/CommandGuide/llvm-mca.html
- Xenia's Vulkan issue reinforces that Android support needs Vulkan rather than
  OpenGL, but also that Android Vulkan feature variability is a separate wall
  after CPU/JIT correctness.
  Source: https://github.com/xenia-project/xenia/issues/2028

## Tools To Build

### 1. HIR Coverage Matrix

Script: `tools/arm64/hir_coverage_report.*`

Scan:

- x64 emitter opcode coverage.
- ARM64 mini-JIT opcode coverage.
- interpreter-only/fallback counters from Thor logs.
- function compile reject reasons.

Output:

- Markdown table sorted by "hot and missing first".
- Counts from latest Blue Dragon log.
- Links to likely x64 reference files and ARM64 implementation locations.

Why it helps:

- Stops random opcode chasing.
- Lets the next agent pick the highest-impact missing sequence.

### 2. X64-to-A64 Sequence Port Assistant

Script/skill: `xenia-a64-sequence-port`

Given a HIR opcode or x64 sequence file:

- Extract x64 implementation.
- Summarize semantic obligations, not x64 instructions.
- Generate an AArch64 emitter checklist:
  - source/dest register class,
  - flags/condition code behavior,
  - endian behavior,
  - memory/MMIO behavior,
  - helper calls,
  - clobbered host regs,
  - fallback cases.

Why it helps:

- Converts "translate x86 to ARM" into "port semantics safely".

### 3. Differential HIR/JIT Runner

Tool: `xenia-arm64-diff-runner`

Run the same small guest/HIR function through:

- HIR interpreter,
- x64 backend on PC when available,
- ARM64 mini-JIT on Thor or AArch64 host.

Compare:

- PPCContext GPR/FPR/VMX,
- CR/XER/LR/CTR,
- touched memory ranges,
- exceptions/MMIO callbacks,
- branch target and return status.

Why it helps:

- Finds CPU bugs before full Blue Dragon boot.
- Makes a failing opcode reproducible without a 2-minute APK/device loop.

### 4. Generated-Code Inspector

Script: `tools/arm64/dump_generated_code.*`

For each compiled guest function:

- Dump guest PPC address and HIR.
- Dump AArch64 bytes.
- Disassemble with Capstone or LLVM objdump.
- Include code-cache start/end, protection mode, and installed function pointer.
- Optionally emit a Ghidra import map.

Why it helps:

- Turns crashes like `fault == pc` into annotated generated-code locations.

### 5. ARM64 ABI Clobber Checker

Tool: runtime poison test plus static checklist.

Check:

- SP 16-byte alignment.
- x19-x28 callee-saved preservation.
- v8-v15 preservation if used across host calls.
- Xenia backend context register invariants.
- helper-call argument/result registers.

Why it helps:

- A single bad callee-save or helper clobber can look like random game logic
  corruption.

### 6. Memory/MMIO Sequence Fuzzer

Tool: targeted tests for load/store HIR.

Cases:

- 8/16/32/64-bit load/store.
- byte-swapping and non-byte-swapping forms.
- page boundary.
- 0x7FC8/0x7FEA known MMIO.
- protected GPU memory.
- invalid address and signal path.

Why it helps:

- Blue Dragon is already stressing endian/MMIO paths.

### 7. Thor Hot Native Loader

Tool/skill: `thor-native-core-hotload`

Keep APK shell stable and push only the native library to app-private storage
for debug runs. Launch with a flag selecting packaged core vs pushed core.

Why it helps:

- C++ loop becomes build native, push `.so`, force-stop, launch, capture.
- Avoids Gradle/APK reinstall for most CPU backend work.

### 8. Trace-to-Test Reducer

Tool: `tools/arm64/reduce_boot_trace.*`

Convert a Blue Dragon boot failure into:

- guest function address,
- HIR dump,
- minimal PPCContext seed,
- memory window,
- expected result from interpreter/x64.

Why it helps:

- Creates regression tests from real title boot failures.

## Skills To Create

### `xenia-a64-sequence-port`

Use when adding or fixing an ARM64 emitter sequence from x64/HIR reference.
Should include:

- register allocation rules,
- AArch64 ABI rules,
- byte-swap/MMIO checklist,
- "semantic port, not x86 transliteration" rule,
- required tests before Thor deployment.

### `xenia-jit-differential-debug`

Use when a generated ARM64 function misbehaves or crashes.
Should include:

- capture HIR/PPC/A64,
- run interpreter parity,
- disassemble generated code,
- map crash PC to generated function and HIR instruction,
- produce a minimized test.

### `thor-native-hotload-loop`

Use when iterating on native C++ only.
Should include:

- build native core,
- push `.so` to app-private devlibs,
- launch with debug core flag,
- capture fast/cpu/trace/gpu lanes,
- hash and record exact core build.

### `xenia-arm64-fork-miner`

Use when comparing upstream/canary/edge/Wunkolo ARM64 work.
Should include:

- license/source provenance checklist,
- cherry-pick safety notes,
- symbol/file mapping,
- "do not bother upstream for support" warning.

## Recommended Build Order

1. Create `xenia-a64-sequence-port` and the HIR coverage matrix first.
2. Create generated-code inspector next, because the current Blue Dragon crash
   needs code-cache address mapping.
3. Build the differential runner before adding many more opcodes.
4. Build Thor native hotload as soon as code-cache mode is stable.
5. Use llvm-mca only after correctness, for the hottest AArch64 sequences.

## Anti-Goals

- Do not try to automatically convert x64 machine code to AArch64.
- Do not embed QEMU TCG into this BSD fork.
- Do not switch assembler libraries during the title-screen push unless
  Xbyak_aarch64 blocks a required instruction class.
- Do not use AGI/RenderDoc as the default loop while the crash is still CPU/JIT.
