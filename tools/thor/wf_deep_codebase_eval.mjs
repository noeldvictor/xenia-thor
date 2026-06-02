export const meta = {
  name: 'xenia-deep-novel-research-grounded-eval',
  description: 'DEEP + NOVEL whole-codebase improvement ideation for xenia-thor (Xbox360 emu, AYN Thor / Snapdragon 8 Gen 2 / Adreno-740). Stage 1 RESEARCH: web-fetch + distill authoritative Qualcomm/Adreno Vulkan-optimization + Adreno-740/A7xx TBDR + PowerPC/Xenon/VMX128 docs + novel mobile-emulator techniques (the download+RAG step). Stage 2 IDEATE: per-subsystem agents, fed the research, propose NOVEL hardware-grounded improvements (not just audit). Stage 3 VERIFY: adversarial divergence-safety + feasibility + novelty-is-real. Stage 4 SYNTHESIZE: prioritized novel-opportunity report. Guardrail: guest-visible math must stay bit-exact (cannot approximate; proven to break games).',
  phases: [
    { title: 'Research', detail: 'web-fetch + distill Adreno/Snapdragon + PowerPC/Xenon/VMX + novel-emulator docs into reusable technique notes' },
    { title: 'Ideate', detail: '9 per-subsystem agents, fed the research, propose NOVEL hardware-grounded improvements' },
    { title: 'Verify', detail: 'adversarial: divergence-safety, feasibility/real, novelty-grounded-not-hallucinated, honest impact' },
    { title: 'Synthesize', detail: 'prioritized novel-opportunity report + saveable doc' }
  ]
};

const CONTEXT = [
  'PROJECT: xenia-thor, Xbox360 emulator. Guest: PowerPC Xenon (3 cores, big-endian, VMX128 SIMD, in-order). Host: AYN Thor - Snapdragon 8 Gen 2 (Cortex-X3 + 2x A715 + 2x A710 + 3x A510; NEON/DotProd/I8MM/BF16/FCMA/FP16/FlagM/LSE; NO SVE2/LSE2), Adreno 740 (Vulkan, TBDR/FlexRender, fp16 ALU 2x but guest shaders fp32, max 680MHz, NO fragment_shader_interlock). a64 ARM64 JIT for guest PPC; Vulkan GPU backend; true-UMA capable.',
  'MISSION: think DEEP and NOVEL. Ground every idea in (a) the authoritative hardware docs distilled in the Research stage AND (b) the actual xenia-thor code (file:line). Propose genuinely NEW improvements - GPU-driven rendering, multi-threaded command recording, indirect/multi-draw, a different binning/draw strategy, novel data-movement, novel JIT/threading - not just an audit of existing code. Imagination is wanted; but it MUST be grounded + feasible + honest.',
  'PROVEN BOTTLENECK (device-measured): the headline Blue Dragon heavy 3D scene is GPU-BOUND on the geometry/per-draw/BINNING front-end (Adreno gpu_busy 77-79%@615MHz; ~2000 tiny draws/frame, ~136 verts avg, ~2000 descriptor binds; gpu_frame_us super-linear in draw+vertex count, pixel-INDEPENDENT; tile load/store ~3%, render-pass breaks ~12%, fill 0%). CPU ~84% IDLE on that scene. So GPU geometry/binning/draw-submission/state/render-pass work is the HIGHEST-value target; CPU/threading/audio/load wins are SECONDARY for that scene (matter for CPU-bound titles, load, audio). A NOVEL idea that cuts the per-draw or per-triangle binning cost is the grand prize.',
  'HARD GUARDRAIL (the lesson that overrides cleverness): classify every idea GUEST-VISIBLE (feeds rendering/game-logic/audio - must reproduce Xenos bit-exactly) vs OUR-INTERNAL/HEURISTIC (safe to optimize/approximate). Approximating guest-visible math DIVERGES AND BREAKS GAMES - PROVEN: arm64_vmx_dot_f32_fastpath black-screened Blue Dragon; fp16 cannot use FCVTL/FCVTN (Xenos exp-31 is normal). A novel guest-visible technique is only valid if bit-equivalent OR proven+gated+device-validated. Device-creation/feature-chain changes are unvalidatable-blind-risky (can black-screen all games) - mark those device-gated. Mark each idea safeToBuildBlind vs device-gated.',
  'ALREADY DONE (do NOT re-pitch; BUILD ON or go beyond): fast-math at ceiling (VMX->NEON, FlagM, LSE, byteswap, dc-zva; the unused exp2/log2/fp-dot/frecpe/fp16/FMA are BLOCKED by divergence - docs/research/20260601-fastmath-hardware-ceiling-audit.md). GPU binning levers BUILT: EDS dynamic state (cull/depth/stencil/topology) + draw concatenation (vulkan_merge_draws) + read-only scaffolding (elig_runlen, merge_miss, cullable_tris). EDS3 blend DESIGNED (device-gated). Blend-mapping refactor shipped. UMA present path present-HANGS (OFF, device-diagnostic-only). The PRIOR research (docs/research/20260601-arm64-adreno-speed-techniques.md, -binning-rearch-plan, -draw-concatenation-design, -frontb-cpu-cull-design) established: per-DRAW cost is coalescable (concat), per-TRIANGLE binning floor is NOT (only a real cull reduces it); instancing is DEAD on TBDR. GO BEYOND these.',
  'OUTPUT RULES: cite REAL sources for hardware claims (Qualcomm Adreno GPU SDK / Snapdragon optimization guide, Khronos Vulkan, IBM/Xenon PowerPC docs, real emulator sources) found via WebSearch/WebFetch; cite file:line for code. Mark unverified claims. Honest feasibility + impact + divergence classification. A NOVEL idea that is grounded + feasible + safe (or device-gated) is gold; a hallucinated or game-breaking one is worse than nothing.'
].join('\n');

