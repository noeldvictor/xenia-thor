export const meta = {
  name: 'xenia-fastmath-hardware-audit',
  description: 'Comprehensive re-audit of the xenia-thor codebase for places that could better exploit Snapdragon/Adreno fast math (NEON, dotprod/i8mm/bf16/fcma, FlagM/LSE, Adreno fp16) - classifying each as GUEST-VISIBLE (must stay bit-exact, cannot approximate) vs OUR-INTERNAL/HEURISTIC (safe to vectorize/approximate), checking what is already optimal, then adversarially verifying and prioritizing. Goal: surface genuinely-missed NEON wins in OUR code OR definitively confirm the hardware ceiling with per-subsystem evidence.',
  phases: [
    { title: 'Audit', detail: '6 parallel auditors sweep distinct subsystems for math/compute hot paths, grounded in file:line' },
    { title: 'Verify', detail: 'adversarially check each claimed opportunity: real (our code / bit-equivalent) vs blocked (guest-visible) vs already-done' },
    { title: 'Synthesize', detail: 'prioritized genuine-opportunity list + the definitive ceiling verdict' }
  ]
};

const CONTEXT = [
  'PROJECT: xenia-thor (Xbox360 emulator) on AYN Thor. Host CPU = ARM64 Cortex-X3 / Snapdragon 8 Gen 2 (NEON baseline; FEAT_DotProd/I8MM/BF16/FCMA/FP16/FlagM/LSE present; SVE2/LSE2/uscat ABSENT). GPU = Adreno 740 (Vulkan; fp16 ALU ~2x but guest Xenos shaders are fp32). Guest CPU = PowerPC Xenon (big-endian, VMX128 SIMD).',
  'THE CRITICAL CLASSIFICATION (apply to EVERY finding): fast/approx math is SAFE for OUR-INTERNAL or HEURISTIC computations (results not fed verbatim to the guest/rendering - e.g. the cullable-triangle counter, draw-extent estimation, internal bookkeeping), but it DIVERGES AND BREAKS GAMES on GUEST-VISIBLE math (values that feed rendering, game logic, or audio output and must reproduce Xenos precision). PROVEN: arm64_vmx_dot_f32_fastpath (a faster single-precision VMX dot) BLACK-SCREENED Blue Dragon; FP16 vertex/texture conversion CANNOT use hardware FCVTL/FCVTN because Xenos half exponent 31 is a NORMAL value (no inf/NaN) - the fork hand-rolls NEON integer math instead. So for guest-visible math, a hardware fast path is usable ONLY if it is BIT-EQUIVALENT (or proven+gated+device-validated like the FlagM carry work). For our-internal/heuristic math, approximate freely.',
  'ALREADY DONE / AT CEILING (do not just re-suggest; note if a finding extends these): VMX128->NEON backbone in src/xenia/cpu/backend/a64/a64_seq_vector.cc (PACK/UNPACK incl. FP16 with Xenos quirks, dot-product f64-widen, splat/DUP, vperm/TBL with XOR-0x03 endian remap, vsel/BSL, saturating pack/unpack); per-mode FPCR; FEAT_FlagM rmif+adc for OPCODE_ADD_CARRY (gated arm64_flagm_fastpath). GPU binning levers (EDS dynamic state + draw concatenation) + read-only scaffolding (elig_runlen, merge_miss, cullable_tris) built. UMA present path is OFF (present-hang). Do NOT chase SVE2/LSE2/i8mm-GEMM/bf16-GEMM/instancing/fp16-shader-approximation blind.',
  'BOTTLENECK CONTEXT for ranking: the headline Blue Dragon heavy scene is GPU-bound (CPU ~84% idle there), so CPU-side wins are SECONDARY for that scene but matter for: CPU-bound titles/scenes (Lost Odyssey), load/stream times, XMA audio, texture/format conversion + detile, EDRAM resolve/copy, large memcpy/byteswap. Rank each finding by HONEST real impact + which workload it helps.',
  'OUTPUT RULES: ground every finding in file:line via Grep/Read of the ACTUAL repo (src/xenia/...). For each: is the math GUEST-VISIBLE or OUR-INTERNAL? Is a NEON/fast-path/approx win applicable? Is it ALREADY vectorized/optimal? Is it SAFE (our-code/bit-equivalent) or BLOCKED (guest-visible-divergence)? Be honest - "already optimal" and "blocked - would diverge" are valuable findings. Do not invent hot paths.'
].join('\n');

