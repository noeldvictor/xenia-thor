export const meta = {
  name: 'deep-thor-gpu-binning-rearch',
  description: 'Deep theoretical re-architecture for AYN Thor (Adreno-740 TBDR + Snapdragon 8 Gen 2): cut Blue Dragon GPU binning/front-end cost and exploit advanced acceleration math (NEON/i8mm/bf16/dotprod/fcma) + UMA. Resolve the binning-cost question by reasoning; produce the first build-verifiable gated lever. Device DISCONNECTED - design/build only.',
  phases: [
    { title: 'Understand', detail: 'parallel readers map the draw/binning/state hot path, geometry-transform math path, and existing instrumentation' },
    { title: 'Binning-verdict', detail: 'reason out per-draw-fixed vs per-triangle cost from TBDR architecture + existing data' },
    { title: 'Design', detail: 'independent deep re-arch approaches incl. advanced acceleration math (judge panel)' },
    { title: 'Verify', detail: 'adversarially verify each reduces REAL TBDR/CPU work, not just host command count' },
    { title: 'Synthesize', detail: 'pick winner, graft best ideas, name the concrete first build-verifiable lever' }
  ]
};

const FACTS = [
  'PROJECT FACTS (xenia-thor: Xbox360 emulator fork; ONE target = AYN Thor). Device is DISCONNECTED: THEORETICAL design + build-verifiable code only, NO device launches, NO perf measurement.',
  '- GPU backend: Vulkan. CPU: PowerPC(Xenon)->ARM64 JIT (a64 backend). Files under src/xenia/gpu/vulkan/, src/xenia/gpu/, src/xenia/cpu/backend/a64/.',
  '- Device: AYN Thor, Snapdragon 8 Gen 2 (kalama). GPU Adreno 740v2 = TILE-BASED DEFERRED RENDERER (TBDR), max 680MHz, NO VK_EXT_fragment_shader_interlock, UNIFIED memory (HOST_VISIBLE|DEVICE_LOCAL host-coherent). CPU: Cortex-X3 + A715/A710 + A510 with NEON, i8mm (int8 matrix), bf16, dotprod (sdot/udot), fcma (complex FMA), fp16.',
  '- USER DIRECTIVE: target the hardware specifically and exploit ADVANCED ACCELERATION MATH on the Thor (NEON/i8mm/bf16/dotprod/fcma), especially for geometry/vertex transform math (guest VMX128/PPC vector ops, matrix transforms, skinning, lighting). Deep re-architecture for Adreno-740 + CPU/GPU UMA is the explicit goal; be bold.',
  '- PROVEN root cause (Blue Dragon heavy 3D field scene): GPU-BOUND on the geometry/binning/state FRONT-END. Adreno gpu_busy 77-99% at 615-680MHz. RULED OUT (content-matched on-device harness): fill/resolution (2x res = 4x pixels, ZERO change), EDRAM tile load/store (~3%), render-pass breaks (~12%), CPU (~84% IDLE). gpu_frame_us (real Vulkan GPU timestamp) is pixel-INDEPENDENT and SUPER-LINEAR in BOTH draw count AND vertex count (corr ~0.88). Scene = ~1200-2148 TINY draws/frame, avg 3-136 verts (triangle lists/strips, point sprites, quads), ~2000 descriptor binds.',
  '- EXISTING instrumentation (cvar vulkan_trace_draw_outcomes_per_frame -> GPU draw outcomes/frame log line): gpu_frame_us; CPU buckets cpu_issuedraw/process/tex/rt/pipe/bind; barrier-break attribution brk_open/buf/img_sr/img_oth; prim/vtx histograms; msaa/surf_pitch; merge_run_hist_8 (run-lengths of CONSECUTIVE same-pipeline draws); merge_vf_same/contig/scattered (each draw guest index source vs prior: same addr/contiguous/scattered). Built to measure draw mergeability.',
  '- OPEN QUESTION (do NOT be blocked by it - REASON it out): per-DRAW FIXED binning overhead (coalescable) vs per-TRIANGLE binning (not coalescable) vs mixed.',
  '- KNOWN trap: exact GPU INSTANCING re-bins identical triangles N times on a TBDR -> total GPU work unchanged (only host count drops) -> NOT a win. Draw CONCATENATION (merge consecutive same-state draws into ONE vkCmdDraw over CONCATENATED vertex/index data) bins each triangle ONCE and pays per-draw fixed cost (state setup/context roll/bin restart) ONCE -> potentially a real win. Distinguish.',
  '- CONSTRAINTS: every change MUST be gated behind a cvar, reversible, and build-verifiable (BUILD SUCCESSFUL + symbol linked in arm64 libxenia-app.so). Adreno-740/SD8Gen2-specific is FINE (single target). UMA direct-shared-memory present-HANGS (device-verified) and stays OFF; UMA role here is a TOOL (zero-copy CPU-side geometry pre-transform feeding fewer/bigger GPU draws), not the lever itself.',
  '- Cite concrete file:line evidence from the ACTUAL repo (Read/Grep). Do not invent. If unknown, say so.'
].join('\n');