// --- Stage 1: RESEARCH (download + distill the docs) ---
const RESEARCH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    topic: { type: 'string' },
    techniques: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      name: { type: 'string' }, detail: { type: 'string' }, relevanceToXeniaThor: { type: 'string' }, source: { type: 'string' }, confidence: { type: 'string', 'enum': ['verified', 'likely', 'unverified'] }
    }, required: ['name', 'detail', 'relevanceToXeniaThor', 'source', 'confidence'] } },
    keyFactsForIdeation: { type: 'array', items: { type: 'string' } },
    sources: { type: 'array', items: { type: 'string' } }
  },
  required: ['topic', 'techniques', 'keyFactsForIdeation', 'sources']
};
const researchTopics = [
  { label: 'research:adreno-vulkan-optimization', prompt: 'Web-research the AUTHORITATIVE Qualcomm Adreno / Snapdragon Vulkan optimization best-practices (Qualcomm Adreno GPU SDK developer guide, Snapdragon GPU optimization docs, Khronos/ARM mobile-Vulkan guidance, GDC/Qualcomm talks). Distill concrete techniques for: minimizing draw-call + binning cost on a TBDR, descriptor-set management (avoiding per-draw binds), render-pass/subpass/tiling (load/store ops, dont-care), pipeline-state management, indirect/multi-draw, GPU-driven rendering, secondary command buffers / multi-threaded recording. Cite real URLs/docs. These feed novel ideation against ~2000 tiny draws/frame + ~2000 descriptor binds.' },
  { label: 'research:adreno-740-tbdr-arch', prompt: 'Web-research the Adreno 740 / Adreno A7xx microarchitecture + TBDR/FlexRender details: how the binning/visibility pass works, what makes per-DRAW and per-TRIANGLE cost on the front-end, tile memory, the geometry/position-shading stage, context rolls / state-change cost, low-resolution-Z, and any A7xx-specific Vulkan features (VK_QCOM_*, tile properties, render-pass-transform). Distill what specifically minimizes the binning/geometry front-end cost. Cite (freedreno/Turnip docs, Qualcomm, gpuinfo, analyses).' },
  { label: 'research:powerpc-xenon-vmx', prompt: 'Web-research the Xbox 360 / PowerPC Xenon CPU + VMX128 architecture: the 3-core in-order PPC, the VMX128 vector ISA specifics, the cache/memory model + coherency, the microcoded/slow instructions, branch behavior, and anything that informs better guest->ARM64 JIT translation or exploiting guest behavior (e.g. known hot patterns, the GPU command ring, how the title submits draws). Cite (IBM PowerPC docs, Xenon analyses, Free60/libxenon, emulator dev writeups).' },
  { label: 'research:novel-mobile-emu-techniques', prompt: 'Web-research NOVEL techniques other emulators/translators use to exploit mobile GPUs + ARM for high draw-call console workloads: GPU-driven rendering / indirect draws to cut CPU+binning cost, draw-call batching/merging strategies, async/parallel command recording, vertex pre-transform on CPU/compute, descriptor indexing/bindless, pipeline pre-warming, and how projects like Turnip/freedreno, RPCS3, Dolphin, Vulkan ports handle Adreno specifically. Cite real sources. Bias toward IDEAS that could cut a TBDR binning front-end bottleneck.' }
];
phase('Research');
const research = (await parallel(researchTopics.map(function (r) {
  return function () { return agent(r.prompt + '\n\n' + CONTEXT, { label: r.label, phase: 'Research', schema: RESEARCH_SCHEMA, agentType: 'general-purpose' }); };
}))).filter(Boolean);
log('Research: ' + research.length + '/' + researchTopics.length + ' topics distilled');
const researchDigest = JSON.stringify(research.map(function (r) { return { topic: r.topic, keyFacts: r.keyFactsForIdeation, techniques: (r.techniques || []).map(function (t) { return t.name + ': ' + t.detail; }) }; })).slice(0, 30000);