const AUDIT_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    subsystem: { type: 'string' },
    findings: {
      type: 'array',
      items: {
        type: 'object', additionalProperties: false,
        properties: {
          location: { type: 'string' },
          what: { type: 'string' },
          guestVisibleOrInternal: { type: 'string', 'enum': ['guest-visible', 'internal-heuristic', 'mixed'] },
          status: { type: 'string', 'enum': ['opportunity-safe', 'opportunity-needs-bitexact', 'already-optimal', 'blocked-would-diverge'] },
          fastPath: { type: 'string' },
          estimatedImpact: { type: 'string', 'enum': ['high', 'medium', 'low'] },
          helpsWorkload: { type: 'string' },
          recommendation: { type: 'string' }
        },
        required: ['location', 'what', 'guestVisibleOrInternal', 'status', 'estimatedImpact']
      }
    },
    subsystemVerdict: { type: 'string' }
  },
  required: ['subsystem', 'findings', 'subsystemVerdict']
};

const auditors = [
  { label: 'audit:a64-jit', prompt: 'Audit the a64 JIT backend (src/xenia/cpu/backend/a64/: a64_seq_vector.cc, a64_sequences.cc, a64_seq_memory.cc, a64_seq_control.cc, a64_emitter.cc). Beyond the known VMX->NEON / FlagM work, find any guest IR op lowered with a scalar fallback, a multi-op sequence, or a software helper where a NEON / FEAT_DotProd / FlagM / LSE instruction would be BIT-EQUIVALENT and faster. All guest-visible (must be exact). Note ops already optimal. file:line.' },
  { label: 'audit:xma-audio', prompt: 'Audit XMA audio decode + the APU mixing/resampling (src/xenia/apu/: xma_*, the decoder, conversion/resample/mix loops). Find scalar per-sample loops that could be NEON-vectorized, FFT/complex-MAC that FEAT_FCMA could accelerate. Classify guest-visible (audio output - small perceptual tolerance but prefer bit-equivalent) vs internal. Is it already vectorized? file:line.' },
  { label: 'audit:texture-conversion', prompt: 'Audit texture load/conversion/detile/scale/mip (src/xenia/gpu/texture_*.cc, texture_conversion.*, texture_util, the vulkan/d3d12 texture caches). Find per-texel format-conversion + detile (swizzle/address - not math) + scaling loops that are scalar and could be NEON-vectorized. Format conversion is guest-visible (must be exact - watch Xenos quirks); detile address math is internal. Already vectorized? file:line.' },
  { label: 'audit:edram-resolve', prompt: 'Audit EDRAM / render-target resolve + copy + resolution-scale paths (src/xenia/gpu/render_target_cache.*, draw_resolution, resolve, the dump/transfer logic; vulkan_render_target_cache). Find CPU-side per-pixel/per-tile math or copies that are scalar (NEON candidate) vs done on GPU (already hardware). Classify guest-visible vs internal. file:line.' },
  { label: 'audit:shader-translator', prompt: 'Audit the SPIR-V shader translator (src/xenia/gpu/spirv_shader_translator*.cc) for whether guest Xenos ALU could safely use Adreno fp16 / RelaxedPrecision (it almost always CANNOT - guest shaders are fp32 and approximation diverges), and any CPU-side translator math/helpers that are hot + vectorizable. Be skeptical: flag fp16-shader ideas as blocked-would-diverge unless provably bit-equivalent. file:line.' },
  { label: 'audit:memory-copy-byteswap', prompt: 'Audit memory copy / byte-swap / upload paths (src/xenia/base/ memory + byte_order; src/xenia/memory.cc; the shared-memory upload in gpu/vulkan/vulkan_shared_memory.cc; physical heap copies). Find big scalar copies/byte-swaps that could use NEON REV/vld-vst or non-temporal stores. Byte-swap of guest data is exact-by-construction (REV is bit-exact); large copies are internal. Mostly OUR code = safe to optimize. Already vectorized? file:line.' }
];