const READ_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    subsystem: { type: 'string' },
    hotspots: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      file: { type: 'string' }, line: { type: 'string' }, what: { type: 'string' },
      perDrawOrPerFrame: { type: 'string' }, redundancyAcrossConsecutiveDraws: { type: 'string' }, concatenatable: { type: 'string' }
    }, required: ['file', 'what'] } },
    keyFindings: { type: 'array', items: { type: 'string' } },
    levers: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      name: { type: 'string' }, description: { type: 'string' }, expectedImpact: { type: 'string' },
      risk: { type: 'string' }, reversible: { type: 'boolean' }, firstStep: { type: 'string' }
    }, required: ['name', 'description'] } }
  },
  required: ['subsystem', 'keyFindings', 'levers']
};

const readers = [
  { label: 'read:issuedraw-hotpath', prompt: 'Read src/xenia/gpu/vulkan/vulkan_command_processor.cc, focus on IssueDraw / the per-draw issue path. Map EXACTLY what runs PER DRAW: descriptor-set binds, pipeline binds, dynamic state, vertex/index buffer binds, push constants, barriers. Identify which are REDUNDANT across CONSECUTIVE draws sharing pipeline+descriptors (hoistable/skippable). Read the draw-outcomes instrumentation to see what cpu_bind/cpu_pipe/cpu_issuedraw and merge_run_hist/merge_vf_ actually count. file:line evidence.' },
  { label: 'read:pipeline-cache', prompt: 'Read src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc and .h. How are pipelines keyed and how often do consecutive draws switch vs reuse? Relationship to merge_run_hist. On Adreno a pipeline/state change forces a context roll / bin restart - identify which guest state changes force a new pipeline here and which are avoidable via Vulkan dynamic state. file:line evidence.' },
  { label: 'read:primitive-geometry', prompt: 'Read the primitive/geometry/index path (src/xenia/gpu/primitive_processor*, src/xenia/gpu/draw_util*, vulkan_primitive*). Where do the ~2000 TINY draws originate - are point sprites/quads/strips expanded? Where do index/vertex data come from; is the guest index source CONTIGUOUS across consecutive draws (relate merge_vf_)? Could consecutive same-state draws be CONCATENATED into one draw over merged index/vertex data without re-binning triangles? file:line evidence.' },
  { label: 'read:rt-cache-barriers', prompt: 'Read src/xenia/gpu/vulkan/vulkan_render_target_cache.cc: what triggers render-pass breaks / EDRAM ownership transfers / barriers mid-frame (brk_ buckets)? Each break can restart binning on a TBDR. Which are avoidable/batchable? Do RT state changes fragment otherwise-mergeable consecutive draw runs? file:line evidence.' },
  { label: 'read:instrumentation-readout', prompt: 'Read the draw-outcomes instrumentation in src/xenia/gpu/vulkan/vulkan_command_processor.cc (merge_run_hist_, merge_run_len_, merge_vf_, prim/vtx). WITHOUT a device, reason what it WOULD reveal about mergeability: long same-pipeline runs with contiguous index sources => concatenation high-value; length-1 scattered => not. State what code already computes vs what is still unmeasured. file:line evidence.' },
  { label: 'read:adreno-tbdr-binning', prompt: 'Reason (research-backed; ToolSearch WebSearch if useful) about Adreno 7xx / freedreno-Turnip TBDR binning: the LRZ/visibility (binning) pass vs per-tile render pass; what a context roll is and what triggers it (state/pipeline/RT changes); whether binning cost is per-DRAW fixed (visibility-stream setup, context roll, bin metadata) vs per-TRIANGLE/VERTEX (every primitive position-shaded to bin into tiles). Cross-reference: gpu_frame_us super-linear in BOTH draw AND vertex count, fill-independent. Conclude what makes ~2000 tiny draws expensive on Adreno 740 and what reduces it. State assumptions.' },
  { label: 'read:vmx-geometry-math', prompt: 'Read src/xenia/cpu/backend/a64/a64_seq_vector.cc + a64_sequences.cc (vector ops, DOT_PRODUCT, MUL_ADD) and the a64 backend generally. Identify guest VMX128/PPC vector MATH used for GEOMETRY transforms (matrix-vector multiply, dot products, MUL_ADD/FMA, skinning, lighting) and how it currently lowers to ARM64. Where could SD8Gen2 ADVANCED math help: dotprod, bf16/fp16, i8mm int8-matrix, fcma? Is there batched/matrix transform work that could use NEON FMA or fp16/bf16 throughput? file:line evidence + honest note on whether geometry transform is even on the hot path given CPU is ~84% idle.' },
  { label: 'read:uma-shared-buffer', prompt: 'Read src/xenia/gpu/vulkan/vulkan_shared_memory.cc + base memory.cc (copy_and_swap NEON vqtbl). Given the host-visible|device-local UMA buffer, what is the upload/vertex-fetch model, and could spare CPU+NEON+UMA do GPU-front-end-REDUCING work zero-copy: CPU-side concatenation/merge of consecutive draws vertex/index into one buffer, CPU-side primitive expansion (point sprites/quads) feeding fewer GPU draws, vectorized index byte-swap/dedup? Which is feasible given the buffer model (and given UMA-direct present-hangs, what is safe)? file:line evidence.' }
];

