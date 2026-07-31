# ReXGlue / XenonRecomp lineage survey (2025-07 → 2026-07) — adoptable items

Full sourced report from the research agent, condensed. Landscape: upstream
hedge-dev/XenonRecomp near-dormant; action moved to sonicnext-dev's fork
(MarathonRecomp), rexglue/rexglue-sdk (v0.9.0 2026-07-31 — inherited XENIA's
kernel+GPU and swapped the JIT for AOT C++, our closest cousin), and
sp00nznet/360tools (ran ReXGlue across 747 XBLA titles — best pitfall catalog).

## Tier 1 — correctness (adopt first)
1. **setjmp/longjmp vs register residency** (MarathonRecomp #122): our
   cpu_backend_llvm_residency_abi must spill residents to context before a
   guest setjmp snapshot and reload after longjmp restore; their fix also had
   an ordering bug (longjmp return value written to r3 BEFORE ctx restore →
   clobbered). Same discipline needed for guest C++ EH unwinds. This is THE
   known way "ABI-compliant" games break non_volatile_as_local.
2. **Conditional bcctr is NOT a block terminator** (rexglue 10cf1ad):
   bnectr/beqctr fall through; treating them as terminators drops function
   tails. Audit our whole-function block discovery.
3. **Switch-aware bctr walker** (XenonRecomp PR #185): key jump-table map by
   the bctr INSTRUCTION VA (not table base, NOP padding breaks that), feed
   case labels as successors, exempt those blocks from gap pruning. 97.4% of
   "switch case outside function" warnings resolved. Also rexglue PR #175
   (tables truncated at code-region boundaries) + #370 (manual hints).
4. **lhbrx/lwbrx family audit** (MarathonRecomp b1b279be): upstream's lhbrx
   had NO swap. Ours should fold byte-reversed ops to native-order LE loads
   (correctness + perf). mffs targets an FPR not GPR (upstream PR #158).
5. **VMX128 decode bugs**: VD3D0 operand mask 3 bits→7 (ppc-dis.c,
   MarathonRecomp #113); binutils dialect priority lets PPC405 ops shadow
   VMX128 encodings (rexglue nightly 2026-05-01). Add to decoder test corpus.
6. **vpkd3d128 write-mask semantics are a trap** — rexglue shipped and
   REVERTED a mask=2/3 zero-clear "fix" (cbb7d9e0). Test any change against
   interpreter reference on real traces.
7. **Tolerate null-page reads and null indirect calls** (360tools): most
   common boots-then-crashes causes across 747 titles. Their fixes: zero page
   readable returning 0 (config), non-fatal logged no-op for null/unresolved
   indirect calls. Our indirect-call path should have a defined non-fatal
   behavior.
8. **Kernel structs in guest memory must be big-endian** — games read
   RTL_CRITICAL_SECTION recursion counts directly (Sonic '06). Sweep ours.

## Tier 2 — perf/architecture
9. **ARM64 FPSCR→FPCR shadow with lazy msr** (XenonRecomp PR #22 + simde):
   per-thread host-CSR shadow, mode switch only when needed, LUT rounding
   translation; simde's NEON lowerings for VMX packs/shifts are a crib sheet.
10. **Indirect-call dense table**: table[(pc - code_base) >> 2] load+call,
    per-module tables keyed by module code_base (multi-XEX DLL titles;
    rexglue had a wrong-code_base bug #371). Beats hashing/ICs for full-AOT.
11. **Residency beyond non-volatiles**: XenonRecomp localizes CTR, XER, the
    lwarx/stwcx reservation, ALL CR fields, and non-argument volatiles. CR/CTR
    are safer than non-volatiles (no cross-call contract) — next lever after
    residency_abi. setjmp discipline (item 1) applies to CR too.
12. **PPCContext layout must not depend on codegen flags** (rexglue 8e82d03d)
    — with an AOT cache the context layout is ABI; fold lever settings into
    the cache key instead.

## Tier 3 — pipeline/caching/triage
13. **Iterative boundary discovery**: 16%→99% title codegen success by
    auto-injecting function hints per UnresolvedCall and re-running (~7
    hints/title, cap iterations — a few titles diverge into hundreds). We can
    run the same loop in-process at load; expect residual → per-fn fallback.
    Watch for emulator-wrapper XBLA (giant 1MB "functions") — size-cap to the
    a64 backend.
14. **Cache keying**: rexglue abandoned hash-skipping (a5b923b4) after stale
    mixed artifacts — key on XEX hash + backend version + ALL codegen levers,
    version-gate hard. Nobody in the lineage handles SMC; ours stays
    write-protect + invalidate-to-fallback.
15. **Safepoint polls** (rexglue admission): no polls = no clean teardown;
    they kill the process. We already built OPCODE_CHECK_PREEMPT (scheduler
    stage 2) — reusable for teardown/tiering.
16. **Crash triage**: per-function guest-VA↔host-PC maps + guest-symbolized
    stacks on host AVs (360tools crash_diag pattern). Our a64 CRASH DIAG has
    half of this; extend to the LLVM ORC ranges.
