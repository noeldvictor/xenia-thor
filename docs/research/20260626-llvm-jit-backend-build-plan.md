# LLVM-JIT Backend — Build Plan (authorized 2026-06-26)

User chose: **embed LLVM as an in-app ORCv2 JIT, compile guest modules at game-load**
(RPCS3 model), accepting the Android costs (+15-40 MB, first-exec compile latency).
Goal: register RESIDENCY via LLVM's whole-function optimizer, eliminating the per-block
PPCContext round-trip tax (~1770 LOAD/STORE_CONTEXT in BD's hot fn `guest_824694A0`).

This consolidates 3 research agents (LLVM-on-Android, RPCS3/XenonRecomp reference, xenia
backend map). Cross-references: docs/research/20260626-static-recomp-residency-eval.md.

## Architecture
`PPC → HIR (REUSE xenia's frontend) → LLVM IR (new lowering) → ORCv2 LLJIT (AArch64) →
native, precompiled at module load.` A new `Backend` (`src/xenia/cpu/backend/llvm/`)
selected by a cvar; hybrid (LLVM for hot/precompiled fns, a64 JIT for cold/fallback)
sharing the dispatcher + entry table.

## Residency mechanism (the whole point) — RPCS3 m_locals model, adapted to HIR
Do NOT emit a literal `llvm load/store` per guest-reg access and hope mem2reg fixes it
(the PPCContext is a pointer param, not an alloca — mem2reg won't promote it without an
alloca copy or proven non-aliasing). Instead, maintain a **per-function SSA-value table
indexed by guest reg** during HIR→LLVM lowering:
- `LOAD_CONTEXT slot` → if the slot has a cached `llvm::Value*`, return it (no IR memory
  op); else emit ONE `load` from the ctx struct GEP and cache it.
- `STORE_CONTEXT slot, v` → set table[slot] = v; emit NO store.
- **Flush** (walk the dirty table → `store` to ctx) ONLY at transfer boundaries: before
  every CALL/CALL_INDIRECT/RETURN/CONTEXT_BARRIER, and at function exit. Across HIR
  basic-block edges, thread the table via LLVM `phi` nodes (or alloca-per-slot +
  `mem2reg`, which is simpler to implement first and lets LLVM build the phis).
- This makes repeated reg reads/writes pure SSA dataflow → LLVM's whole-function regalloc
  keeps them in host registers. Round-trips to memory only at boundaries.
- **First impl shortcut:** alloca one i64/f64/v128 slot per guest reg at entry, lower
  LOAD/STORE_CONTEXT to load/store on those allocas, run `PromoteMemoryToRegisterPass`
  (mem2reg). Identical end result, much less lowering code; optimize to the explicit
  table later if mem2reg is too slow.

## Flags (CR0-7 + XER) — per-bit SSA, lazy
Represent each CR bit and XER CA/OV/SO as an INDEPENDENT value (i1 / i8 alloca), never a
packed register — so a flag no branch consumes becomes dead and DCE deletes it. Whole-
function lowering lets us go LAZY (defer materialization to the consuming branch/mfcr and
emit only the bits read) — strictly beats eager+DCE and extends the shipped tier-2 flag DSE
([[tier2-crossblock-flag-dse]]). xenia's HIR already lowers most CR work to COMPARE +
BRANCH_TRUE/FALSE, so much of this is free; the residual mfcr/cror/record-form paths need care.

## Memory model
Guest load/store → `base + zext32(addr)` + inline `@llvm.bswap` (→ ARM64 REV, ~free).
`base` = the membase pointer (a function arg or a global, pinned). Oversized reservation
(xenia already maps guest memory) → no bounds check. Because guest regs are SSA table
entries (above), guest memory ops never alias the register state → residency is automatic,
NO alias-analysis dependency (RPCS3's key property). Atomics (lwarx/stwcx) → helper calls
+ a flush in front.

## Dispatch / calls (reuse xenia's entry table)
xenia already has `EntryTable` (entry_table.h) + the a64 resolve thunks. The LLVM backend
reuses them: direct CALL → `call` the target's resolved host pointer (or a stub that
resolves via EntryTable); CALL_INDIRECT → `call` a resolve-helper(ctx, addr) that does the
EntryTable lookup. PPCContext* is the first function arg (mirror a64's x20 convention).
RETURN → `ret`. Keep the a64 backend as the fallback so unresolved/cold fns still run.

## Integration map (from the xenia backend agent)
- Backend base: `backend.h:50` — implement CommitExecutableRange, CreateAssembler,
  CreateGuestFunction, CalculateNextHostInstruction (+ Initialize). Model `backend/a64/`.
- HIR: `hir/{block,instr,value,opcodes}.h`. Lower categories: LOAD/STORE_CONTEXT,
  LOAD/STORE(+OFFSET), ADD/SUB/MUL/DIV/AND/OR/XOR/shifts, COMPARE_*, BRANCH/BRANCH_TRUE/
  FALSE, CALL/CALL_INDIRECT/RETURN, VECTOR_* (→ <N x iM> + NEON), BYTE_SWAP/CNTLZ/SQRT
  (→ intrinsics), CAST/ZERO_EXTEND/SIGN_EXTEND/TRUNCATE, CONTEXT_BARRIER (→ flush).
- PPCContext offsets (`ppc_context.h`): r[32]@0x20, f[32]@0x120, v[128]@0x220, lr@0x10,
  ctr@0x18, xer_ca@0xA20, cr0@0xA24 (split lt/gt/eq/so).
- The tax we remove: `register_allocation_pass.cc:657` PrepareBlockState() resets all host
  regs per block. The LLVM path bypasses this pass entirely.

## LLVM-on-Android (from the LLVM agent) — the gating dependency
- No prebuilt libLLVM for the NDK → **cross-build from source** (LLVM 18.1.8, cloning now).
  cmake: NDK android.toolchain.cmake, ANDROID_ABI=arm64-v8a, ANDROID_PLATFORM=android-26,
  LLVM_TARGETS_TO_BUILD=AArch64, LLVM_HOST_TRIPLE=aarch64-unknown-linux-android, point
  LLVM_TABLEGEN at a prebuilt HOST llvm-tblgen (the cross gotcha). Static `.a`s linked into
  our .so + --gc-sections (smallest). AArch64-only + codegen + ORC + IR/Support.
- **W^X / SELinux:** untrusted_app can't flip W→X; use **ORCv2 + JITLink (ObjectLinkingLayer)**
  with a dual RW/RX mapping (memfd) — NOT legacy MCJIT/RuntimeDyld. Up-front anonymous RWX
  mmap works as a fallback.
- ORC API: InitializeNativeTarget + AsmPrinter → LLJITBuilder().create() → addIRModule(TSM)
  → lookup(name) → ExecutorAddr.toPtr<T>(). Size: AArch64-only stripped + --gc-sections ≈
  3-10 MB (codegen-only) to 15-40 MB (with the -O2 mid-end). Build & measure.

## Compile flow + caching (RPCS3)
Compile AOT at module load (before guest threads — sidesteps the recursive-global-lock
deadlock, [[parallel-jit-precompiler-wall]]), parallel workers, ~100 KiB/≥1000-fn units
sharing an LLJIT, progress "Compiling modules". Object cache keyed by SHA1(guest code bytes)
+ a settings bitset + CPU, gzipped → skip codegen on relaunch (the big startup win).

## PHASES (each commits; default-off cvar `cpu_backend_llvm`)
- **P0 — LLVM dep:** cross-build LLVM 18.1.8 for android-arm64 (AArch64-only, ORC+JITLink);
  link a trivial "build IR for add1, JIT, call → 43" test into the app; measure .so size.
  THE gating step (in progress).
- **P1 — Backend skeleton:** `backend/llvm/` (LLVMBackend, LLVMAssembler, LLVMCodeCache,
  LLVMFunction) implementing the 4 virtuals; CreateAssembler returns the LLVM assembler;
  a64 stays default. Compiles (with LLVM linked), no lowering yet.
- **P2 — HIR→LLVM lowering (scalar):** the SSA reg table (alloca+mem2reg first), context
  load/store, integer ALU, compares, branches, calls/returns via EntryTable, memory
  load/store + bswap. Differential vs a64 on the host/qemu cpu-tests (RequireTransparent
  byte-identical).
- **P3 — Flags + vector + FP:** CR/XER per-bit lazy, VMX → <N x iM>/NEON, FP + FPSCR. Pass
  the existing cpu-tests under the LLVM backend on qemu-a64.
- **P4 — JIT integration + dispatch:** ORCv2 LLJIT + JITLink, wire compiled fns into
  EntryTable, hybrid fallback to a64. Boot a tiny title.
- **P5 — Precompile-at-load + object cache:** parallel load-window compile, gzipped SHA1
  cache. Boot BD.
- **P6 — Device validation:** BD boots pixel-correct on the LLVM backend; profile
  `guest_824694A0` — confirm the ~1770 context round-trips collapse; single-run alternating
  a64-vs-LLVM fps on a matched scene.
- **P7 — Optimization:** lazy-flags, the explicit SSA table if mem2reg is slow, tuned LLVM
  pass pipeline, the pinned-register calling convention.

## Risks / open
- LLVM .so size on Android (measure in P0; if >40 MB, reconsider the AArch64-only/-Os trim
  or the lighter MIR/copy-and-patch alternatives the agent flagged).
- Compile latency at load (mitigated by the object cache + parallel compile; first launch slow).
- W^X (JITLink dual-mapping).
- HIR coverage: every HIR opcode the frontend emits must be lowered or fall back to a64.
