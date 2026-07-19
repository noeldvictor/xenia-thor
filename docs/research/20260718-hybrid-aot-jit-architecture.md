# The hybrid static-AOT + dynamic-fallback architecture (2026-07-18)

Research into "the hybrid" — static recompilation as primary, dynamic (JIT/
interpreter) as fallback for the undecidable residue, over a shared HLE runtime.
This is the equilibrium the static-vs-dynamic thinking converged on. The finding:
**the hybrid is not a compromise — it is the proven production architecture of
the two biggest binary translators in the world, and this project already owns
every component of it.**

---

## TL;DR

Apple **Rosetta 2** and Microsoft **Prism** — the two most-shipped x86→ARM
translators on Earth — are both the same shape: **AOT-translate the discoverable
bulk, persist it to a cache, route indirect branches through a lookup table, and
JIT-fall-back on a table miss / for runtime-generated code.** RPCS3 (the closest
emulation-scene analog, same PowerPC guest) is the same: LLVM recompiler +
precompilation + interpreter fallback. The academic line (HBT, HQEMU) adds the
persistent cross-run cache and the background-thread optimizer. For the Xbox 360
the dynamic residue is unusually small because the hypervisor enforces W^X (no
self-modifying code), so "static covers nearly everything; dynamic handles only
late-discovered indirect targets." **xenia-thor already has the JIT (fallback),
the AOT object cache (persistence), the kernel HLE (runtime), and the Vulkan HLE
(GPU service) — the hybrid is a re-wiring of existing parts into the Rosetta-2
shape, not a from-scratch build.**

## The blueprint: Rosetta 2 (the reference implementation to copy)

Rosetta 2 is the cleanest, best-documented AOT+JIT hybrid, and its mechanism is
almost exactly what a 360 hybrid needs:
- **AOT primary + persistent cache.** x86_64 binaries are translated to ARM
  ahead of time and stored as `.aot` files; on launch the daemon checks the
  cache dir and reuses the `.aot` if present, else creates one (write to
  `.in_progress`, rename to `.aot`). Translation happens before the thread runs.
- **Indirect branch = lookup table, miss = JIT.** "When an indirect jump or call
  sets the instruction pointer to an arbitrary offset, the runtime looks up the
  corresponding translated instruction using an x86-to-ARM lookup table that
  contains all function starts and other basic blocks; **if it misses this, it
  can fall back to the JIT.**"
- **JIT reserved for the irreducible residue.** "JIT translation logic is needed
  to support… x86_64 applications that generate x86_64 code at runtime, such as
  JavaScript engines, because AOT translation is not enough for code determined
  only at runtime."
- **Canonical state between instructions** (an x86↔ARM address map) for precise
  exceptions/debugging and any SMC.

