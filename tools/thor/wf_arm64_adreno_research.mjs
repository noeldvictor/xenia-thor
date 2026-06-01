export const meta = {
  name: 'arm64-adreno-emulator-techniques-research',
  description: 'Research online (WebSearch/WebFetch) the ARM64 + Snapdragon Adreno-740 techniques an Xbox360 emulator (xenia-thor on AYN Thor / SD8Gen2) should exploit — vector/SIMD math (NEON, dotprod, i8mm, bf16, fcma), advanced ARM ISA, Adreno TBDR + Vulkan best practices, unified-memory/coherency, ARM64 JIT techniques from other emulators, big.LITTLE scheduling — then verify the load-bearing claims against authoritative sources and synthesize a prioritized, cited report mapped to xenia-thor.',
  phases: [
    { title: 'Research', detail: '6 parallel agents web-research distinct dimensions' },
    { title: 'Verify', detail: 'cross-check load-bearing ISA + Vulkan-extension claims against authoritative sources' },
    { title: 'Synthesize', detail: 'prioritized, cited report mapped to xenia-thor + a durable markdown doc body' }
  ]
};

const CONTEXT = [
  'TARGET HARDWARE: AYN Thor handheld. SoC: Snapdragon 8 Gen 2 (SM8550, "kalama"). GPU: Adreno 740 v2 — a tile-based DEFERRED renderer (TBDR), max 680 MHz, NO VK_EXT_fragment_shader_interlock. CPU: 1x Cortex-X3 (prime) + 2x A715 + 2x A710 + 3x A510. Per the repo CLAUDE.md the CPU advertises NEON, i8mm, bf16, dotprod, fcma. Verify exact FEAT_* support against authoritative sources, do not assume.',
  'EMULATOR: a fork of Xenia (Xbox 360 emulator). Guest CPU = IBM Xenon, 3x PowerPC cores, BIG-ENDIAN, with VMX128 (AltiVec-derived) 128-bit SIMD. Host CPU JIT = the a64 (ARM64) backend recompiling guest PPC -> ARM64. GPU backend = Vulkan. Memory model: TRUE UMA — the whole 512MB guest RAM can be a persistently-mapped HOST_VISIBLE|DEVICE_LOCAL Vulkan buffer that the CPU writes in place and the GPU reads (gpu_uma_direct_shared_memory). Shaders: guest Xenos ucode -> SPIR-V; there is also a CPU ShaderInterpreter that replays guest vertex shaders.',
  'KNOWN BOTTLENECK (device-proven, content-matched): the heavy Blue Dragon 3D scene is GPU-BOUND on the GEOMETRY / per-DRAW / BINNING front-end — ~2000 tiny draws/frame, ~136 verts avg, ~2000 descriptor binds/frame; gpu_frame_us is super-linear in draw/geometry count and pixel-INDEPENDENT (2x resolution = no change). The CPU is ~84% IDLE during those frames. Tile load/store ~3%, render-pass breaks ~12%, fill 0%.',
  'IMPLICATION FOR RANKING: GPU/binning/Vulkan-state techniques are the HEADLINE lever (they attack the proven bottleneck). CPU vector/SIMD math is SECONDARY for that scene (CPU is idle there) but still matters for: CPU-bound titles/scenes, guest VMX-heavy code, the CPU ShaderInterpreter / draw-extent / cull VS-replay, texture/vertex FORMAT conversion + endian byte-swap, XMA audio decode, and large guest memcpy/upload paths. Rank every technique by HONEST applicability to THIS emulator + bottleneck, not generic benchmark wins.',
  'ALREADY DONE in the fork (do not just re-suggest; note if a researched technique extends these): VK_EXT_extended_dynamic_state (EDS, core in 1.3) promotion of cull/front-face/depth/stencil/topology/primitive-restart to collapse pipeline variants; zero-copy draw concatenation of same-state contiguous LIST-topology draws; UMA direct shared memory (host-visible-device-local) with smart-sync upload guards; merge_vf vertex-fetch stride fix.',
  'OUTPUT RULES: every technique MUST cite a REAL source (URL or precise doc/title) found via WebSearch/WebFetch — ARM developer docs / ARM ARM, Qualcomm Adreno Vulkan developer guide, Khronos/Vulkan spec, gpuinfo.org (Vulkan hardware DB) for actual Adreno 740 extension exposure, real emulator source/blogs (FEX-Emu, Box64, AetherSX2/PCSX2, Dolphin, RPCS3, yuzu/Skyline, QEMU TCG, Apple Rosetta 2 writeups). If you cannot verify a claim, mark it UNVERIFIED rather than asserting it. No fabricated instruction mnemonics or extension names.'
].join('\n');