phase('Audit');
const audits = (await parallel(auditors.map(function (a) {
  return function () { return agent(a.prompt + '\n\n' + CONTEXT, { label: a.label, phase: 'Audit', schema: AUDIT_SCHEMA, agentType: 'general-purpose' }); };
}))).filter(Boolean);
log('Audit: ' + audits.length + '/' + auditors.length + ' subsystems');
const allFindings = audits.flatMap(function (a) {
  return (a.findings || []).map(function (f) { return { sub: a.subsystem, loc: f.location, what: f.what, gv: f.guestVisibleOrInternal, status: f.status, impact: f.estimatedImpact }; });
});
const auditDigest = JSON.stringify(allFindings).slice(0, 22000);

phase('Verify');
const VERIFY_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    lens: { type: 'string' },
    confirmedOpportunities: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      location: { type: 'string' }, why: { type: 'string' }, safeBlind: { type: 'boolean' }
    }, required: ['location', 'why', 'safeBlind'] } },
    rejected: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      location: { type: 'string' }, reason: { type: 'string' }
    }, required: ['location', 'reason'] } },
    notes: { type: 'string' }
  },
  required: ['lens', 'confirmedOpportunities', 'rejected']
};
const verifyLenses = [
  { label: 'verify:guest-visible-safety', prompt: 'Adversarially check each claimed opportunity for the guest-visible-divergence trap: would the proposed fast/approx path change a value that feeds rendering/game-logic/audio and thus diverge from Xenos? REJECT any guest-visible math optimization that is not provably BIT-EQUIVALENT. Confirm only the ones that are our-internal/heuristic (approx-safe) or bit-equivalent. Use Grep/Read to verify the actual code. Default to rejecting if uncertain.' },
  { label: 'verify:real-and-not-done', prompt: 'Adversarially check each claimed opportunity for being REAL and NOT-ALREADY-DONE: is the cited path actually a hot/scalar loop (not already NEON-vectorized, not compiler-auto-vectorized trivially, not cold)? Is the estimated impact honest given the GPU-bound headline scene (most CPU wins are secondary)? Use Grep/Read. Reject already-optimal or cold-path claims.' }
];
const verify = (await parallel(verifyLenses.map(function (l) {
  return function () { return agent(l.prompt + '\n\nCLAIMED FINDINGS: ' + auditDigest + '\n\n' + CONTEXT, { label: l.label, phase: 'Verify', schema: VERIFY_SCHEMA, agentType: 'general-purpose' }); };
}))).filter(Boolean);

phase('Synthesize');
const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    ceilingVerdict: { type: 'string' },
    genuineOpportunities: {
      type: 'array',
      items: {
        type: 'object', additionalProperties: false,
        properties: {
          rank: { type: 'number' },
          location: { type: 'string' },
          change: { type: 'string' },
          guestVisibleOrInternal: { type: 'string' },
          safeToBuildBlind: { type: 'boolean' },
          impact: { type: 'string' },
          helpsWorkload: { type: 'string' }
        },
        required: ['rank', 'location', 'change', 'safeToBuildBlind', 'impact']
      }
    },
    confirmedAtCeiling: { type: 'array', items: { type: 'string' } },
    honestCaveats: { type: 'array', items: { type: 'string' } }
  },
  required: ['ceilingVerdict', 'genuineOpportunities', 'confirmedAtCeiling']
};
const synth = await agent('Synthesize the audit + verification into (1) a clear CEILING VERDICT answering "are we taking full advantage of the CPU/GPU hardware fast math, and can we approx?" - honestly, with the guest-visible-vs-internal split; (2) a ranked list of GENUINE, verifier-confirmed opportunities (only ones that survived both lenses), each marked safeToBuildBlind (our-internal/heuristic or bit-equivalent) vs device-gated, with honest impact + which workload it helps; (3) the subsystems confirmed AT CEILING / blocked. Be honest if the answer is "mostly at the ceiling; the remaining safe wins are X" - that is a valid, valuable verdict. Do not pad with guest-visible approximations that would break games.\n\nAUDITS: ' + JSON.stringify(audits).slice(0, 30000) + '\n\nVERIFY: ' + JSON.stringify(verify).slice(0, 14000) + '\n\n' + CONTEXT, { phase: 'Synthesize', schema: SYNTH_SCHEMA, agentType: 'general-purpose' });

return { audits: audits, verify: verify, synthesis: synth };
