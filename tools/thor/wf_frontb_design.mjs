export const meta = {
  name: 'deep-thor-frontb-design',
  description: 'Design + adversarially verify Front B (CPU advanced-math + UMA to cut the GPU per-triangle binning floor) for the Adreno-740 re-arch. Brutally honest feasibility verdict before any blind build; the heavy scene is GPU-bound and xenia runs the guest VS as SPIR-V, so most CPU offload does NOT reduce GPU work. Device disconnected - design/read only.',
  phases: [
    { title: 'Understand', detail: 'parallel readers map the vfetch/VS path, culling, primitive/geometry, guest transform math, and the UMA feed' },
    { title: 'Feasibility', detail: 'reason whether ANY CPU+UMA offload reduces GPU per-triangle binning (vs just moving work)' },
    { title: 'Verify', detail: 'adversarial verifiers attack each option: CPU cull, CPU pre-transform, CPU geometry compaction' },
    { title: 'Synthesize', detail: 'honest verdict + first safe gated increment OR recommend holding for the device A/B' }
  ]
};

const FACTS = [
  'PROJECT: xenia-thor (Xbox360 emulator fork) -> AYN Thor / Adreno-740 (TBDR) / Vulkan; PowerPC->ARM64 a64 JIT. Device DISCONNECTED: design + code-read only, NO device, NO perf numbers. Cite file:line from the ACTUAL repo (Read/Grep); do not invent.',
  'GOAL of FRONT B: reduce the GPU PER-TRIANGLE binning floor - the NON-coalescable cost (every vertex position-shaded in the binning pass + every primitive written to each tile it covers). This is the part Lever 2 draw concatenation and Front A EDS do NOT touch. Proven: the Blue Dragon heavy scene is GPU-BOUND (Adreno 77-99% busy), gpu_frame_us super-linear in vertex count + fill-INDEPENDENT; CPU ~84% IDLE. So there is spare CPU + the SD8Gen2 has NEON/i8mm/bf16/dotprod/fcma and the device is UNIFIED memory (host-visible|device-local).',
  'HARD REALITY to confront (be brutally honest): (a) Offloading work to the idle CPU only helps if it REDUCES GPU work - moving the same work to CPU while the GPU still does it is useless. (b) xenia runs the GUEST vertex shader translated to SPIR-V on the GPU, with MANUAL in-shader vfetch (gl_VertexIndex + system constants index into shared_memory_->buffer()); the GPU re-runs the VS (incl. position transform) for every vertex regardless of any CPU pre-transform, UNLESS the entire vfetch/VS path is bypassed for a draw (a deep, per-shader re-arch). (c) The binning pass cost scales with TRIANGLES BINNED; the only way to cut it is to BIN FEWER TRIANGLES (cull before the GPU) or submit fewer/smaller primitives.',
  'THREE OPTIONS to evaluate honestly: (1) CPU-SIDE CULL - on the spare CPU, frustum-cull and/or backface-cull guest triangles before the GPU bins them, producing a reduced index buffer (in UMA, zero-copy) the GPU draws instead; cuts triangles binned = real GPU-work reduction. RISK: must reproduce the guest VS position transform on CPU to know clip-space positions (needs the guest transform matrix/constants), and must EXACTLY match the GPU cull (backface winding, guard-band, near-plane) or geometry visibly drops/appears. (2) CPU PRE-TRANSFORM - compute clip-space positions on CPU and have the GPU skip the VS position math; but the GPU VS still runs for varyings, so this only helps if positions are a big share AND the VS can consume precomputed positions (deep SPIR-V change). (3) CPU GEOMETRY COMPACTION - dedup/merge vertices or reorder indices into contiguous runs to feed Lever 2 better; helps the per-DRAW cost (already addressed) more than the per-triangle floor.',
  'CONTEXT: Front A EDS (cull/front/depth/stencil/topology) DONE; Lever 2 draw concatenation (vulkan_merge_draws) DONE - both gated default-off, build-verified, hardware-unverified, awaiting the device A/B. Guest geometry is in shared_memory_->buffer(); vfetch is manual in-shader (spirv_shader_translator.cc); the a64 backend (src/xenia/cpu/backend/a64/) runs guest VMX128 vector math; cull mode comes from PA_SU_SC_MODE_CNTL (regs).',
  'CONSTRAINT: any Front B build must be gated behind a cvar, default-off, build-verifiable, and STRICTLY equivalent rendering (a cull that drops a visible triangle is a correctness bug, invisible to validation, only catchable on device - which is OFF). If an option cannot be made provably-equivalent + build-verified blind, the honest recommendation is to HOLD it for the device A/B, NOT build it blind. Prefer recommending a SAFE first step (e.g. read-only instrumentation that measures how many triangles a CPU cull COULD drop) over risky geometry-altering code.'
].join('\n');

