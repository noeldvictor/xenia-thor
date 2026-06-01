export const meta = {
  name: 'deep-thor-draw-concatenation-design',
  description: 'Design + adversarially verify the zero-copy draw-concatenation lever (Lever 2) for the Adreno-740 binning re-arch before building it blind: exact merge predicate, all flush points, correctness traps, and a line-anchored incremental build plan. Device disconnected - design/read only.',
  phases: [
    { title: 'Understand', detail: 'parallel readers map the draw-emission path, state tracking, flush points, vertex/index addressing, and merge instrumentation' },
    { title: 'Design', detail: 'synthesize the concatenation architecture: intercept, predicate, flush points, index accumulation, merged emission' },
    { title: 'Verify', detail: 'adversarial verifiers attack correctness/ordering, vertex-index addressing, and flush completeness' },
    { title: 'Synthesize', detail: 'precise line-anchored incremental build plan + first safe increment' }
  ]
};

const FACTS = [
  'PROJECT: xenia-thor (Xbox360 emulator fork) targeting AYN Thor / Adreno-740 (TBDR) / Vulkan. Device DISCONNECTED - design + code-read only, NO device, NO perf numbers. Cite file:line from the ACTUAL repo (Read/Grep); do not invent.',
  'GOAL: LEVER 2 = zero-copy DRAW CONCATENATION. The proven heavy-scene cost is MIXED: a coalescable PER-DRAW fixed cost (Adreno context roll / bin restart / state re-latch, ~per vkCmdDraw) + a non-coalescable per-triangle floor. ~1200-2148 tiny draws/frame. Concatenation merges CONSECUTIVE same-state draws into ONE vkCmdDrawIndexed over a concatenated index range -> pays the per-draw fixed cost ONCE per run while binning each triangle exactly once. This is the direct attack on the per-draw component.',
  'KEY in-repo facts (verify exact current lines): one guest draw packet -> one IssueDraw -> exactly one CmdVkDraw / CmdVkDrawIndexed in vulkan_command_processor.cc (~3909 / ~3937). CmdVkBindIndexBuffer emitted ~3931 (currently unconditional - a last-bound-IB cache is a precursor). Index source classified by primitive_processing_result.index_buffer_type (kGuestDMA = indexes directly into shared_memory_->buffer()); guest_index_base + host_draw_vertex_count + host_index_format (kInt16/kInt32) describe the byte range. NO host vertex buffer (manual in-shader vfetch: gl_VertexIndex + system constants). Vertex addressing system constants set in vulkan_command_processor.cc: vertex_index_load_address (~5217), vertex_index_endian (~5224), vertex_base_index = VGT_INDX_OFFSET (~5230).',
  'FRONT A (DONE, gated cvars, build-verified): EDS dynamic-state promotion (cull/front-face, depth, stencil, topology+restart) collapses pipeline variants so MORE consecutive draws share current_guest_graphics_pipeline_ -> longer same-pipeline runs for concatenation to merge. merge_run_hist_ (run-lengths of consecutive same-pipeline draws), merge_vf_same_/contig_/scattered_ (index-source adjacency, stride-corrected by cvar gpu_merge_vf_index_stride_fix), merge_consts_same_/changed_ (per-draw VS float-constant churn) instrumentation already exists in IssueDraw (~3953-3991) and was built to size mergeability.',
  'SAME-STATE MERGE PREDICATE (a run is mergeable only if ALL hold across consecutive draws): same current_guest_graphics_pipeline_ (VkPipeline); same bound descriptor sets (textures/samplers/constants); same render pass + framebuffer (no SubmitBarriersAndEnterRenderTargetCacheRenderPass break between them); same VS float constants (merge_consts_same_ - if constants change per draw the run is length-1 and nothing merges); index source == kGuestDMA with CONTIGUOUS byte range (prev_end == next guest_index_base, stride-correct); SAME primitive topology (or both promoted to the dynamic kTriangleList class); AND identical vertex addressing: vertex_base_index (VGT_INDX_OFFSET), vertex_index_load_address, vertex_index_endian - because the in-shader vfetch computes addresses from these + gl_VertexIndex, so two draws can only share one concatenated index range if their vertex addressing is identical.',
  'FLUSH POINTS (a pending deferred run MUST be flushed = emit ONE CmdVkBindIndexBuffer + ONE CmdVkDrawIndexed before): any predicate field changes; render-pass / framebuffer break (SubmitBarriersAndEnterRenderTargetCacheRenderPass); pipeline change; descriptor-set change; VS-constant invalidation; memexport draw (kGuestDrawReadWrite); IssueCopy / resolve; end of frame (IssueSwap); end of submission (EndSubmission / SubmitBarriers); any non-draw command that depends on prior draws having executed; trace/readback. A MISSED flush = dropped or mis-ordered geometry (invisible to validation, only catchable on device).',
  'CONSTRAINTS: gate behind a cvar vulkan_merge_draws (default false) = bit-identical when off. Build-verifiable incrementally (BUILD SUCCESSFUL + symbol linked in arm64 libxenia-app.so). The merged draw must be 100% equivalent to the N separate draws (same triangles, same order, same state). Adreno instancing is DEAD (re-bins N times); this is CONCATENATION (one bind + one draw over a merged index range), not instancing.'
].join('\n');

