# ARM64 + Adreno-740 speed techniques for xenia-thor — researched & prioritized (2026-06-01)

From the web-research + verification workflow (tools/thor/wf_arm64_adreno_research.mjs,
run wf_998feb8c-8a0, 9 agents: 6 web-researchers → 2 verifiers → synthesis). Sources are
real (ARM ACLE/ARM ARM, corsix AArch64 vector tour, Box64/RPCS3/FEX-Emu, QEMU emulation
page, the arm-socs HWCAP table); claims the verifiers could not confirm are flagged.

## TL;DR — where the speed actually is
The device-proven Blue Dragon bottleneck is the **GPU binning / geometry / per-draw front
end** (~2000 tiny draws/frame, gpu_busy 77-79% @615MHz, **CPU ~84% idle**, fill 0%). So:
- **GPU/Vulkan techniques (ranks 1-4) are the only real framerate lever for the headline
  scene** — and the big ones are already BUILT; they need the **device to validate** (build
  blind, prove on a content-matched A/B).
- **Every CPU SIMD/JIT technique (ranks 5-10) is SECONDARY for that scene** (nothing for the
  CPU to do while the GPU is the wall). They matter for CPU-bound titles, guest VMX-heavy
  code, the CPU ShaderInterpreter/cull replay, texture/vertex format-conversion + endian
  byte-swap, XMA audio, and large memcpy/upload — **and most are already implemented** in the
  fork. The research's main CPU-side value is *confirming the existing approach is correct*
  and supplying the verified instruction mapping + the things NOT to chase.

## Ranked recommendations

### GPU / Vulkan — the headline lever (validate on device)
1. **EDS/EDS2 topology promotion** (triangle-list + strip share one VkPipeline), extending the
   shipped cull/front-face/depth/stencil EDS. Collapses pipeline-key variants → fewer Adreno
   context rolls on the binning front end, and is the **prerequisite** for concatenation to
   find runs >1. EDS is Vulkan 1.3 core; `CmdVkSetPrimitiveTopology` infra exists.
   **Device-gated** (the `UpdateDynamicState` emit site needs `host_primitive_type`; rect/quad
   class needs real-frame verification). Effort: medium.
2. **Zero-copy draw concatenation** of consecutive same-state, byte-contiguous LIST-topology
   kGuestDMA ranges into one `vkCmdDrawIndexed` (`vulkan_merge_draws`). Pays the Adreno
   per-DRAW fixed cost once per run — the *coalescable* half of the mixed binning cost.
   **BUILD-COMPLETE blind**; gated default-off. Honest ceiling: recovers only the per-draw
   share, **cannot** reduce the per-triangle binning floor. NOT instancing (dead on TBDR).
   Gated on rank 1 lengthening same-pipeline runs first.
3. **Read-only cullable-triangle COUNTER** (`gpu_trace_cullable_tris`) via ShaderInterpreter
   VS-position replay — the decision instrument for an on-device CPU cull that would lower the
   per-TRIANGLE floor (the non-coalescable half). **Counter is buildable blind and now SHIPPED**
   (C1 afa40d932; C2/C3 conservative-frustum b700e52c2). The geometry-altering **cull itself is
   HOLD-for-device** (unmodeled guard-band/near-plane; a winding/rect mismatch silently drops a
   visible triangle, invisible to build validation).
4. **Last-bound-index-buffer cache** — drop the unconditional `CmdVkBindIndexBuffer` rebind per
   draw. **Buildable blind, small.** Honest: CPU is ~84% idle on the headline scene, so this is
   CPU hygiene that pairs with concatenation and helps CPU-bound submission in other titles —
   **not** a Blue-Dragon framerate lever.

### CPU SIMD / JIT — secondary (mostly already implemented; correctness-critical)
5. **VMX128→NEON direct lowerings**: element-wise 1:1 ops; `vperm`→**TBL with XOR-0x03 endian
   index remap**; `vsel`→**BSL**; saturating pack/unpack→SQXTN/UQXTN/SQXTUN + SXTL/UXTL;
   splat→DUP. FEAT_AdvSIMD baseline. **Already in `a64_seq_vector.cc`** (PERMUTE_V128 movi
   0x03 + tbl at ~:996, SPLAT/PACK/UNPACK with the Xenos 0x7FFF float16 saturate quirk). Value
   = the verified mapping table, not re-implementation.
6. **Per-mode FPCR + precision-matched dot**: VMX flush-to-zero (FPCR bit 24) vs IEEE FPU,
   PPC→ARM round-mode mapping, software denormal/NaN-payload fallback; `vmsum4fp`/`vmsum3fp`
   widen f32→f64 then force inf→QNaN 0x7FC00000 to match Xenos. **Correctness-critical, not a
   speed lever** — the `arm64_vmx_dot_f32_fastpath` cvar is a known Blue Dragon black-3D idler
   when ON (the f32 fastpath diverges observably). Keep the fork's per-mode design.