function readerThunk(r) {
  return function () {
    return agent(r.prompt + '\n' + FACTS, { label: r.label, phase: 'Understand', schema: READ_SCHEMA });
  };
}

phase('Understand');
const rawFindings = await parallel(readers.map(readerThunk));
const findings = rawFindings.filter(Boolean);
log('Understand: ' + findings.length + '/' + readers.length + ' readers returned');
function digestOf(f) {
  return { sub: f.subsystem, key: f.keyFindings, levers: (f.levers || []).map(function (l) { return l.name + ': ' + l.description; }) };
}
const digest = JSON.stringify(findings.map(digestOf)).slice(0, 18000);

phase('Binning-verdict');
const BINNING_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    dominantCost: { type: 'string', 'enum': ['per-draw-fixed', 'per-triangle', 'mixed'] },
    reasoning: { type: 'string' },
    evidenceCited: { type: 'array', items: { type: 'string' } },
    confidence: { type: 'string' },
    mergeStrategyThatWins: { type: 'string' },
    mergeStrategyThatFails: { type: 'string' },
    implicationsForLevers: { type: 'array', items: { type: 'string' } }
  },
  required: ['dominantCost', 'reasoning', 'confidence', 'mergeStrategyThatWins', 'implicationsForLevers']
};
const verdict = await agent('We will NOT wait for a profiler. Using the Phase-1 findings and project facts, REASON OUT whether Blue Dragon GPU binning cost is dominated by per-DRAW fixed overhead (coalescable) vs per-TRIANGLE binning (not coalescable) vs mixed, and state which concrete merge/coalesce strategy WINS and which FAILS on the Adreno 740 TBDR. Anchor claims in evidence (super-linear-in-both + fill-independent is key). Decisive but mark confidence.\nFINDINGS: ' + digest + '\n' + FACTS, { phase: 'Binning-verdict', schema: BINNING_SCHEMA });