const READ_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    subsystem: { type: 'string' },
    hotspots: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      file: { type: 'string' }, line: { type: 'string' }, what: { type: 'string' }
    }, required: ['file', 'what'] } },
    stateChangePoints: { type: 'array', items: { type: 'string' } },
    flushPoints: { type: 'array', items: { type: 'string' } },
    keyFindings: { type: 'array', items: { type: 'string' } }
  },
  required: ['subsystem', 'keyFindings']
};

const readers = [
  { label: 'read:issuedraw-emission', prompt: 'Read vulkan_command_processor.cc IssueDraw, focus on the GPU draw EMISSION: the CmdVkDraw / CmdVkDrawIndexed calls (~3909/3937), the CmdVkBindIndexBuffer (~3931), and how firstIndex/indexCount/vertexCount are derived from primitive_processing_result (guest_index_base, host_draw_vertex_count, index_buffer_type, host_index_format). Exactly where could a deferred-draw interceptor sit, and what values must be captured per draw to later emit ONE merged CmdVkDrawIndexed over a concatenated index range? file:line.' },
  { label: 'read:state-tracking', prompt: 'Read vulkan_command_processor.cc/.h for the per-draw state members that define when two draws share state: current_guest_graphics_pipeline_, current render pass/framebuffer, descriptor-set binding state, and the VS float-constant up-to-date tracking. Where is each set/changed in the draw path? Which are the cheap equality checks for a same-state merge predicate? file:line.' },
  { label: 'read:flush-points', prompt: 'Read vulkan_command_processor.cc for EVERY place a pending deferred draw-run would have to be flushed before: SubmitBarriersAndEnterRenderTargetCacheRenderPass (render-pass break ~3892), SubmitBarriers, EndSubmission, IssueCopy/resolve, IssueSwap (frame end), the command-buffer reset (~4448), memexport (kGuestDrawReadWrite ~3886), trace/readback. Enumerate them with file:line. A missed flush = dropped/mis-ordered geometry - be exhaustive.' },
  { label: 'read:vertex-index-addressing', prompt: 'Read vulkan_command_processor.cc ~5210-5235 (vertex_index_load_address, vertex_index_endian, vertex_base_index/VGT_INDX_OFFSET system constants) + spirv_shader_translator.cc in-shader vfetch (gl_VertexIndex usage) + the kGuestDMA index path (~3914). Determine EXACTLY what must be identical across draws for their index ranges to be concatenated into one CmdVkDrawIndexed without corrupting vertex addressing - and whether firstIndex offset vs a re-based index range is the right mechanism. file:line.' },
  { label: 'read:merge-instrumentation', prompt: 'Read the merge instrumentation in vulkan_command_processor.cc (~3953-3991: merge_run_hist_, merge_run_len_, merge_run_pipeline_, merge_vf_same_/contig_/scattered_, merge_consts_same_/changed_). What does each already compute about consecutive-draw mergeability, and how can the concatenation predicate REUSE this logic (esp. the stride-correct contiguity from gpu_merge_vf_index_stride_fix)? file:line.' }
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
function digestOf(f) { return { sub: f.subsystem, find: f.keyFindings, flush: f.flushPoints || [], state: f.stateChangePoints || [] }; }
const digest = JSON.stringify(findings.map(digestOf)).slice(0, 18000);

phase('Design');
const DESIGN_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    architecture: { type: 'string' },
    interceptPoint: { type: 'string' },
    mergePredicateFields: { type: 'array', items: { type: 'string' } },
    flushPoints: { type: 'array', items: { type: 'string' } },
    indexAccumulation: { type: 'string' },
    mergedEmission: { type: 'string' },
    risks: { type: 'array', items: { type: 'string' } }
  },
  required: ['architecture', 'interceptPoint', 'mergePredicateFields', 'flushPoints', 'mergedEmission']
};
const design = await agent('Design the zero-copy draw-concatenation architecture for IssueDraw using the findings. Specify: the exact intercept point (where to defer the draw); the data captured per draw; the FULL same-state merge predicate; how index ranges are accumulated (contiguous kGuestDMA -> single CmdVkDrawIndexed with firstIndex/indexCount spanning the run, since indices DMA from shared_memory_->buffer()); the merged emission (one CmdVkBindIndexBuffer + one CmdVkDrawIndexed); and EVERY flush point. Be concrete and line-anchored.\nFINDINGS: ' + digest + '\n' + FACTS, { phase: 'Design', schema: DESIGN_SCHEMA });