const RESEARCH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    dimension: { type: 'string' },
    techniques: {
      type: 'array',
      items: {
        type: 'object', additionalProperties: false,
        properties: {
          name: { type: 'string' },
          what: { type: 'string' },
          armFeatureOrVkExtension: { type: 'string' },
          howItHelpsEmulators: { type: 'string' },
          applicabilityToXeniaThor: { type: 'string', 'enum': ['high', 'medium', 'low', 'unknown'] },
          source: { type: 'string' },
          confidence: { type: 'string', 'enum': ['verified', 'likely', 'unverified'] },
          caveat: { type: 'string' }
        },
        required: ['name', 'what', 'howItHelpsEmulators', 'applicabilityToXeniaThor', 'source', 'confidence']
      }
    },
    crossEmulatorExamples: { type: 'array', items: { type: 'string' } },
    sources: { type: 'array', items: { type: 'string' } },
    openQuestions: { type: 'array', items: { type: 'string' } }
  },
  required: ['dimension', 'techniques', 'sources']
};

const dimensions = [
  {
    label: 'research:ppc-vmx-to-neon',
    prompt: 'Research how to vectorize/translate the Xbox360 guest PowerPC VMX128 (AltiVec) 128-bit SIMD ISA onto ARM64 NEON in a JIT. Cover: lane-for-lane mapping of common VMX ops to NEON, the hard cases (vperm/permute via TBL/TBX, vsel, vmsum/sum-of-products, saturating pack/unpack, vrlimi, splat, vcmpbfp), big-endian guest vector lane ordering vs little-endian NEON (REV/lane-reversal strategy), float exception/NaN/denormal differences (PPC VMX vs ARM NEON FPCR, flush-to-zero), and fp16/bf16 conversions. Find how real emulators map AltiVec/VMX to NEON (or SSE) and what the pitfalls are. Cite real sources.'
  },
  {
    label: 'research:arm64-advanced-isa',
    prompt: 'Research the ARMv8.2..v9 advanced CPU instructions on Cortex-X3 / Snapdragon 8 Gen 2 that an emulator/JIT can exploit, and CONFIRM which the X3 actually supports: dot-product (UDOT/SDOT, FEAT_DotProd), int8 matmul (SMMLA/UMMLA/USMMLA, FEAT_I8MM), bf16 (BFDOT/BFMMLA, FEAT_BF16), FP16 arithmetic (FEAT_FP16), complex FMA (FCMLA/FCADD, FEAT_FCMA), JS float-convert FJCVTZS (FEAT_JSCVT — useful for JITs), LSE atomics (CAS/SWP/LDADD, FEAT_LSE/LSE2), FRINTx, FEAT_FlagM/FlagM2 (CFINV/AXFLAG — relevant to translating guest condition flags), SVE/SVE2 availability and vector width on this SoC. For each: the use in emulation (guest SIMD accel, flag translation, atomics mapping, audio/texture math) and applicability to xenia-thor. Cite ARM developer docs / ARM ARM / Cortex-X3 TRM.'
  },
  {
    label: 'research:adreno-vulkan-tbdr',
    prompt: 'Research Adreno 740 (Snapdragon 8 Gen 2) TBDR + Vulkan best practices most relevant to an emulator that issues ~2000 tiny draws/frame with heavy pipeline-state and descriptor churn (a binning/geometry-front-end bottleneck). Cover: VK_EXT_extended_dynamic_state 1/2/3 and VK_EXT_vertex_input_dynamic_state (collapsing pipeline variants), VK_KHR_dynamic_rendering (avoiding render-pass/framebuffer objects), VK_EXT_graphics_pipeline_library + fast-linking, descriptor indexing / push descriptors / VK_EXT_descriptor_buffer, reducing per-draw binning cost, render-pass/subpass merging and load/store-op tuning on a tiler, primitive-restart and index/draw batching, and Qualcomm-specific VK_QCOM_* extensions (render_pass_transform, tile_properties, image_processing, fragment_density_map_offset, multiview_per_view_*). Use gpuinfo.org to confirm which extensions Adreno 740 ACTUALLY exposes. Read the Qualcomm Adreno Vulkan developer/optimization guide. Cite real sources, flag unexposed extensions.'
  },
  {
    label: 'research:snapdragon-uma-coherency',
    prompt: 'Research unified-memory / zero-copy / CPU<->GPU cache-coherency techniques on Snapdragon/Adreno for an emulator that maps guest RAM as a persistently-mapped HOST_VISIBLE|DEVICE_LOCAL Vulkan buffer written by the CPU and read by the GPU. Cover: Vulkan memory types on Adreno (host-visible-device-local, host-coherent vs non-coherent, write-combine), VK_EXT_external_memory_host (importing CPU memory), buffer_device_address, persistent mapping + vkFlushMappedMemoryRanges/invalidate semantics, the ARM CPU cache + Adreno GPU coherency domain on SD8Gen2 (is GPU access coherent with CPU caches? what flushes are required?), avoiding redundant copies/uploads, and submission/visibility ordering of CPU writes vs in-flight GPU reads. Note relevance to a known UMA present-hang. Cite ARM/Qualcomm/Khronos sources.'
  },
  {
    label: 'research:arm64-jit-techniques',
    prompt: 'Research dynamic-recompiler (JIT) techniques on ARM64 as used by real emulators/translators, that a PowerPC->ARM64 recompiler should adopt: fastmem (mmap guest address space + SIGSEGV/SIGBUS handling for MMIO), lazy/deferred condition-flag evaluation, register allocation + guest-register caching, block linking / block chaining, handling self-modifying code + code-cache invalidation (instruction-cache maintenance, IC IVAU/DC CVAU on ARM), W^X / RWX JIT memory on Android (mprotect, and how Apple uses pthread_jit_write_protect_np), mapping PPC strong memory ordering + lwarx/stwcx atomics onto ARM weak ordering + LSE/load-exclusive, big-endian guest byte-swap via REV/REV16/REV32, and indirect-branch/dispatch optimization. Pull concrete techniques from FEX-Emu, Box64, AetherSX2/PCSX2, Dolphin (ARM/Apple Silicon), RPCS3, QEMU TCG, Apple Rosetta 2 analyses. Cite real sources (source repos, dev blogs, talks).'
  },
  {
    label: 'research:big-little-scheduling-power',
    prompt: 'Research thread scheduling / core-affinity / power + thermal techniques for emulators on Snapdragon 8 Gen 2 big.LITTLE (1x Cortex-X3 prime + 2x A715 + 2x A710 + 3x A510 efficiency). Cover: pinning latency-critical emulator threads (the GPU command-processor/submit thread, the guest-core JIT threads, the audio thread) to the prime/performance cores via sched_setaffinity, the cost of migration between core types, cache/coherency differences across clusters, interaction with the Android scheduler/EAS/cpusets and DVFS governors, sustained-clock vs thermal throttling on a passively-or-small-fan handheld (the Adreno caps at 680MHz; the device has thermally crashed at 72C+), and how handheld emulator frontends (e.g. on RG/AYN/Retroid devices) configure affinity/governor for stable frametimes. Cite real sources (kernel docs, ARM docs, emulator/handheld community + dev writeups).'
  }
];