// --- Stage 2: IDEATE (novel, fed the research) ---
const IDEA_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    area: { type: 'string' },
    ideas: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      title: { type: 'string' },
      novelty: { type: 'string', 'enum': ['novel-for-this-codebase', 'extends-existing', 'known-technique-not-yet-applied'] },
      what: { type: 'string' },
      groundedIn: { type: 'string' },
      codeLocation: { type: 'string' },
      guestVisibleOrInternal: { type: 'string', 'enum': ['guest-visible', 'internal-heuristic', 'mixed'] },
      bitExactOrApprox: { type: 'string', 'enum': ['bit-exact', 'approx-but-internal-so-safe', 'approx-guest-visible-BLOCKED'] },
      appliesToBottleneck: { type: 'boolean' },
      impact: { type: 'string', 'enum': ['high', 'medium', 'low'] },
      safeToBuildBlind: { type: 'boolean' },
      effort: { type: 'string', 'enum': ['small', 'medium', 'large', 'research'] },
      risks: { type: 'string' }
    }, required: ['title', 'novelty', 'what', 'groundedIn', 'guestVisibleOrInternal', 'appliesToBottleneck', 'impact', 'safeToBuildBlind', 'effort'] } }
  },
  required: ['area', 'ideas']
};
const ideators = [
  { label: 'ideate:gpu-drawcall-binning', prompt: 'NOVEL ideas to cut the ~2000 tiny draws/frame + the TBDR binning front-end cost (THE bottleneck). Consider: indirect/multi-draw-indirect, GPU-driven rendering, draw merging beyond the shipped list-concat, reducing geometry-shader-emulated draws (rect/quad/point lists), a CPU/compute cull feeding fewer triangles, batching by state. Ground in the Adreno-TBDR research + primitive_processor/draw_util/vulkan_command_processor. Highest-value area.' },
  { label: 'ideate:gpu-descriptor-pipeline', prompt: 'NOVEL ideas for descriptor + pipeline-state management to kill the ~2000 descriptor binds/frame + pipeline churn: VK_EXT_descriptor_buffer, descriptor indexing/bindless, push descriptors, persistent/cached descriptor sets, pipeline-cache + fast-link (graphics_pipeline_library), reducing state changes. Ground in the Adreno research + vulkan_command_processor + vulkan_pipeline_cache.' },
  { label: 'ideate:gpu-renderpass-edram-tiling', prompt: 'NOVEL ideas for render-pass/EDRAM/tiling to cut the ~12% render-pass-breaks + ~3% tile load/store: subpass merging, load/store dont-care, fewer RT-ownership transfers, VK_QCOM tile features, on-tile resolve, restructuring the EDRAM emulation for the TBDR. Ground in Adreno-TBDR research + render_target_cache.' },
  { label: 'ideate:gpu-shader-geometry', prompt: 'NOVEL ideas for shader translation + the geometry-shader-emulated primitives (which inflate draw count): avoid geometry shaders (compute pre-expansion, vertex-pulling, native primitives), better SPIR-V for the Adreno compiler, transform-feedback/compute vertex pre-transform. Ground in research + spirv_shader_translator + the geometry-shader paths. Keep guest-visible output bit-exact.' },
  { label: 'ideate:threading-parallel-recording', prompt: 'NOVEL threading/parallelism: is the GPU command-processor a SINGLE thread (a serial bottleneck for the binning front-end)? Consider multi-threaded/secondary-command-buffer recording, parallel draw/descriptor recording, guest-3-core->host-core mapping, big.LITTLE prime-core pinning for the CP/JIT/audio threads. Ground in research (Adreno multi-threaded recording, big.LITTLE) + command_processor + the threading model. HIGH potential if the CP thread is the wall.' },
  { label: 'ideate:jit-codegen-novel', prompt: 'NOVEL a64 JIT improvements grounded in PowerPC-Xenon + ARM research, BEYOND the at-ceiling fast-math: block linking/chaining + threaded dispatch, guest-register residency/allocation, handling Xenon in-order/microcoded ops, fastmem refinements, reducing per-block overhead, parallel/background recompilation. Ground in research + the a64 backend. Secondary to GPU bottleneck but real for CPU-bound titles.' },
  { label: 'ideate:memory-uma-datamovement', prompt: 'NOVEL memory/UMA/data-movement ideas: fixing the UMA present-hang to UNLOCK zero-copy (per the device-diagnostic root-cause), buffer_device_address, zero-copy vertex/index feeding, reducing upload/copy, persistent mapping done right, exploiting the Adreno coherent UMA. Ground in research (Snapdragon UMA/coherency) + vulkan_shared_memory + the present path. UMA is the headline architecture if the present-hang is solved.' },
  { label: 'ideate:cpu-gpu-cooperative', prompt: 'NOVEL CPU<->GPU cooperative ideas exploiting the ~84%-idle CPU to relieve the GPU binning front-end: CPU/compute vertex pre-transform + a real cull (reduce triangles BINNED), CPU-side draw stream reorganization/dedup, predictive pipeline pre-warming, async work. Ground in research + the Front-B design (frontb-cpu-cull) + draw_extent_estimator. Mind: a CPU cull that drops a visible triangle is a correctness bug (device-gated) - but the IDEAS can be novel.' },
  { label: 'ideate:wildcard-architecture', prompt: 'WILDCARD: the single most NOVEL high-impact architectural idea you can ground in the research + the bottleneck - a fundamentally different approach to the Xbox360-on-Adreno geometry/binning problem (e.g. GPU-driven submission, a visibility-buffer/deferred approach, caching binned geometry across frames, exploiting an Adreno-specific feature no one uses). Be bold but ground it + be honest about feasibility/divergence/effort.' }
];
phase('Ideate');
const ideas = (await parallel(ideators.map(function (i) {
  return function () { return agent(i.prompt + '\n\nRESEARCH (distilled hardware docs to ground your ideas):\n' + researchDigest + '\n\n' + CONTEXT, { label: i.label, phase: 'Ideate', schema: IDEA_SCHEMA, agentType: 'general-purpose' }); };
}))).filter(Boolean);
log('Ideate: ' + ideas.length + '/' + ideators.length + ' areas');
const ideaDigest = JSON.stringify(ideas.flatMap(function (a) { return (a.ideas || []).map(function (d) { return { area: a.area, title: d.title, gv: d.guestVisibleOrInternal, bottleneck: d.appliesToBottleneck, impact: d.impact, safe: d.safeToBuildBlind, effort: d.effort }; }); })).slice(0, 26000);

