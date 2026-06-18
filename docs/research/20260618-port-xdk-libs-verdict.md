# Can we PORT core Xbox 360 libs instead of emulating them? — verdict (12-agent analysis)

Date 2026-06-18. User: "instead of emulating core xbox 360 libs can we port them?" Ran an 11-agent
workflow (map surface → assess portability + cross-emulator precedent → red-team precision/state/ROI →
synthesize), grounded against the fork's actual code. This is the reconciled verdict.

## Verdict: partial yes — and most of it is ALREADY done
- **Kernel (xboxkrnl), XAM, XMA audio are already HLE-ported.** The `sc 2` import-thunk → `export_resolver`
  → `RegisterExport` trampoline path natively reimplements ~250 xboxkrnl + ~173 xam exports; XMA runs
  host-side via FFmpeg (`src/xenia/apu/`). These are not the open question.
- **The open target is the statically-linked XDK CRT** compilers inline into each title's `.text`
  (15-30% of `.text` is byte-shared XDK runtime across BD/LO/Burnout/Banjo). No import thunk → the only way
  to intercept is CONTENT RECOGNITION = exactly the shipped `cpu_shared_function_fastpath`.
- **Only byte/integer-exact, stateless, callback-free, non-allocating LEAF kernels can be safely ported**
  (memset/memcpy/memmove already wired; memcmp/strlen/strcmp/strncmp/memchr trivially addable). Everything
  float (libm), heap (malloc/new), callback-bearing (qsort), or stateful (printf/rand) cannot. The
  framework is sound and ~80% built — but it is **CPU hygiene, not an fps unlock.**

## The honest ROI (two facts verified in-code that bound it)
1. **There IS a real steady-state delta — JIT memset is dcbz-only.** `a64_seq_memory.cc:1120-1166`:
   `OPCODE_MEMSET` asserts `src2.constant()==0` ("Only used by dcbz/dcbz128"). So a guest
   `memset(buf,value,count)` is NOT lowered to a bulk op — it JIT-translates as a per-iteration PPC store
   loop (each store on the store-watch/MMIO path). A native `std::memset`/`std::memcpy` collapses that to a
   vectorized libc call. So the red-team's "the JIT already does a tight loop" is WEAKER than it sounds.
2. **…but the win is bimodal AND hit-rate-capped.** Bimodal: positive only on large/hot byte counts; for
   the dominant tiny/cold calls the `CallExtern + CallIndirect(LoadLR())` round-trip is comparable-or-worse
   than the inlined JIT loop. Hit-rate cap: the scanner's forward-tail-call heuristic is COMMENTED OUT
   (`ppc_scanner.cc:195-202`, "too aggressive") — leaf CRT kernels are usually reached by tail-`b`, which
   folds the callee into the caller's hashed range → the standalone-leaf hash never matches → safe JIT
   fallback, no win. **"15-30% shared" is a code-SIZE share, not a runtime share.**
- **Measured bottlenecks gut the payoff:** BD GPU-bound (CPU 75-85% idle → porting does nothing); Burnout
  CPU-bound on title-specific game-logic (hashes uniquely → cross-title recognizer never fires); XMA +
  kernel/XAM already host-side. The only plausible win = the unattributed Gears-class "~24% JIT'd guest
  code" IF it is hot large-count CRT churn — currently UNMEASURED. Expect single-digit-% CPU relief on
  CPU-bound, non-middleware titles only, IF a harvest proves a hot leaf.

## Ports cleanly (do first, via the shipped fast-path)
Handler pattern (`ppc_frontend.cc:117-147`): read args `r3..r5`, operate on `memory->TranslateVirtual`,
substituted `MarkLabel; CallExtern(handler); CallIndirect(LoadLR())` (`ppc_hir_builder.cc`).
- **Tier 0 (handlers exist; need only a harvested+RE-confirmed (hash,size) entry):** memset, memcpy, memmove.
- **Tier 1 (new SharedFunctionKind + ~10-line handler each):** memcmp, strlen, strcmp, strncmp, memchr.
  Byte-exact + endian-neutral (ordering/NUL are single-byte facts). Char compares MUST be UNSIGNED (360 PPC
  char is unsigned); each must write `r[3] = (uint64_t)(int32_t)result`.
