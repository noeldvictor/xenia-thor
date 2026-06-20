# Next-gen PowerPC→ARM64 dynamic-translation techniques — ranked for xenia-thor (2026-06-20)

A 7-agent multi-modal web/arxiv sweep (arxiv DBT, Rosetta-2/Box64/FEX, Dolphin/RPCS3 emulator community,
ARMv9 HW features, AOT/ML/novel) synthesized + adversarially realism-checked against what the a64 backend
**already does**. Verdict sound=True. The point: surface GENUINELY NEW levers, not re-proposals.

## ⭐ TOP NEW LEVER — lazy carry (XER[CA]) + flag NZCV residency  [CPU-bound titles: Burnout, LO]
- **CONFIRMED GAP:** ppc_emit_alu.cc materializes carry EAGERLY (AddDidCarry/CompareUGT→StoreCA) for every
  adde/subfe/addze/subfze (lines ~105-562); the carry is never kept resident in the host C flag. DCE only
  drops the dead STORE *after* the value is computed. The shipped FlagM ADD_CARRY (rmif) is a DIFFERENT
  in-block primitive, not chained-carry residency.
- **Why it's the right lever:** it's exactly what every gold-standard translator relies on (Rosetta NZCV
  residency, FEX flag-residency, Box64 Kildall lazy flags, Dolphin PR#13251 carry-state). It is strictly
  WITHIN-BLOCK → sidesteps this codebase's cross-barrier-elision wall + the parallel-JIT race. Composes with
  the shipped FlagM/known-bits/const-fold (CFINV already detected for the subfc borrow flip). Attacks the
  documented Burnout CPU-bound bubble + LO game-logic.
- **Honest magnitude (adversarial correction):** LOW SINGLE DIGITS on integer-heavy CPU-bound code — the
  FEX +17.6% / "Dolphin ~2x" figures are the x86-TSO / FP-heavy cases, NOT transferable to a mature
  hand-JIT here. Zero on GPU-bound BD/BTTF. Still the cleanest unmined codegen win + it stacks.
- **Build (3-5 days, MED risk, device-free qemu/host-testable):** add a CarryState tracker (ConstantFalse/
  True / InContext / InHostCarry) to the a64 emitter; emit ADDS then ADCS for chained adde/subfe; fold
  constant-carry addze/subfze; spill CA to context only when it escapes the block / crosses context_barrier.
  Default-off cvar; gate on the green 353-test qemu+host differential suite; XeniaOptimizations toggle.
- **First unit:** CarryState field + a host/qemu-a64 test that adde→adde→adde lowers to ADDS;ADCS;ADCS (one
  flag producer, no per-op StoreCA) and is bit-identical to the eager path.

## DO FIRST (cheap, near-zero-risk, unblocks others) — broaden the a64 feature probe
- platform_arm64.cc InitFeatureFlags (~:50-77) currently detects ONLY kA64EmitLSE (HWCAP_ATOMIC), kA64EmitFlagM
  (AT_HWCAP bit 23), kA64FZFlushesInputs (runtime probe). Add getauxval(AT_HWCAP/HWCAP2) detection for
  **rcpc/ilrcpc (LDAPR), lse2, jscvt, fcma, asimddp(dotprod), bf16, i8mm** + log them. Pure detection (no
  codegen change) → inert by itself but unblocks the LDAPR lever + exposes fcma/dotprod/i8mm for future
  heuristic units. ~half a day. (All present on the Thor per /proc/cpuinfo.)

## INCREMENTAL grind (LOW risk, each local + qemu-tested) — macro-op / peephole fusion
- rlwimi → ARM64 **BFI**; known-bits-proven rlwinm masks → **UBFM/SBFM** bitfield-move (1 op vs shift+AND);
  mulli → shift/add; cror/crand feeding a single branch → **CCMP** chain (cmp+ccmp+b.cond). rlwinm fast-path +
  known-bits-fold + compare→branch fusion already shipped, so these are the residual tail. CPU-bound only.

## ALREADY DONE — do NOT re-propose (adversarial-confirmed against the code)
- **compare→branch NZCV residency**: SHIPPED = TrySelectSingleCompareBranch (a64_sequences.cc:6983, cvar
  arm64_single_compare_branch_fusion) emits cmp+B.cond for all 10 COMPARE_* whose dest feeds the next branch.
- BL/RET return-address-stack pairing (host blr + epilog ret); block-linking via the indirection table;
  LSE CAS (casal, ldaxr/stlxr only as fallback); membase + PPCContext(x20) register pinning; NEON VMX128 1:1;
  ADPF PerformanceHint + Thermal (command_processor.cc); FlagM ADD_CARRY; known-bits/const-range folds;
  lock-free entry lookup; csel/fcsel/fccmp SELECT fusion.

## OVERHYPED / SKIP (claims don't transfer to this HW/backend)
- **LDAPR/LRCPC barrier weakening**: the "~10x DMB cost" is the x86-TSO-on-ARM case (Rosetta/FEX faking TSO
  per store). Xenon is WEAKLY-ordered (explicit lwsync/sync) → no per-access barrier to weaken. Bounded upside
  (lower lwsync→ISHST + elide redundant barriers + LDAPR acquire-loads), cross-thread-sync only. LOW priority.
- **Persistent on-disk JIT code cache**: attacks BOOT/STUTTER not steady fps, multi-day relocation risk, and
  JIT may be a MINORITY of the ~80s boot (ISO mount/asset/movies) — MEASURE the boot breakdown before building.
- **Trace/superblock formation**: cross-barrier guest-state wall; the sanctioned cross-block path is
  leaf-INLINING. Fold into the tier-1 container, don't build standalone.
- **FJCVTZS**: wrong op (xenia uses saturating fcvtzs for fctiw*; FJCVTZS is JS/x86 wrap semantics). Dead here.
- **ML/LLM transpile (~49% correct), GPU/NPU-assisted translation**: research-watch only, disqualifying for
  bit-exact emulation / latency.
- **cross-call GPR pinning, saSLP vector remap**: closed wall / already-symmetric NEON. Skip.

## PROFILE-GATE (don't build blind)
- **Indirect-dispatch inline monomorphic cache** (bctr/bctrl + the blr return-miss path): the common return is
  already RAS-predicted (CallIndirect single-target guess). simpleperf Burnout's guest Main first to confirm
  indirect dispatch is hot before building.

## Container (the architecture this all wants) — hot-block 2nd optimizing tier
- xenia is effectively single baseline-tier; cpu_emit_jit_perf_map already finds hot guest PCs. A per-fn exec
  counter + promotion threshold that re-JITs hot fns with the optimizing passes (lazy-carry/CR + fusion +
  leaf-inline) is the DELIVERY VEHICLE — PURSUE AFTER the passes exist (nothing to promote until then).

## Build order
1. feature-probe broadening (½ day, unblocks) → 2. lazy-carry residency (the win) → 3. macro-fusion grind →
4. profile-gate indirect-dispatch → 5. tier-1 container to promote hot fns. Each ships default-off, qemu+host
353-test gated, as a stacking XeniaOptimizations toggle. CPU-bound titles only (Burnout/LO); GPU-bound BD
unaffected (its lever is the shipped overdraw thinning).