// --- Stage 3: VERIFY ---
const VERIFY_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    lens: { type: 'string' },
    upheld: { type: 'array', items: { type: 'object', additionalProperties: false, properties: { title: { type: 'string' }, why: { type: 'string' } }, required: ['title', 'why'] } },
    rejected: { type: 'array', items: { type: 'object', additionalProperties: false, properties: { title: { type: 'string' }, reason: { type: 'string' } }, required: ['title', 'reason'] } },
    notes: { type: 'string' }
  },
  required: ['lens', 'upheld', 'rejected']
};
const verifyLenses = [
  { label: 'verify:divergence-safety', prompt: 'Adversarially reject any idea that approximates guest-visible math/rendering away from Xenos (the proven game-breaking trap). Uphold only bit-exact or internal/heuristic ideas. Be strict.' },
  { label: 'verify:feasibility-grounded', prompt: 'Adversarially check each idea is GROUNDED (the cited hardware fact + Adreno-740 actually supports it - e.g. is the Vulkan extension exposed? is multi-threaded recording actually a win on Adreno? is the claim from a real source or hallucinated?) and FEASIBLE in this codebase (cite the real code it would touch). Reject hallucinated-capability or architecturally-infeasible ideas. Use WebSearch/Grep to check.' },
  { label: 'verify:impact-and-device-risk', prompt: 'Adversarially check HONEST impact vs the GPU-bound bottleneck (does it actually cut per-draw/per-triangle/binning/render-pass cost, or is it secondary?) and device-risk (safeToBuildBlind vs device-gated vs device-creation-risky-unvalidatable). Reject over-rated or "moves nothing on the real bottleneck" ideas. Identify the few that genuinely could move the BD framerate.' }
];
phase('Verify');
const verify = (await parallel(verifyLenses.map(function (l) {
  return function () { return agent(l.prompt + '\n\nIDEAS: ' + ideaDigest + '\n\n' + CONTEXT, { label: l.label, phase: 'Verify', schema: VERIFY_SCHEMA, agentType: 'general-purpose' }); };
}))).filter(Boolean);

