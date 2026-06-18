# Deep reimagination of xenia-on-Thor — reconciled (our 3 passes + Codex gpt-5.5 + 4-agent paper workflow)

Date 2026-06-18. The user asked: rearch xenia around the Thor hardware, deep reimagination, latest papers,
ask Codex+Gemini, the "Thor is 10-20x the 360" framing. Three independent analyses (our prior 3 design
passes; an external Codex gpt-5.5 consult; a 4-agent latest-papers workflow + red-team) CONVERGE. Gemini
errored (empty response). This doc reconciles them + a device-data correction to their shared top pick.

## The 10-20x, honestly decomposed (all three agree)
It is a FLOPS/core ratio, NOT a usable-speedup ratio:
- **~1.3x is the real native CPU scalar ratio** (Thor ~12.8 GIPS vs 360 ~9.6). The other ~10x of the "CPU
  gap" is PARALLEL throughput the guest CANNOT consume: the 360 game targets 3 in-order PPE threads that
  DIVERGE (vectorizing them = the proven-wrong technique), and our CPU-bound title (Burnout) binds on ONE
  branchy guest XThread. You can't split one thread's data-dependent control flow across 7 idle cores.
- **The ~10-15x GPU FLOPS gap is real but BD is OVERDRAW-bound, not FLOP-bound** (constant-color FS = no
  change -> ALU/FP16 DEAD; coverage-collapse = 22x -> per-covered-fragment raster+depth+alpha-test). More
  FLOPS don't help; FEWER COVERED FRAGMENTS do.
- **Net: ~2-3.5x is reclaimable on the right title via the right unit; the rest is intrinsically latent.**
  The shipped blocking-fence-poll fix (+46-78%) already took the single biggest reclaimable slice -> "near
  ceiling" is CORRECT for generic xenia.

## The genuine live levers (reconciled)
**CPU — idle-cores-as-COMPILERS/PREPROCESSORS, not parallel guest execution** (Codex's central insight +
the workflow's Unit 3/4/5):
- **Lock-free code-cache LOOKUP** (read-side only; ARMv9 LSE CAS insert + RCPC/LDAPR weak-acquire read).
  NOT parallel compilation (that DEADLOCKS - proven). Removes per-guest-call lock overhead. Safe,
  device-free-validatable (qemu + ThreadSanitizer + host call-graph), modest single-digit % on Burnout,
  zero precision risk, prerequisite for any future safe pre-warm. QEMU-MTTCG/QHT precedent. **BUILD FIRST.**
- **ThorPack / non-blocking tiered trace-specializer** (Codex #1-2): bg cores consume IMMUTABLE trace
  records -> candidate direct PPC->A64 superblocks in private arenas -> a single publisher atomically swaps
  block entries at safe points; NO worker takes the recursive global lock (threads the deadlock wall by
  construction). Papers: learned-translation QEMU +1.36x, direct-translation "IR dominates", mixed
  cross-compilation up to 13x for offloadable fns. Kill test: hand-write a direct backend for Burnout's top
  branchy fn's 20-50 hot blocks, HIR fallback at every guard, diff guest state 10k calls; <15-25% = dead.
- **In-block FLAGM2 + leaf-inlining** (the sanctioned cross-barrier-wall survivors). Modest; Burnout-only.

**GPU/BD overdraw — reduce COVERED FRAGMENTS (the measured cost), content-preserving:**
- ⚠️ **DEPTH-PREPASS (both external analyses' #1 pick) is NET-NEGATIVE for BD per OUR device data — they
  under-weighted "constant-color FS = no change."** Math: a depth-only alpha-test prepass + EQUAL-test color
  pass costs `covered×(2*raster + alpha-test) + visible×FS` vs original `covered×(raster + alpha-test + FS)`
  = net `+covered×raster - (covered-visible)×FS`. Since FS is CHEAP (constant-color = no change -> FS~0),
  the saving term ~0 and you ADD a full raster pass = WORSE. This is why our opaque depth-prepass was
  net-negative too. The prepass doesn't reduce the alpha-test count (the bottleneck) - it DUPLICATES it.
  Test cheaply to confirm-dead if desired, but do NOT expect a win.
- ✅ **Alpha-geometry tightening (Codex #5) = the real content-preserving BD lever.** Tighten foliage quads
  to the alpha SILHOUETTE at load (conservative marching-squares / 8x8-16x16 alpha tiles) -> FEWER empty
  covered fragments rasterized -> directly cuts the raster+alpha-test cost AT THE SOURCE, never removing a
  visible alpha pixel. Cuts the actual bottleneck without the depth-prepass's duplication. Kill test: one
  BD grass texture family, replace quads with conservative meshlets, keep the original alpha shader, measure
  covered-fragment drop on the frozen frame.
- **VRS (`fragment_shading_rate`)**: reduces FS/alpha-test invocation rate (cuts the cost) BUT coarsens the
  alpha-test = blocky foliage edges (quality), and the extension is UNCONFIRMED on the 740 (probe
  `vkGetPhysicalDeviceFragmentShadingRates` first). Quality-toggle class, like the shipped thinning.
- **Shipped thinning toggles** (foliage/blended) remain the proven BD lever (2.2x, cuts the fragment count).

## DEAD (all three agree — papers good, device says no)
Compute-ROP / paraLLEl-GS (needs FSI/ROAA = absent; LucidRaster 2024 = 3x slower than HW even for
transparency on a discrete GPU; worse on a 99%-busy mobile TBDR); `VK_QCOM_tile_shading`/tile_memory_heap
(840+); GPU async-compute overlap (single queue); 3-PPE->NEON fusion (threads diverge); frame
interpolation/reprojection (adds latency + competes for the overdraw-bound GPU); int8/bf16/FP16 NEON for
guest VMX (precision - black-screened BD); parallel lazy-compile JIT (deadlock); cross-barrier state
elision (shared PPCContext - black-screened BD); LLVM (~40% slower than hand-JIT); per-draw merge as a BD
lever (device-bounded ~2.4%; BD is overdraw- not draw-bound).

## Is the ceiling broken? Honest verdict
NO by the papers, codegen, or more cores. xenia-on-Thor is near its ceiling GIVEN the current renderer's
overdraw handling. The remaining honest unlocks: (1) alpha-geometry tightening on BD overdraw
(content-preserving, attacks the measured cost - NOT the depth-prepass, which our data says is
net-negative); (2) idle-cores-as-compilers (lock-free lookup -> ThorPack trace-specializer) for modest
CPU-bound wins; both stack on the shipped fence-fix + thinning. The dramatic 10-20x is mostly latent +
structurally unconsumable by this workload.

## Build order
1. **Lock-free code-cache lookup** (CPU, device-free-validatable, safe, prerequisite). 
2. **Alpha-geometry tightening prototype** on one BD grass texture (the real content-preserving overdraw lever).
3. ThorPack Burnout-hot-fn kill-test (the bold CPU bet). 4. VRS probe (one log line) then test if present.
Sources: scratch/consult/rearch/codex.md, scratch/rearch_v4.txt, docs/research/20260617-*, the device data
in bd-real-bottleneck-overdraw.md.