phase('Verify');
const VERDICT_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    lens: { type: 'string' },
    correctnessHoles: { type: 'array', items: { type: 'string' } },
    missedFlushPoints: { type: 'array', items: { type: 'string' } },
    predicateGaps: { type: 'array', items: { type: 'string' } },
    verdict: { type: 'string', 'enum': ['sound', 'fixable', 'unsafe'] },
    requiredFixes: { type: 'array', items: { type: 'string' } }
  },
  required: ['lens', 'verdict', 'requiredFixes']
};
const lenses = [
  { label: 'verify:ordering', prompt: 'Adversarially attack the concatenation design for DRAW-ORDER / state correctness: can a state change (blend, viewport, scissor, dynamic state, descriptor, render-target) between two draws in a run be missed by the predicate so the merged draw renders with wrong state? Can merging reorder relative to a non-draw command? Find holes. Default skeptical.' },
  { label: 'verify:vertex-index', prompt: 'Adversarially attack the VERTEX/INDEX addressing: with manual in-shader vfetch (gl_VertexIndex + vertex_base_index/load_address/endian system constants) and kGuestDMA indices into shared_memory_->buffer(), does concatenating index ranges into one CmdVkDrawIndexed actually fetch the right vertices for EACH original draw? What about non-contiguous ranges, 16- vs 32-bit indices, differing vertex_base_index, index endian, or primitive-reset/strip boundaries (a merged strip would stitch triangles across draw boundaries - is that wrong)? Find the traps.' },
  { label: 'verify:flush-completeness', prompt: 'Adversarially attack FLUSH COMPLETENESS: enumerate every code path that ends/breaks a render pass, submits, resolves, swaps, reads back, or otherwise depends on prior draws having executed, and check the design flushes the pending run before EACH. A missed flush = dropped/mis-ordered geometry, invisible to validation. Find any missed flush point.' }
];
function verifyThunk(l) {
  return function () {
    return agent(l.prompt + '\n\nDESIGN: ' + JSON.stringify(design) + '\nFINDINGS: ' + digest + '\n' + FACTS, { label: l.label, phase: 'Verify', schema: VERDICT_SCHEMA });
  };
}
const rawVerdicts = await parallel(lenses.map(verifyThunk));
const verdicts = rawVerdicts.filter(Boolean);

phase('Synthesize');
const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    feasibility: { type: 'string', 'enum': ['build-now', 'build-with-fixes', 'too-risky-blind'] },
    architecture: { type: 'string' },
    mergePredicate: { type: 'array', items: { type: 'string' } },
    flushPoints: { type: 'array', items: { type: 'string' } },
    correctnessFixesRequired: { type: 'array', items: { type: 'string' } },
    incrementalPlan: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      step: { type: 'string' }, files: { type: 'string' }, change: { type: 'string' }, buildVerify: { type: 'string' }, gated: { type: 'boolean' }
    }, required: ['step', 'change'] } },
    firstIncrement: { type: 'object', additionalProperties: false, properties: {
      title: { type: 'string' }, files: { type: 'string' }, exactChange: { type: 'string' }, cvar: { type: 'string' }, buildVerify: { type: 'string' }
    }, required: ['title', 'exactChange'] },
    openRisks: { type: 'array', items: { type: 'string' } },
    honestUncertainties: { type: 'array', items: { type: 'string' } }
  },
  required: ['feasibility', 'mergePredicate', 'flushPoints', 'incrementalPlan', 'firstIncrement']
};
const synth = await agent('Synthesize the final concatenation plan from the design + adversarial verdicts. Incorporate ALL required fixes from the verdicts into the merge predicate and flush points. CRITICAL: triangle STRIP concatenation would stitch triangles across draw boundaries (wrong) unless handled (e.g. only concatenate triangle LISTS, or insert primitive-restart / degenerate handling) - resolve this explicitly. Give: feasibility, the corrected merge predicate, the complete flush-point list, required correctness fixes, and a SMALL-INCREMENT build plan where each step is independently build-verifiable and gated behind vulkan_merge_draws (default off). Specify the FIRST increment precisely (likely: scaffolding - the cvar + a last-bound-index-buffer cache + a deferred-run state struct that initially flushes immediately = no behavior change). Be honest about what stays unverifiable until the device returns.\nDESIGN: ' + JSON.stringify(design) + '\nVERDICTS: ' + JSON.stringify(verdicts) + '\n' + FACTS, { phase: 'Synthesize', schema: SYNTH_SCHEMA });

return { design: design, verdicts: verdicts, synthesis: synth };