phase('Design');
const DESIGN_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    approachName: { type: 'string' },
    thesis: { type: 'string' },
    architecture: { type: 'string' },
    whyThorAdreno740Specific: { type: 'string' },
    howItCutsBinningOrCpuWork: { type: 'string' },
    umaAngle: { type: 'string' },
    advancedMathAngle: { type: 'string' },
    risks: { type: 'array', items: { type: 'string' } },
    firstBuildVerifiedStep: { type: 'object', additionalProperties: false, properties: {
      file: { type: 'string' }, change: { type: 'string' }, cvarName: { type: 'string' },
      howToBuildVerify: { type: 'string' }, expectedEffect: { type: 'string' }, reversible: { type: 'boolean' }
    }, required: ['file', 'change', 'cvarName', 'expectedEffect'] },
    selfScore: { type: 'number' }
  },
  required: ['approachName', 'thesis', 'architecture', 'howItCutsBinningOrCpuWork', 'firstBuildVerifiedStep', 'selfScore']
};
const angles = [
  { label: 'design:concat-coalesce', prompt: 'Design angle A - DRAW CONCATENATION/COALESCING: a layer detecting consecutive draws sharing pipeline+descriptors+render-target and merging them into ONE vkCmdDraw over concatenated vertex/index data (bins each triangle once, pays per-draw fixed cost once). NOT instancing. Where to intercept in IssueDraw; how to concatenate guest index/vertex buffers (CPU+NEON+UMA zero-copy); preserve draw order/blend correctness; handle scattered vs contiguous index sources.' },
  { label: 'design:state-churn', prompt: 'Design angle B - STATE/DESCRIPTOR CHURN ELIMINATION: cut the ~2000 per-draw descriptor binds + pipeline/context rolls via persistent/bindless descriptors, larger descriptor arrays, and converting pipeline-forcing guest state into Vulkan DYNAMIC state so consecutive draws stop forcing context rolls/bin restarts on Adreno 740. Which state changes force a new pipeline today and which are dynamic-state-eligible?' },
  { label: 'design:primitive-rearch', prompt: 'Design angle C - PRIMITIVE/GEOMETRY RE-ARCH: reduce GPU geometry/binning work by transforming the guest primitive stream more cheaply - CPU-side (NEON+UMA) expansion/merge of point sprites/quads/tiny strips into batched triangle buffers, dedup, pre-binning hints. How are tiny draws expanded today and how to feed the GPU fewer, larger, state-stable draws?' },
  { label: 'design:tbdr-native', prompt: 'Design angle D - TBDR-NATIVE BINNING REDUCTION: restructure render passes/subpasses/EDRAM transitions + Adreno-specific flags to minimize bin restarts and context rolls (defer/merge render-pass breaks, keep LRZ valid, avoid mid-pass RT ownership transfers that fragment mergeable draw runs). Address brk_ causes and which are batchable.' },
  { label: 'design:advanced-math', prompt: 'Design angle E - ADVANCED ACCELERATION MATH (the user-requested hardware exploit): map guest VMX128/PPC geometry-transform math (matrix-vector, dot, MUL_ADD/FMA, skinning, lighting) onto SD8Gen2 NEON/i8mm/bf16/dotprod/fcma in the a64 backend AND/OR a CPU-side batched transform that feeds the GPU front-end. Be honest that the heavy scene is GPU-bound (CPU ~84% idle), so frame this as: (1) using spare CPU+advanced-math to do work that REDUCES GPU front-end load (e.g. CPU pre-transform/cull/merge so the GPU bins fewer/bigger draws), and/or (2) general a64 vector-math throughput wins that help CPU-bound scenes/other titles. Identify the single most defensible, gated, build-verifiable first step.' }
];
function designThunk(a) {
  return function () {
    return agent(a.prompt + '\n\nBINNING VERDICT: ' + JSON.stringify(verdict) + '\nFINDINGS: ' + digest + '\n' + FACTS + '\n\nProduce a DEEP concrete architecture and a precise FIRST build-verifiable, gated, reversible step (real file, real cvar name, how to confirm it compiles+links, expected effect). Self-score 0-100 on impact x feasibility x correctness-safety.', { label: a.label, phase: 'Design', schema: DESIGN_SCHEMA });
  };
}
const rawDesigns = await parallel(angles.map(designThunk));
const designs = rawDesigns.filter(Boolean);
log('Design: ' + designs.length + '/' + angles.length + ' approaches');