function researchThunk(d) {
  return function () {
    return agent(
      'Use WebSearch and WebFetch to research THIS dimension and return structured, SOURCED findings. Prefer authoritative/primary sources. Mark anything you cannot verify as confidence="unverified". Rank each technique by honest applicability to xenia-thor given its GPU-bound binning bottleneck (CPU mostly idle on the headline scene).\n\nDIMENSION: ' + d.prompt + '\n\n' + CONTEXT,
      { label: d.label, phase: 'Research', schema: RESEARCH_SCHEMA, agentType: 'general-purpose' }
    );
  };
}

phase('Research');
const rawResearch = await parallel(dimensions.map(researchThunk));
const research = rawResearch.filter(Boolean);
log('Research: ' + research.length + '/' + dimensions.length + ' dimensions returned');

// Aggregate all techniques + sources for the verify pass (barrier justified: verify
// cross-checks the FULL claim set against authoritative sources).
const allTechniques = research.flatMap(function (r) {
  return (r.techniques || []).map(function (t) {
    return { dim: r.dimension, name: t.name, feat: t.armFeatureOrVkExtension, conf: t.confidence, src: t.source };
  });
});
const claimDigest = JSON.stringify(allTechniques).slice(0, 16000);

phase('Verify');
const VERIFY_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    lens: { type: 'string' },
    confirmed: { type: 'array', items: { type: 'string' } },
    corrections: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      claim: { type: 'string' }, problem: { type: 'string' }, correction: { type: 'string' }, source: { type: 'string' }
    }, required: ['claim', 'problem'] } },
    notExposedOrUnsupported: { type: 'array', items: { type: 'string' } },
    sources: { type: 'array', items: { type: 'string' } }
  },
  required: ['lens', 'confirmed', 'corrections']
};
const verifyLenses = [
  {
    label: 'verify:arm-isa',
    prompt: 'Adversarially VERIFY the ARM CPU/ISA claims (FEAT_* support on Cortex-X3 / Snapdragon 8 Gen 2, instruction mnemonics, SVE2 width, FJCVTZS/LSE/I8MM/BF16/FCMA/DotProd/FlagM presence) against AUTHORITATIVE sources (ARM ARM, Cortex-X3 TRM/feature list, ARM developer docs). Use WebSearch/WebFetch. Flag any fabricated mnemonic, mis-attributed feature, or instruction the X3 does NOT have. Confirm what is real.'
  },
  {
    label: 'verify:adreno-vulkan',
    prompt: 'Adversarially VERIFY the Adreno 740 / Snapdragon 8 Gen 2 Vulkan claims — which extensions and features are ACTUALLY exposed by the Adreno 740 driver — against gpuinfo.org (the Vulkan hardware database) reports for Adreno 740 and the Qualcomm Adreno Vulkan developer guide. Use WebSearch/WebFetch. Flag any extension/feature claimed that Adreno 740 does NOT expose (e.g. fragment_shader_interlock is known absent). Confirm the real ones + their Vulkan version/core status.'
  }
];
function verifyThunk(l) {
  return function () {
    return agent(l.prompt + '\n\nCLAIMS TO CHECK: ' + claimDigest + '\n\n' + CONTEXT, { label: l.label, phase: 'Verify', schema: VERIFY_SCHEMA, agentType: 'general-purpose' });
  };
}
const rawVerify = await parallel(verifyLenses.map(verifyThunk));
const verify = rawVerify.filter(Boolean);