7. **FEAT_FP16 native half** (FCVTN/FCVTL) for FLOAT16_2/4 vertex/texture format conversion +
   the vertex-fetch/endian path. Present on SM8550 (fphp/asimdhp). **Buildable blind**, helps
   upload-heavy frames, not the bottleneck. Caveat: Xenos half saturates to 0x7FFF (still needs
   a fixup after FCVTN).
8. **FEAT_FlagM/FlagM2** (RMIF/SETF8/16/CFINV) for guest PPC CR/XER flag materialization — one
   instruction instead of multi-op bit shuffles. Present on SM8550 (flagm, flagm2). Real lever
   for CPU-bound titles (Lost Odyssey, Burnout), negligible for the idle-CPU headline scene.
9. **FEAT_LSE atomics** (CAS/SWP/LDADD) for guest `lwarx/stwcx` LL/SC and JIT sync. Present on
   SM8550 (atomics). **CORRECTION: FEAT_LSE2 (uscat) and LRCPC2 (ilrcpc) are ABSENT** — do NOT
   assume the unaligned 16-byte single-copy-atomic guarantee; runtime-gate any LSE2 reliance via
   HWCAP_USCAT.
10. **FEAT_DotProd (SDOT/UDOT)** for integer byte-MAC in format/texture decode, **FEAT_FCMA
    (FCMLA/FCADD)** for XMA-audio complex-MAC. Both present on SM8550. **Low applicability**:
    guest VMX128 math is fp32 (no native guest op maps to SDOT/UDOT/i8mm/bf16); benefit needs
    the JIT/decoder to pattern-match reductions.

## Verification corrections — do NOT chase these
- **SVE2 / SVE-bitperm: ABSENT on SM8550** (Qualcomm-disabled). The Cortex-X3 IP supports SVE2
  but the shipped SoC does not expose it. SVE present only at 128-bit VL = **no width advantage
  over NEON**. The JIT must runtime-gate, never assume.
- **FEAT_LSE2 (uscat) / LRCPC2 (ilrcpc): ABSENT.**
- **i8mm / bf16 GEMM and FEAT_JSCVT: near-zero applicability** (no fp32-guest-op mapping; JSCVT
  rounding mismatches PPC). CLAUDE.md advertises NEON/i8mm/bf16/dotprod/fcma, but i8mm/bf16 have
  no guest mapping here.
- **Instancing is DEAD on this TBDR** — re-bins identical triangles N times, leaving the per-
  triangle term untouched.
- **Detect all optional features at runtime** via `getauxval(AT_HWCAP)` rather than trusting the
  spec/TRM (the Cortex-X3 TRM and gpuinfo.org Adreno-740 report were not directly retrievable;
  feature presence is corroborated by the arm-socs HWCAP table + QEMU + lelegard features.md).
- **Adreno-740 Vulkan extension exposure could not be directly confirmed** (gpuinfo.org 403).
  Rely on EDS/EDS2 as **1.3 core** (safe); EDS3 blend promotion has **uncertain** Adreno-740
  support → feature-detect at runtime. FSI is known absent (depth/stencil EDS already gated `!FSI`).
- **UMA present path stays OFF** — `gpu_uma_direct_shared_memory` present-hangs Blue Dragon even
  with the smart-sync guards (device-diagnostic-only, see 20260601-uma-present-hang-rootcause.md).
  UMA is usable only as a CPU→GPU geometry feed (Front B), never for present, until a device A/B.

## Apply queue (38h-blind window)
**Buildable blind now:** rank 4 (last-bound-IB cache, small); rank 7 (FP16 format conversion,
gated); rank 8 (FlagM flag materialization) — all secondary to the headline scene, gated/safe,
build-verified. Rank 3 counter = DONE.
**Device-gated (validate on reconnect, do not build blind):** rank 1 EDS topology emit + A/B;
rank 2 concatenation combined A/B; rank 3 the cull itself; read `cullable_tris`; the CPU SIMD
ranks' *speedups* (they're already correct; perf only matters on CPU-bound titles to measure).

Honest bottom line: the research **confirms** the GPU-binning re-arch is the right lever and the
CPU VMX→NEON backbone is already sound. It does not reveal a large *new* blind speed win — the
remaining headline gains are GPU-side and gated on the device A/B.

Source: wf_998feb8c-8a0 (full structured output in the task temp file). All file:line/repo
claims cross-checked by the workflow's verifiers against the actual repo + cited web sources.