phase('Verify');
const VERDICT_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    approachName: { type: 'string' },
    reducesRealWork: { type: 'boolean' },
    notJustHostCommandCount: { type: 'string' },
    correctnessRisks: { type: 'array', items: { type: 'string' } },
    firstStepIsBuildVerifiable: { type: 'boolean' },
    refutationAttempt: { type: 'string' },
    verdict: { type: 'string', 'enum': ['strong', 'viable', 'weak', 'refuted'] },
    score: { type: 'number' }
  },
  required: ['approachName', 'reducesRealWork', 'verdict', 'score']
};
function verifyThunk(d) {
  return function () {
    return agent('Adversarially VERIFY this design for Adreno 740 TBDR + SD8Gen2. The #1 trap: a change that only reduces HOST command/draw count but makes the GPU re-bin/re-rasterize the SAME total triangles is NOT a win (killed exact instancing). Prove whether it reduces REAL work (fewer context rolls / fewer total binned primitives / fewer bin restarts / genuinely-offloaded CPU math that lowers GPU load), or just host count. Check correctness (draw order, blending, RT state, MSAA) and whether the first step is genuinely build-verifiable+gated+reversible. Try hard to REFUTE; default skeptical.\n\nDESIGN: ' + JSON.stringify(d) + '\nBINNING VERDICT: ' + JSON.stringify(verdict) + '\n' + FACTS, { label: 'verify', phase: 'Verify', schema: VERDICT_SCHEMA }).then(function (v) { return { design: d, verdict: v }; });
  };
}
const rawVerified = await parallel(designs.map(verifyThunk));
const liveVerified = rawVerified.filter(Boolean);

phase('Synthesize');
const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    chosenApproach: { type: 'string' },
    rationale: { type: 'string' },
    graftedIdeas: { type: 'array', items: { type: 'string' } },
    deepRearchDirection: { type: 'string' },
    firstLever: { type: 'object', additionalProperties: false, properties: {
      title: { type: 'string' }, file: { type: 'string' }, exactChange: { type: 'string' },
      cvarName: { type: 'string' }, buildVerify: { type: 'string' }, expectedGpuEffect: { type: 'string' },
      howToAbDeviceLater: { type: 'string' }, reversible: { type: 'boolean' }
    }, required: ['title', 'file', 'exactChange', 'cvarName', 'buildVerify', 'expectedGpuEffect'] },
    sequencedRoadmap: { type: 'array', items: { type: 'string' } },
    openRisks: { type: 'array', items: { type: 'string' } },
    honestUncertainties: { type: 'array', items: { type: 'string' } }
  },
  required: ['chosenApproach', 'rationale', 'firstLever', 'sequencedRoadmap', 'deepRearchDirection']
};
const synth = await agent('Synthesize the final plan. Pick the WINNING deep re-architecture for cutting Blue Dragon Adreno-740 TBDR binning/front-end cost (the proven bottleneck) while honoring the user directive to exploit the Thor hardware + advanced acceleration math + UMA. Graft the best ideas from runners-up. Prefer the approach the adversary could NOT refute as a real-work reducer. Specify the EXACT first build-verifiable, gated, reversible lever to implement NOW (real file, exact change, cvar name, how to build-verify compiles+links, expected GPU effect, how to A/B on device later when reconnected). Give a sequenced roadmap toward the full re-arch (incl. how UMA + CPU advanced-math feed it), open risks, honest uncertainties.\n\nDESIGNS+VERDICTS: ' + JSON.stringify(liveVerified).slice(0, 20000) + '\nBINNING VERDICT: ' + JSON.stringify(verdict) + '\n' + FACTS, { phase: 'Synthesize', schema: SYNTH_SCHEMA });

return { binningVerdict: verdict, synthesis: synth, designCount: designs.length, verifiedCount: liveVerified.length };
