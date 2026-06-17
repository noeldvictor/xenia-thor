# Frontier hardware-accel rearchitecture — online research + imagination (2026-06-17)

Source: ultracode web-research workflow (Rosetta2/FEX/Box64/QEMU/RPCS3-ARM64/paraLLEl-GS) +
adversarial red-team that verified every load-bearing claim online. User direction: "deep hardware
accel rearch design research ONLINE — we need IMAGINATION, not regular software work."

## Hardware reality checks the red-team CONFIRMED (kills hype)
- `VK_QCOM_tile_memory_heap`/`tile_shading` (direct-GMEM-as-EDRAM) = **Adreno 840+ only**; the Thor's 740
  exposes only `VK_QCOM_tile_properties` (query tile geom, can't map GMEM).
- Apple M1 "free TSO toggle + hardware PF/AF flag bits" = **Apple-custom silicon**, absent on stock
  ARMv9.0. Only the **FlagM2 instructions** (RMIF/SETF8/SETF16/CFINV) + RCPC ordering transfer.
- Adreno 740 (A7xx) = **single graphics+compute queue** (concurrency is internal BR/BV binning), NOT
  AMD-style separately-schedulable ACE queues → "async-compute PM4 split" is sci-fi here.
- Rosetta-2's speed is NOT the FlagM instructions per se — it's the **"unused flags" cross-block lazy
  elision**, which is EXACTLY our [[cross-barrier-elision-wall]]. So only the IN-BLOCK flag fusion is safe.

## The 6 bold ideas, red-teamed + reconciled with our device data
1. **Compute-shader Xenos ROP back-end** (paraLLEl-GS for the 360). VERDICT: an ACCURACY/capability
   moonshot, NOT a BD-speed fix (software ROP re-prices fragments, may go UP on mobile iGPU; paraLLEl-GS
   needs a discrete GPU + isn't even bit-exact). Park as a correctness play.
2. **Vectorized 3-PPE emulation** (pack guest threads into NEON lanes, Gamozo-style). VERDICT: **SCI-FI
   for this guest** — Gamozo's technique is N-inputs-ONE-program (fuzzing); 360 threads are
   engine/physics/audio = DIFFERENT code that diverges permanently; author says it collapses in this
   regime. KILL-TEST (1 counter): how often are >=2 PPE threads at the same basic block simultaneously.
3. **FlagM2 in-block CR/XER->NZCV fusion** (RMIF/SETF/CFINV + `b.cc`). VERDICT: **GENUINELY PROMISING,
   lowest-risk CPU win** — extends our validated rlwinm/CR plumbing; keep IN-BLOCK (cross-block = the
   wall). Modest (single-% guest_ms) but cheap + safe. Aligns with [[ppc-thor-hw-accel-rearch]] Units 3/4.
4. **Hexagon NPU vertex-unpack preprocessor**. VERDICT: precision-perfect (only integer unpack, zero FP
   risk) BUT economically suspect — verified FastRPC = 75us-several-ms BLOCKING round-trip; our own
   POS-binning data caps the upside. KILL-TEST: time the integer-unpack slice on NEON first (<1-2ms => dead).
5. **Opaque depth/visibility pre-pass** (alpha-test discard disables Adreno LRZ; pre-fill opaque Z so
   alpha-test draws early-Z-reject). The research's TOP PICK. **⚠️ SESSION CORRECTION (the research didn't
   know): WE ALREADY BUILT THIS and it's NET-NEGATIVE on BD** — BD's foliage is FOREGROUND, terrain depth
   rejects nothing; the overdraw is alpha-test-OVER-alpha-test (foliage layers), which LRZ can't help
   (discard disables LRZ). So #5 is DEAD for BD's specific overdraw. The shipped foliage/blended THINNING
   toggles (2.2x) are the real BD-overdraw lever. #5 may still help opaque-overdraw titles (BTTF/Gears).
6. **Lock-free PPCContext + RCPC/LSE ordering.** Stage A (replace blanket `DMB` with `LDAPR`/`STLR`
   weak-acquire on guest atomic/sync patterns) = guest-invisible, low-risk, ~few-% (Box64 precedent),
   **genuinely NEW + pursuable**. Stage B (versioned-shadow optimistic STM over PPCContext) = collides
   with the shared-context + parallel-JIT-deadlock walls; gate hard behind Stage A + an abort-rate probe.

## HONEST CONSOLIDATED VERDICT (with our session context)
- **BD/GPU-overdraw**: already addressed by the shipped thinning toggles; depth-prepass (#5) refuted;
  compute-ROP (#1) is accuracy-only. No new big BD GPU win in the research.
- **CPU-bound (Burnout/LO)**: the research VALIDATES + EXTENDS the codegen rearch — #3 FlagM2 in-block
  fusion and #6A LDAPR/STLR fence replacement are the grounded, low-risk, NEW wins (both pursuable now,
  device-free-validatable). These join [[ppc-thor-hw-accel-rearch]].
- **Moonshots** (#1 compute-ROP, #2 vectorized-PPE, #6B STM) are reframed/demoted honestly.
- Stacked credible upside ~2-4x on CPU-bound titles via FlagM2 + fence-replacement + the existing
  codegen units; NOT a single silver bullet.

## NEXT (grounded, cheapest-to-falsify, device-free first)
1. **#6A DMB->LDAPR/STLR fence audit** — find xenia's current guest-barrier emission (a64 backend), see
   if blanket DMB can be weak-acquire on guest atomic patterns; host/qemu validate; A/B a multithreaded title.
2. **#3 FlagM2 in-block cmp+bc fusion** — count in-block CR0-write->CR0-branch pairs on Burnout/LO; swap
   the fast-path to SETF/RMIF+`b.cc`; A/B. (Merges with the rearch plan.)
3. Cheap KILL-TESTS (single counters, falsify before building): #2 PPE basic-block coincidence; #4 NEON
   unpack-slice timer.

Full agent output: scratch/frontier_vision.txt (+ the task json). Sources verified online (paraLLEl-GS,
dougallj Rosetta2, Qualcomm Adreno LRZ + Hexagon FastRPC docs, Box64 RISC-V TSO, Chips-and-Cheese A7xx,
Gamozo vectorized-emulation, RPCS3-ARM64).