- Safe BECAUSE: raw bytes, exact integer result, no hidden state, no callback, no allocation = observationally
  identical to the guest CRT bit-for-bit. **No floating point = the entire dividing line.**

## Needs care
- **Float math (sinf/cosf/powf/sqrtf): NEVER call ARM libm.** The 360 CRT transcendentals are software
  fdlibm/MS-CRT polynomials with vintage-specific reduction constants + non-FMA op order; AArch64 libm uses
  a different polynomial + auto-contracts to fmadd (1 rounding vs 2) → 1-2 ULP divergence → guest code that
  hashes/compares float output branches differently → NaN cascade → black screen (proven scar tissue:
  `arm64_vmx_dot_f32_fastpath`, FP16-on-geometry). The ONLY safe float port is a bit-for-bit fdlibm repro
  with `-ffp-contract=off` + guest-FPSCR rounding (the discipline `PpcFrsqrte` already uses). A full-domain
  money-pit for a speculative win no title needs. **Cap the feature at integer/byte leaves forever** — the
  per-op JIT FP is already bit-exact, so a JIT'd guest sinf is already correct.
- **Recognition-safety hardening (do BEFORE populating any entry):** add a prologue+epilogue exact-word 2nd
  gate to `LookupSharedFunction` (store first/last K=4-8 canon words; the size guard alone misses the
  LI-mask structured collision the instant a non-leaf entry is added). Keep entries LEAF-ONLY (zero
  opcode-18 b/bl → canonical-hash == raw-hash). Harvest + RE-confirm PER XDK VINTAGE (aug06/aug07/jun08
  hash differently; drift degrades to a safe miss, never corruption). When ambiguous memcpy vs memmove →
  map to memmove (overlap-safe). RE-confirm each harvested memset is a plain byte-fill before mapping to
  kMemset (dcbz/word-pattern fills are different semantics).

## Do NOT port
- **malloc/free/realloc/new/delete — fatal.** No `RtlAllocateHeap` impl exists; the static CRT malloc carves
  guest-arena objects with headers/free-lists/cookies at guest-visible addresses the guest walks. A native
  host allocator corrupts the moment guest code inspects/frees a block.
- **qsort/bsearch/callback-takers — fatal.** Host port must re-enter the guest comparator per element via
  `Processor::Execute` (full PPCContext save/restore + "assumes 32-bit args" hazard) → net loss + ABI mine.
- **printf/sprintf — already HLE'd; varargs `%f`-from-GPR hazard if self-ported.** rand/srand — guest seed
  determinism (procedural/replay/netsync).
- **Middleware (Havok/PhysX/CRIWARE/Bink/Scaleform/FaceFX) — unportable.** Proprietary, massively stateful
  (state in guest structures the game reads), FP-determinism-sensitive (host Havok desyncs the sim),
  version-fragmented, no clean API boundary. The CPU lever for physics-heavy titles is codegen quality
  (NEON VMX128/FMA), not library porting.

## Next steps (build on `cpu_shared_function_fastpath`)
1. **Harvest-and-measure FIRST (the gate).** `cpu_shared_function_harvest` on a CPU-bound title (Gears/LO),
   logging canonical hash + call frequency + avg byte count + ACTUAL recognition rate (account for the
   tail-call fold). Recognition without a measured hot profile is worthless.
2. **Kill-criterion #1:** no leaf profiles hot → ship nothing, leave the table empty (the correct outcome).
   Redirect to the proven levers (GPU overdraw for BD, codegen for Burnout/LO).
3. **If a leaf proves hot:** add the prologue/epilogue 2nd gate, then populate only RE-confirmed, bl-entered,
   byte-exact entries; ship as a default-off stacking XeniaOptimizations toggle.
4. **Kill-criterion #2 (per entry):** A/B at matched guest_ms + equal rendered, off vs on; drop if within
   noise or negative.
5. **Precision discipline (hard review gate):** no handler may do guest-visible FP, allocate, take a
   callback, or affect determinism. Integer/byte-exact stateless leaves only.

Files of record: `src/xenia/cpu/ppc/ppc_frontend.cc` (CanonicalFunctionHashRaw, LookupSharedFunction,
handlers), `ppc_hir_builder.cc` (substitution), `a64_seq_memory.cc:1120` (memset=dcbz-only),
`ppc_scanner.cc:195` (commented-out tail-call heuristic).
