# Fast-math hardware-exploitation audit — ceiling verdict (2026-06-01)

Comprehensive re-audit of the xenia-thor codebase for unexploited Snapdragon/Adreno fast math
(NEON, dotprod/i8mm/bf16/fcma, FlagM/LSE, Adreno fp16), classifying every finding GUEST-VISIBLE
(must stay bit-exact) vs OUR-INTERNAL/HEURISTIC (approx-safe), with adversarial verification.
Workflow tools/thor/wf_fastmath_audit.mjs, run wf_7c17c42e-2f6 (9 agents).

## Verdict: MOSTLY AT THE CEILING
**Are we taking full advantage of the CPU/GPU fast math, and can we approximate?**
- **Guest-visible math is at/near ceiling.** Every guest-visible op that CAN use a faster
  bit-exact instruction already does: saturating arithmetic (uqadd/sqadd), rounding halving-add
  (urhadd/srhadd = PPC vavg), narrowing/widening packs (sqxtn/uqxtun/sxtl/uxtl), TBL permutes with
  the XOR-0x03 endian remap, native byteswap (rev/rev16/rev32, rev32.16b for V128), LSE atomics
  throughout (casal/swpal/ldaddal gated on kA64EmitLSE), dc-zva memset.
- **CANNOT approximate guest-visible math.** The scalar/software paths that remain are BLOCKED by
  Xenos precision divergence, not laziness — and approximating them breaks games (proven twice:
  the f32 dot-product black-screen, the fp16 hand-roll). The unused hardware (NEON exp2/log2, fp
  dot, frecpe/frsqrte estimates, FCVTL/FCVTN for fp16, fp16 shader ALU, FMA contraction) is unusable
  precisely BECAUSE it diverges from Xenos.
- **Approximation is only free on OUR-INTERNAL/HEURISTIC math**, and the audit found those loops are
  already trivial/cold or dominated by the ShaderInterpreter (vectorizing the wrapper yields ~0).
- **No host CPU hot loops to vectorize:** GPU texture load/format-conversion/detile/scale/mip and
  EDRAM resolve/copy run ENTIRELY on the Adreno via compute/transfer shaders (no host per-texel/
  per-pixel loop — the classic texture_conversion.cc CPU path does not exist in this fork). XMA
  decode IMDCT/FFT is already NEON via vendored FFmpeg aarch64 asm.

## Confirmed AT CEILING / BLOCKED (do not re-suggest)
- VMX128->NEON integer/format backbone (a64_seq_vector.cc): PACK/UNPACK, PERMUTE/SWIZZLE (TBL +
  XOR-0x03), VECTOR_ADD/SUB/MAX/MIN/AVERAGE/SHL/SHR/SHA, all COMPARE — bit-exact single NEON ops.
- Byteswap loads/stores + bulk save/restore (a64_seq_memory.cc, a64_emitter.cc): rev/rev16/rev32; dc zva memset/dcbz.
- LSE atomics throughout (kA64EmitLSE, present on SM8550). LSE2/uscat correctly NOT chased (absent).
- BLOCKED fp16 PACK/UNPACK: hand-rolled NEON integer math mandatory (Xenos exp-31 is a NORMAL value;
  FCVTL/FCVTN would emit inf/NaN). Already optimal given the constraint (4 lanes at once).
- BLOCKED POW2/LOG2: scalar libm thunk is the only bit-exact option (no bit-exact NEON exp2/log2).
- BLOCKED & CORRECTLY GATED fp32 DOT_PRODUCT_3/4: default f64-widen matches Xenos; the single-
  precision arm64_vmx_dot_f32_fastpath is compile-default-false (the proven BD black-idler). No fp
  dot exists in the ARMv8.2 baseline; FEAT_DotProd is integer-only. DO NOT re-attempt.
- BLOCKED RSQRT/RECIP: PPC frsqrte/vrsqrtefp lookup-table helpers mandatory; RECIP uses exact
  fdiv(1.0,x) not frecpe. NEON estimates would diverge.