const READ_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    subsystem: { type: 'string' },
    hotspots: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      file: { type: 'string' }, line: { type: 'string' }, what: { type: 'string' }
    }, required: ['file', 'what'] } },
    keyFindings: { type: 'array', items: { type: 'string' } },
    feasibilityNotes: { type: 'array', items: { type: 'string' } }
  },
  required: ['subsystem', 'keyFindings']
};

const readers = [
  { label: 'read:vfetch-vs', prompt: 'Read src/xenia/gpu/spirv_shader_translator*.cc/.h for how the guest vertex shader is translated to SPIR-V and how vertex fetch works (gl_VertexIndex + system constants indexing shared_memory_->buffer(); kSystemConstantVertexIndexLoadAddress; per-vfetch reads). Determine: does the GPU re-run the full guest VS (incl. position transform) per vertex regardless of CPU work? Could a draw bypass the VS position math if positions were precomputed on CPU, and how deep would that be? file:line.' },
  { label: 'read:culling', prompt: 'Read where back-face / front-face / frustum culling is configured (PA_SU_SC_MODE_CNTL cull_front/cull_back/face in vulkan_pipeline_cache.cc + vulkan_command_processor.cc; viewport/clip in draw_util). Determine EXACTLY what the GPU culls (winding, near/far, guard-band) so a CPU cull could match it. Is there any guest-side or CPU-side cull today? file:line.' },
  { label: 'read:primitive-geometry', prompt: 'Read src/xenia/gpu/primitive_processor*.{h,cc} + draw_util*. How are index buffers processed (kGuestDMA contiguous), primitives assembled, vertex counts derived? Could a CPU pass produce a REDUCED/compacted index buffer (dropping culled triangles) into a UMA buffer the GPU draws instead? Where would that hook in (it already produces ProcessingResult)? file:line.' },
  { label: 'read:guest-transform-math', prompt: 'Read src/xenia/cpu/backend/a64/a64_seq_vector.cc + a64_sequences.cc (VMX128 vector math: matrix-vector multiply via dot/MUL_ADD, transforms). Where does the guest run vertex transform math, on which thread, and is it even reachable/interceptable from the GPU draw path? Is the transform matrix knowable from registers/constants at draw time (to replicate position transform on CPU for culling)? Honest assessment of whether CPU can know clip-space positions without re-running arbitrary guest VS logic. file:line.' },
  { label: 'read:uma-feed', prompt: 'Read src/xenia/gpu/vulkan/vulkan_shared_memory.cc + how index/vertex data reaches the GPU. Could a CPU-produced index buffer (e.g. culled/compacted) in a host-visible UMA buffer be bound for a draw instead of the guest index range? What is the cheapest zero-copy path, and does the kGuestDMA path or a scratch buffer support it? file:line.' }
];

function readerThunk(r) {
  return function () {
    return agent(r.prompt + '\n' + FACTS, { label: r.label, phase: 'Understand', schema: READ_SCHEMA });
  };
}

phase('Understand');
const rawFindings = await parallel(readers.map(readerThunk));
const findings = rawFindings.filter(Boolean);
log('Understand: ' + findings.length + '/' + readers.length + ' readers');
function digestOf(f) { return { sub: f.subsystem, find: f.keyFindings, feas: f.feasibilityNotes || [] }; }
const digest = JSON.stringify(findings.map(digestOf)).slice(0, 18000);