Source: [Project Champollion RE of Rosetta 2](https://ffri.github.io/ProjectChampollion/part1/),
[Apple](https://support.apple.com/guide/security/rosetta-2-on-a-mac-with-apple-silicon-secebb113be1/web),
[dougallj: why Rosetta 2 is fast](https://dougallj.wordpress.com/2022/11/09/why-is-rosetta-2-fast/).

## Second production proof: Microsoft Prism

Windows-on-ARM's Prism "combines ahead-of-time translation with dynamic
components to handle undiscovered code and edge cases." Same equilibrium,
independently arrived at by a second trillion-dollar vendor.
Source: [binary-translation survey results].

## Emulation-scene proof: RPCS3 (same PowerPC guest family)

RPCS3 already ships the hybrid for PPU (PowerPC) code:
- **Recompiler:** PPUTranslator → LLVM IR → native (x86-64, and ARM64 since
  Dec 2024).
- **Interpreter fallback:** "If the instruction/function is not found, fall back
  to decoding single instructions… similar to how PPU interpreters handle
  unsupported instructions."
- **Precompilation (the AOT knob):** "searches the game's directory and
  precompiles extra PPU modules during boot. If disabled, these modules will
  only be compiled when needed" (on-demand JIT). So RPCS3 spans the whole
  AOT↔JIT spectrum with one switch.
Source: [Introducing RPCS3 for arm64](https://blog.rpcs3.net/2024/12/09/introducing-rpcs3-for-arm64/),
[PR #16659 ppu_recompiler_fallback](https://github.com/RPCS3/rpcs3/pull/16659).

## Academic backbone: Hybrid Binary Translation + HQEMU

- **HBT framing:** SBT gives whole-program optimization and zero runtime
  translation overhead but is blocked by code-discovery + indirect branches; DBT
  handles indirect branches and runtime behavior but can't afford aggressive
  optimization because translation time is on the critical path. **HBT takes the
  static path for the discoverable/optimizable bulk and the dynamic path for the
  residue** — combining the merits.
- **HQEMU** (QEMU + LLVM): a fast low-overhead translator up front, an
  **optimization-intensive LLVM optimizer running on separate threads** to raise
  code quality without stalling execution, plus a **general persistent code
  cache** reused across runs that "can seamlessly handle even dynamically
  generated code." 2.4–4× over QEMU (ARM→x86-64: 2.4×). Two lessons for the
  project: (1) hide translation latency by optimizing on background threads,
  (2) a cross-run persistent cache is the amortization mechanism.
Source: [HQEMU (ACM 10.1145/2259016.2259030)](https://dl.acm.org/doi/10.1145/2259016.2259030),
[Persistent Code Caching (USENIX ATC'16)](https://www.usenix.org/system/files/conference/atc16/atc16_paper-wang.pdf).

## The dispatch mechanism (how indirect branches route in a hybrid)

This is the concrete seam between static and dynamic, and the recomp tools show
exactly how to build it:
- **N64Recomp:** a detectable `jr` jump table → a C `switch`; a `jalr`
  function-pointer/virtual call → `LOOKUP_FUNC(ctx->r25)(rdram, ctx)` — a
  **runtime function-pointer table lookup**. The runtime table maps guest
  address → host function; entries can be statically-recompiled functions *or*
  JIT-compiled ones. This is the unification point: one table, mixed providers.
- **XenonRecomp (360):** jump tables detected statically from the
  `mtctr r0; bctr` pattern; XenonAnalyse emits a **TOML of jump tables** the
  recompiler turns into real `switch` cases. Unresolved cases (tables that look
  like tail calls) need human-supplied metadata — the irreducible manual tail.
Source: [N64Recomp README](https://github.com/N64Recomp/N64Recomp),
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp).

**⇒ The hybrid seam = one guest-addr→host-fn dispatch table (ReXGlue/N64Recomp/
Rosetta-2 style). Statically-recompiled functions are pre-registered at startup.
On an indirect call, look up the target; on a MISS, JIT it, register it, and
call — subsequent calls are direct. Identical to Rosetta 2's "lookup, miss →
JIT."**

## How far can "static" go? — the frontier and the irreducible residue

A 2026 arXiv paper ("Deterministic Fully-Static Whole-Binary Translation Without
Heuristics," Chen/McGowan/Franz) claims fully-static cross-ISA translation
without dynamic fallback or heuristics — pushing static coverage toward 100% for
**well-behaved, statically-linked, non-self-modifying** binaries. It does not
claim to handle runtime-generated code; "deterministic" signals results under
preconditions, not universality. Read correctly, it *refines* rather than
refutes the hybrid thesis: **you can push static coverage extremely high, but the
theoretical residue (code that exists only at runtime) always needs a dynamic
path for full generality.** The hybrid's dynamic side can shrink to near-zero for
a clean target — but never provably to zero for an arbitrary one.
Source: [arXiv 2605.08419](https://arxiv.org/pdf/2605.08419).

## Why the Xbox 360 is the easy case

The 360 hypervisor enforces **W^X — no self-modifying code**. That deletes the
single hardest dynamic case. The remaining residue is only *late-discovered
code*: indirect targets the static analysis couldn't resolve, and dynamically
loaded modules/DLC. That residue is "discover once, translate once, cache
forever" — never "re-translate because the code changed." So the 360 hybrid's
dynamic side is the *easy* kind of dynamic, and static coverage can approach the
"fully-static" frontier above.

## The payoff: xenia-thor already owns every component

| Hybrid component (Rosetta-2 blueprint) | Project already has |
|---|---|
| JIT / interpreter fallback | the LLVM-JIT backend (default-on, renders BD) |
| Persistent AOT cache | `cpu_llvm_object_cache` (device-validated, ~98% warm hits) |
| Runtime (memory/kernel/threads/fs) | the ARM64 kernel HLE |
| GPU rendering service | the in-progress D3D9→Vulkan HLE |
| Guest↔host dispatch table + ABI | the JIT's resolve-cache + guest-call ABI |

**So the hybrid is not a new build — it is a re-wiring:** flip the primary path
from JIT-first to AOT-static-first (XenonRecomp-style C++ or the existing LLVM
whole-function path emitted at build/precompile time), keep the JIT as the
fallback-on-miss, unify both behind the existing dispatch table, and keep the
kernel + Vulkan HLE underneath. This is the Rosetta-2 shape, and it is the same
direction the project already named ("AOT-LLVM, RexGlue-style, NO JIT/dispatch at
gameplay") — now with a concrete, proven blueprint.

## Concrete architecture for xenia-thor

1. **Build-time / precompile AOT.** Statically discover functions (XEX symbols +
   XenonAnalyse-style jump-table detection → TOML). Emit each as native code via
   the existing whole-function LLVM path (or XenonRecomp C++ → Clang-aarch64).
   Persist to the existing object cache, keyed by guest addr + code hash.
2. **Startup.** Populate the dispatch table with AOT function pointers (Rosetta 2
   / ReXGlue "populated once at startup"). No per-call resolution for AOT hits.
3. **Runtime fallback.** Indirect call → table lookup; on miss (late-discovered
   target, loaded module), JIT the function via the existing backend, register
   it in the table, continue. W^X guarantees no re-translation.
4. **Background optimization (HQEMU lesson).** Do heavy LLVM optimization off the
   guest-critical thread so translation latency never stutters gameplay.
5. **GPU as a service.** Route Xenos/D3D9 draws to the Vulkan HLE behind an
   abstract interface (ReXGlue/RT64 pattern) — the TBDR wins (GMEM-resident RTs,
   pass fusion) land here.

**Net:** the hybrid gives the AOT residency/perf win (no per-call context
round-trip, whole-program optimization, no runtime translation for the hot bulk)
*and* full generality (the JIT fallback runs anything static missed) *and*
reuses the project's entire existing stack. It is the architecture, not a
compromise between two.

## Build plan against xenia-thor's current backend (code-grounded, 2026-07-18)

**Key finding from reading the code: the backend is already hybrid-shaped — it is
JIT-*first*, and the inversion to AOT-*first* is mostly coverage + persistence +
tuning on existing machinery, plus one hard perf piece.**

What already exists (maps 1:1 to the Rosetta-2 blueprint):
- **The dispatch + fallback IS Rosetta-2's.** `Processor::ResolveFunction`
  (processor.cc:357): `entry_table_.GetOrCreate(addr)` → if `STATUS_NEW`,
  `DemandFunction` compiles it now; if `STATUS_READY`, return it. So an indirect
  call to a not-yet-compiled address JITs on demand and caches in the entry
  table. **That is exactly "look up the target; on a miss, JIT it, register it,
  continue."** The hybrid fallback seam already works — the code is simply
  JIT-first because nothing is pre-populated.
- **Static function discovery already exists** (`XexModule::RefillPrecompileWork`,
  xex_module.cc:1216): two sources — (1) the call-graph frontier (functions
  DECLARED as direct-call targets but not yet DEFINED; pdata-independent, works
  on no-pdata titles like Burnout) and (2) pdata/exception-directory entry
  points. This is the XenonAnalyse-equivalent AOT work-list, already built.
- **The precompiler** (`PrecompileGuestFunctions`, xex_module.cc:1262): parallel
  workers, load-window-only, JOINED before gameplay.
- **The persistent AOT cache** (`cpu_llvm_object_cache`): per-function `.o`
  keyed by guest addr + code hash, ~98% warm hits.

The three changes to make AOT primary (in effort order):
1. **Pre-populate the dispatch table at load** = run the precompiler to full
   static-discovery coverage and register results so `ResolveFunction` hits
   `STATUS_READY` for the bulk at gameplay; `DemandFunction` then fires ONLY for
   the residue (indirect-only targets, late-loaded modules). Mostly wiring the
   existing precompiler + entry table. **Known blocker (memory):** the
   precompiler serializes on the global compile mutex (no per-thread
   LLVMContext) → give each worker its own LLVMContext for real parallelism.
2. **True warm-skip cache** ("INCREMENT 2", not yet done): on an object-cache
   hit, `LLJIT::addObjectFile` and skip IR-build + the O2 pipeline entirely
   (today the cache skips only codegen, so warm launches still rebuild IR + O2 =
   modest win). This makes a warm launch a near-instant AOT load — the
   Rosetta-2 `.aot`-reuse behavior.
3. **Direct guest→guest calls = the real perf win, and the hard part.** Today
   every guest call round-trips the full register file through PPCContext
   (`WriteBackCtxRegs`/`ReloadCtxRegs`, llvm_assembler.cc ~281) — the residency
   trap. The whole-program-optimization win Rosetta 2 / XenonRecomp get requires
   AOT functions calling each other with a host calling convention and registers
   resident across the boundary. The in-code fix is scoped (the per-call-site
   **host return trampoline**: a `blockaddress` registered as the `x0` the callee
   branches back to) but a prior inline-cache attempt crashed BD at opt=2 — this
   is the genuine new engineering, and the biggest lever.

Sequencing: (1)+(2) are low-risk reuse that deliver the "no runtime translation
for the hot bulk" + fast-warm-load wins and prove the AOT-primary shape on the
Thor. (3) is the multi-week piece that unlocks the cross-function residency /
whole-program win — do it after (1)+(2) validate the direction. Generality (the
JIT fallback) is free — it already ships.

**Escape-hatch note (W^X-simplified):** because the 360 forbids SMC, the JIT
fallback never needs code-invalidation/re-translation — a missed target is
compiled once and lives in the entry table forever. So keeping the existing JIT
as the fallback is nearly zero extra maintenance.

## Sources
- Rosetta 2 RE: https://ffri.github.io/ProjectChampollion/part1/ ; https://dougallj.wordpress.com/2022/11/09/why-is-rosetta-2-fast/
- RPCS3 arm64 + fallback: https://blog.rpcs3.net/2024/12/09/introducing-rpcs3-for-arm64/
- HQEMU + persistent cache: https://dl.acm.org/doi/10.1145/2259016.2259030 ; https://www.usenix.org/system/files/conference/atc16/atc16_paper-wang.pdf
- N64Recomp / XenonRecomp dispatch + jump tables: https://github.com/N64Recomp/N64Recomp ; https://github.com/hedge-dev/XenonRecomp
- Fully-static frontier: https://arxiv.org/pdf/2605.08419
- Binary translation overview: https://en.wikipedia.org/wiki/Binary_translation