- XMA2 decode IMDCT/FFT: already NEON inside vendored FFmpeg (HAVE_NEON=1, fft_neon.S/mdct_neon.S).
  FEAT_FCMA would not beat the radix FFT asm; do NOT hand-roll an FCMA FFT.
- GPU texture + EDRAM resolve: on the Adreno via compute/transfer shaders; host only records
  commands. Also ~3%/~0% of the BD GPU frame — not even the GPU bottleneck.
- GPU shader translation: deliberately fp32 + strict-IEEE (DenormFlush/SignedZeroInfNanPreserve/
  RTE + NoContraction/Invariant) to match Xenos. Lowering to Adreno 2x-fp16 or dropping NoContraction
  is the exact forbidden divergence. Required, at ceiling.
- Internal draw-extent estimation: cost dominated by the ShaderInterpreter, not the reductions;
  NEON-vectorizing the wrapper yields ~0; scene is GPU-bound anyway.

## Genuine opportunities (all CPU/audio-side -> SECONDARY to the GPU-bound BD scene)
1. **DEVICE READ (not code) — confirm kA64FZFlushesInputs is detected on c3ca0370.** The runtime
   probe (platform_arm64.cc:80-113) gates the software denormal-flush. If it IS set (expected on
   Cortex-X3), the whole FlushDenormals_V128 subsystem is already at ceiling and nothing ships. If
   it is NOT set, every VMX fp op (transform + audio) carries ~18 extra NEON instructions and
   confirming/enabling the hardware FZ-input path recovers that with ZERO divergence. MEDIUM-if-unset,
   ZERO-if-set. CAVEAT: first CLEAR the stale device files/xenia.config.toml (carries
   arm64_vmx_dot_f32_fastpath=true, the BD black-idler) so it doesn't confound the read.
2. **FlagM add-carry flip** (arm64_flagm_fastpath, a64_sequences.cc:2120-2207): already built +
   bit-exact; a config/rollout flip AFTER the device A/B. LOW (saves ~1 op/add-carry).
3. APU 2ch downmix NEON port (conversion.h:96-112, the Thor audio path): genuinely un-vectorized,
   but <1% of a core; guest-visible -> must pin scalar op-order + NO FMA + literal 1/2.5 constants
   (NOT vrecpe) to stay bit-exact. NOT safe to build blind. LOW.
4. APU 6ch downmix NEON port (conversion.h:86-95): pure byteswap+reorder -> bit-identical, SAFE to
   build blind, but DESKTOP-ONLY (the Android driver emits 2ch, never calls it) -> near-zero Thor
   benefit. Not worth it for the Thor goal.
5. XMA ConvertFrame float->int16 (xma_context.cc:994-1009): scalar today; guest-visible, must match
   the scalar clamp+truncate (NOT the SSE unclamped round-to-nearest path) -> NOT safe blind. LOW.
6. Dead-code cleanup: delete unused EmulateDotProduct3/4 (a64_sequences.cc:5630-5664). ZERO perf.

## Honest caveats
- The headline Blue Dragon heavy scene is GPU-bound (CPU ~84% idle). EVERY opportunity here is
  CPU/audio-side and SECONDARY for that scene; none move the BD framerate. They help CPU-bound
  titles (Lost Odyssey), load/stream, audio-heavy moments.
- This is a MOSTLY-AT-CEILING result. The fast-math hardware is already exploited wherever a
  bit-exact instruction exists; the unused hardware is unusable because it would diverge.
- The only non-trivial action is the rank-1 device read (confirm FZ-input flush is active); fold it
  into the device-validation queue. The two safe-blind items (ranks 4, 6) are near-zero value.

Source: wf_7c17c42e-2f6 (full output in the task temp file). See [[approx-math-guest-visible-vs-heuristics]];
prior research docs/research/20260601-arm64-adreno-speed-techniques.md.