phase('Feasibility');
const FEAS_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    canCpuReduceGpuBinning: { type: 'string', 'enum': ['yes-with-cull', 'yes-other', 'no', 'only-marginally'] },
    reasoning: { type: 'string' },
    whyPretransformFailsOrWorks: { type: 'string' },
    cullViability: { type: 'string' },
    compactionViability: { type: 'string' },
    blindBuildable: { type: 'boolean' }
  },
  required: ['canCpuReduceGpuBinning', 'reasoning', 'blindBuildable']
};
const feas = await agent('Reason BRUTALLY HONESTLY whether ANY CPU + NEON/i8mm + UMA offload can reduce the GPU per-TRIANGLE binning floor (not just move work). Address each option (CPU cull / CPU pre-transform / CPU compaction) using the findings. Conclude whether it is buildable+verifiable BLIND (device off) or must wait for the device.\nFINDINGS: ' + digest + '\n' + FACTS, { phase: 'Feasibility', schema: FEAS_SCHEMA });

phase('Verify');
const VERDICT_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    option: { type: 'string' },
    reducesGpuBinning: { type: 'boolean' },
    correctnessHazards: { type: 'array', items: { type: 'string' } },
    blindBuildRisk: { type: 'string' },
    verdict: { type: 'string', 'enum': ['build-now-safe-increment', 'design-only-hold', 'reject'] },
    safeFirstIncrement: { type: 'string' }
  },
  required: ['option', 'reducesGpuBinning', 'verdict']
};
const options = [
  { label: 'verify:cpu-cull', prompt: 'Adversarially evaluate CPU-SIDE CULL (frustum + backface on the idle CPU, producing a reduced UMA index buffer the GPU draws). Does it REALLY cut triangles binned? Can the CPU know clip-space positions without re-running arbitrary guest VS logic (is the transform a simple knowable matrix, or arbitrary shader)? Can it EXACTLY match the GPU cull (winding from PA_SU_SC_MODE_CNTL, near-plane, guard-band) so nothing visibly drops? Is it safe to build blind (device off) - or only a read-only "how many triangles COULD be culled" instrument is safe now? Default skeptical.' },
  { label: 'verify:cpu-pretransform', prompt: 'Adversarially evaluate CPU PRE-TRANSFORM (compute clip-space positions on CPU so the GPU VS skips position math). Given xenia runs the guest VS as SPIR-V with manual vfetch, can the GPU even consume precomputed positions without a deep per-shader SPIR-V re-arch? Does the VS still run for varyings (so positions are a small share)? Is this anything but a research project? Default skeptical; likely reject for blind build.' },
  { label: 'verify:cpu-compaction', prompt: 'Adversarially evaluate CPU GEOMETRY COMPACTION (dedup/reorder indices into contiguous same-state runs to feed Lever 2 better). Does it reduce the per-TRIANGLE floor (no - same triangles) or only help the per-DRAW cost (already handled by Lever 2)? Is the marginal benefit worth the cost/risk? Default skeptical.' }
];
function verifyThunk(o) {
  return function () {
    return agent(o.prompt + '\n\nFEASIBILITY: ' + JSON.stringify(feas) + '\nFINDINGS: ' + digest + '\n' + FACTS, { label: o.label, phase: 'Verify', schema: VERDICT_SCHEMA });
  };
}
const rawVerdicts = await parallel(options.map(verifyThunk));
const verdicts = rawVerdicts.filter(Boolean);

phase('Synthesize');
const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    overallVerdict: { type: 'string', 'enum': ['build-safe-increment-now', 'hold-for-device', 'reject-frontb'] },
    rationale: { type: 'string' },
    bestOption: { type: 'string' },
    firstSafeIncrement: { type: 'string' },
    whatNeedsDevice: { type: 'array', items: { type: 'string' } },
    honestCaveats: { type: 'array', items: { type: 'string' } }
  },
  required: ['overallVerdict', 'rationale', 'honestCaveats']
};
const synth = await agent('Synthesize the Front B verdict from the feasibility analysis + adversarial verdicts. Be HONEST: if no option is safely buildable+verifiable blind, recommend HOLD-FOR-DEVICE and say exactly why; if a read-only instrumentation increment (e.g. measuring cullable-triangle potential) is the only safe blind step, recommend that as the first increment; only recommend building geometry-altering code blind if it can be made provably rendering-equivalent. Give: overall verdict, best option, the first safe increment (if any), what needs the device, honest caveats.\nFEASIBILITY: ' + JSON.stringify(feas) + '\nVERDICTS: ' + JSON.stringify(verdicts) + '\n' + FACTS, { phase: 'Synthesize', schema: SYNTH_SCHEMA });

return { feasibility: feas, verdicts: verdicts, synthesis: synth };