phase('Synthesize');
const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    topRecommendations: {
      type: 'array',
      items: {
        type: 'object', additionalProperties: false,
        properties: {
          rank: { type: 'number' },
          technique: { type: 'string' },
          domain: { type: 'string', 'enum': ['gpu-vulkan', 'cpu-simd', 'cpu-jit', 'memory-uma', 'scheduling'] },
          rationaleForXeniaThor: { type: 'string' },
          appliesToBottleneck: { type: 'boolean' },
          effort: { type: 'string', 'enum': ['small', 'medium', 'large', 'research'] },
          buildableBlind: { type: 'boolean' },
          source: { type: 'string' }
        },
        required: ['rank', 'technique', 'domain', 'rationaleForXeniaThor', 'appliesToBottleneck', 'effort']
      }
    },
    honestCaveats: { type: 'array', items: { type: 'string' } },
    docMarkdown: { type: 'string' }
  },
  required: ['topRecommendations', 'honestCaveats', 'docMarkdown']
};
const synth = await agent(
  'Synthesize ALL the research + verification into a PRIORITIZED, honestly-ranked report for xenia-thor. Apply the verification corrections (drop/flag anything the verifiers found unsupported — e.g. extensions Adreno 740 does not expose, features the Cortex-X3 lacks). Rank techniques by REAL impact on THIS emulator given the GPU-bound binning bottleneck (CPU ~84% idle on the headline scene): GPU/Vulkan/binning techniques that attack the proven bottleneck rank highest; CPU SIMD/JIT techniques are honestly secondary for that scene but call out where they DO help (guest VMX code, CPU shader interpreter / cull replay, format conversion + endian swap, XMA audio, memcpy/upload). Note which extend work already done in the fork (EDS, draw concatenation, UMA). Mark each recommendation buildableBlind (device-disconnected ~38h) or device-gated. Produce docMarkdown = a complete, well-structured markdown research doc body (with a sources section) suitable to save under docs/research/. Keep only SOURCED, verified-or-clearly-flagged claims.\n\nRESEARCH: ' + JSON.stringify(research).slice(0, 40000) + '\n\nVERIFICATION: ' + JSON.stringify(verify).slice(0, 12000) + '\n\n' + CONTEXT,
  { phase: 'Synthesize', schema: SYNTH_SCHEMA, agentType: 'general-purpose' }
);

return { research: research, verify: verify, synthesis: synth };