// --- Stage 4: SYNTHESIZE ---
const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    headline: { type: 'string' },
    rankedOpportunities: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      rank: { type: 'number' }, title: { type: 'string' }, codeLocation: { type: 'string' }, what: { type: 'string' }, groundedIn: { type: 'string' }, impact: { type: 'string', 'enum': ['high', 'medium', 'low'] }, appliesToBottleneck: { type: 'boolean' }, safeToBuildBlind: { type: 'boolean' }, effort: { type: 'string' }, firstStep: { type: 'string' }
    }, required: ['rank', 'title', 'what', 'impact', 'appliesToBottleneck', 'safeToBuildBlind', 'firstStep'] } },
    docMarkdown: { type: 'string' },
    honestCaveats: { type: 'array', items: { type: 'string' } }
  },
  required: ['headline', 'rankedOpportunities', 'docMarkdown']
};
phase('Synthesize');
const synth = await agent('Synthesize the research-grounded NOVEL ideation into a PRIORITIZED opportunity report. Keep ONLY ideas that survived verification (bit-exact-or-internal, grounded-and-feasible, honestly-impactful). Rank by REAL impact on the GPU-bound Blue Dragon bottleneck first (per-draw/per-triangle/binning/render-pass/descriptor wins), then secondary. For each: code location, what, what hardware fact it is grounded in, impact, appliesToBottleneck, safeToBuildBlind vs device-gated, effort, and a concrete FIRST step. Headline = is there genuine NOVEL untapped performance, and where is the single highest-leverage one. docMarkdown = a complete saveable research doc (with sources). Be honest about device-gated vs buildable-now and about what is speculative. Do NOT include guest-visible approximations.\n\nIDEAS: ' + JSON.stringify(ideas).slice(0, 40000) + '\n\nVERIFY: ' + JSON.stringify(verify).slice(0, 16000) + '\n\nRESEARCH SOURCES: ' + JSON.stringify(research.map(function (r) { return r.sources; })).slice(0, 6000) + '\n\n' + CONTEXT, { phase: 'Synthesize', schema: SYNTH_SCHEMA, agentType: 'general-purpose' });

return { research: research, ideas: ideas, verify: verify, synthesis: synth };
