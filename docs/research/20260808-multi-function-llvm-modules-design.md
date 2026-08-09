# Multi-function LLVM modules — the only remaining change shaped like a large win

**Status: DESIGN ONLY, nothing implemented. Scoped 2026-08-08, device-free.**

## Why this and not another lever

Everything measured on 2026-08-08 says the guest thread is **memory-bound at guest call boundaries**, and that
every remaining lever is capped by two structural facts:

1. **`Assembler::Assemble(GuestFunction*, hir::HIRBuilder*)` takes ONE function** (assembler.h:40), and
   `PPCTranslator::Translate` is invoked per function (ppc_translator.cc:257 → :369). The LLVM backend therefore
   builds **one `llvm::Module` per guest function** (llvm_assembler.cc:2449).
2. Guest calls lower to **`xe_llvm_guest_call` / `xe_llvm_call_extern` / `xe_llvm_resolve_function`** — opaque
   external runtime helpers (:418/:435/:459).

⇒ **The O2 pipeline runs on a module containing exactly one function whose callees are invisible. The inliner
cannot fire. No interprocedural pass can see anything.** Combine with AAPCS64
(`docs/reference/arm/aapcs64-callee-saved-notes.md`) and the box is closed:

| across a guest call | can guest state stay in host registers? |
|---|---|
| GPRs | ≤ 8 (verified in emitted code) |
| FPRs | ≤ 8 (low 64 bits of v8–v15) |
| **VMX (128-bit)** | **no — no host register preserves 128 bits** |
| **by inlining the callee away** | **no — the callee is an external helper** |

**That is why LLVM register residency bought thermals but no speed.** It keeps registers resident *within* a
function; call-dense guest code leaves that function constantly and everything round-trips through the 2 KB
`PPCContext`.

**This is also precisely what XenonRecomp (the Sonic Unleashed recompilation) does differently.** It emits the
whole program as C++ in ONE translation unit, so clang inlines across callees and keeps state in registers where
we hit a wall. **Their advantage is compilation-unit scope, not better codegen.**

## The change

Lower a **call-graph CLUSTER** of guest functions into ONE `llvm::Module` with internal linkage, so calls
*within* the cluster become real LLVM calls the inliner and register allocator can work across.

### What has to change, in dependency order

1. **A batch entry point.** `Assemble()` is per-function by signature. Add a parallel batch path
   (`AssembleCluster(span<GuestFunction*>, span<HIRBuilder*>)`) rather than changing the existing one — the a64
   backend implements the same interface and must not be disturbed.
2. **A clustering pass, and the AOT precompile is the natural home.** It already walks every reachable function
   at load (`xex_module.cc`), which is exactly where the call graph is known. Cluster by static call edges with
   a size cap (LLVM compile time and peak memory both scale badly).
3. **Intra-cluster calls become direct.** The win only materialises if a call whose callee is in the same module
   emits a real `call @guest_fn` instead of `xe_llvm_guest_call`. Calls LEAVING the cluster keep the helper.
4. **Object cache keying becomes per-cluster.** Today the key is `g<addr>_<codehash>` per function
   (llvm_assembler.cc:2800). A cluster key must hash **every member's** code bytes.

## The four things that will bite, listed before anyone starts

- **Cache invalidation granularity.** One changed guest function invalidates the whole cluster. With 18k+
  functions and a 264 MB cache this is the difference between a 12s warm start and a multi-minute recompile.
  **Cluster size is a cache-churn parameter, not just a compile-time one.**
- **The ABI hazard is REAL and already documented.** a64 code clobbers `x22-x28` and the **full q8-q15**, while
  AAPCS preserves only the low 64 bits of v8-v15 (llvm_assembler.cc ~:407). A cluster-internal call can use a
  private convention, but **any call that may reach an a64-compiled callee must not keep values in those
  registers**. Getting this wrong is silent state corruption, not a crash — and an attempt to fix it with an
  inline-asm clobber barrier on 2026-08-08 did NOT resolve the related vmaddfp miscompile.
- **Compile time and memory.** Clusters multiply both. The AOT pass already runs 261-340% CPU and is the
  startup heat; a naive clustering could make load times far worse for a runtime win.
- **It does not help interpreted/fallback functions.** 194 functions still fall back (all vector
  `mul_add`/`mul_sub`). A cluster containing one poisons the direct-call benefit for its neighbours.

## How to size it BEFORE building it (rule 4)

**Do not start with the implementation.** Two cheap measurements decide whether it is worth anything:

1. **Call-graph locality.** Instrument the AOT walk to report, per function, how many of its call targets are
   *also* in the precompiled set and would land in a plausible cluster (say ≤32 functions). **If most guest
   calls leave any reasonable cluster, direct calls never happen and the whole idea collapses.**
2. **How much time is actually at call boundaries.** The residency levers give a proxy: they measured 4-5°C
   cooler at equal throughput, which says the context traffic is real but did not show up as speed. Quantify the
   per-call save/restore cost directly before assuming clusters recover it.

**If (1) shows poor locality, this design is dead and should be recorded as such** — which would be a cheap,
valuable negative, and is the outcome the rest of 2026-08-08 would predict.
