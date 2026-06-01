export const meta = {
  name: 'flagm-lostodyssey-design-verify',
  description: 'Design + adversarially VERIFY a bit-exact, gated FEAT_FlagM (RMIF/SETF8/SETF16/CFINV) lowering for guest PPC carry/CR/XER flag materialization in the xenia-thor a64 JIT, targeting CPU-bound Lost Odyssey. Device disconnected ~38h so correctness CANNOT be tested - equivalence must be PROVEN, not measured. Verify exact ARM FlagM semantics + whether OPCODE_ADD_CARRY carry-in is guaranteed 0/1, then prove the proposed lowering is bit-identical to the current one and safe to ship gated default-off.',
  phases: [
    { title: 'Research', detail: 'exact FlagM semantics from ARM ARM + repo feasibility (emitter/HWCAP API, IR carry-operand value range, target sites)' },
    { title: 'Verify', detail: 'adversarially prove bit-exact equivalence of the proposed lowering + safety of gating' },
    { title: 'Synthesize', detail: 'precise gated implementation plan + explicit do-NOT-apply list' }
  ]
};

const CONTEXT = [
  'PROJECT: xenia-thor, Xbox360 emulator. Guest CPU = PowerPC Xenon (big-endian, 3 cores). Host = ARM64 (Snapdragon 8 Gen 2 / Cortex-X3). The a64 backend (src/xenia/cpu/backend/a64/) JITs guest PPC IR -> ARM64 via the xbyak_aarch64 assembler. TARGET: CPU-bound title Lost Odyssey (the FlagM win helps dense guest conditional/flag-setting arithmetic; it is IRRELEVANT to the GPU-bound Blue Dragon scene where the CPU is ~84% idle).',
  'HARD CONSTRAINT: the device is DISCONNECTED ~38h, so functional correctness CANNOT be tested. The build only proves it COMPILES. Therefore any FlagM lowering MUST be PROVEN bit-exact-equivalent to the current lowering for ALL inputs, and gated behind a NEW default-OFF cvar + runtime FEAT_FlagM detection, so the default path stays bit-identical and the change is a device A/B later. A wrong carry/flag lowering = silent wrong guest execution. Equivalence-by-proof is the ONLY safety net.',
  'FEASIBILITY (already confirmed in-repo): FlagM is currently UNUSED (no rmif/setf8/cfinv anywhere). The xbyak_aarch64 assembler EXPOSES it: third_party/xbyak_aarch64/.../xbyak_aarch64_mnemonic.h has `void CodeGenerator::rmif(const XReg &xn, const uint32_t sh, const uint32_t mask)`, `void setf8(const WReg &wn)`, `void setf16(const WReg &wn)`, `void cfinv()`. CONFIRM the A64Emitter (a64_emitter.h) exposes these (it derives from / wraps the xbyak CodeGenerator) - if not, note what wrapper is needed.',
  'FEATURE-DETECTION PATTERN (mirror this exactly): src/xenia/base/platform_arm64.cc has GetFeatureFlags() + InitFeatureFlags() using `Xbyak_aarch64::util::Cpu cpu_;` and a TEST_EMIT_FEATURE(emit, ext) macro: `if ((cvars::a64_extension_mask & emit)==emit) feature_flags_ |= (cpu_.has(ext)?emit:0);`. Existing flags: kA64EmitLSE (XBYAK_AARCH64_HWCAP_ATOMIC), kA64FZFlushesInputs. The a64_extension_mask cvar (default -1 = detect all) gates DETECTION. FIND where the kA64Emit* enum is defined (a64_backend.h?) and the exact XBYAK_AARCH64_HWCAP_* constant name for FlagM (XBYAK_AARCH64_HWCAP_FLAGM?). NOTE: detection set by default (-1) is fine, but the USE of FlagM must be gated behind a SEPARATE new default-FALSE cvar (e.g. arm64_flagm_fastpath) AND require (GetFeatureFlags() & kA64EmitFlagM) - so default behavior is unchanged and unvalidated FlagM never runs unless explicitly enabled.',
  'PRIMARY TARGET SITE - OPCODE_ADD_CARRY (PPC adde/addc/subfe multi-word arithmetic). CURRENT lowering (src/xenia/cpu/backend/a64/a64_sequences.cc): ADD_CARRY_I32 (lines ~2119-2142): mov w0,src1; add w0,w0,src2; if src3 const && nonzero add w0,#1 ELSE `add w0, w0, src3` (adds the FULL src3 value); mov dest,w0. ADD_CARRY_I64 (~2143-2167): same but for x regs, and for non-const carry it does `mov w1,src3; uxtb w1,w1; add x0,x0,x1` (adds the full zero-extended BYTE). PROPOSED FlagM lowering: set the host C flag from the carry-in then ADC: e.g. `rmif <carry>, #63, #2` (claim: sets PSTATE.C = carry bit0) then `adc dest, src1, src2`.',
  'CRITICAL EQUIVALENCE QUESTION the verifiers MUST settle: the current code ADDS THE FULL src3 (not just bit0). The ADC lowering adds only the C flag (0 or 1). These are equivalent ONLY IF OPCODE_ADD_CARRY guarantees its carry-in operand (src3, an I8) is always in {0,1}. DETERMINE from the IR/PPC frontend (src/xenia/cpu/ppc/ and src/xenia/cpu/hir/ and the OPCODE_ADD_CARRY definition + how ppc_emit_alu emits adde/addc/subfe) whether the carry-in is provably 0/1. If it is NOT provably 0/1, the ADC lowering is WRONG and must be rejected (or guarded with a mask that defeats the win). This is the load-bearing correctness fact.',
  'SECONDARY TARGET SITES to evaluate (only if provably-equivalent): SETF8/SETF16 for materializing N/Z/V from narrow (8/16-bit) guest comparison/arithmetic results; RMIF for extracting/inserting individual guest CR (condition register) or XER (CA carry / OV overflow) bits in one op instead of multi-op shift/mask sequences; CFINV for the PPC subtract-borrow vs ARM carry polarity difference (PPC subfc borrow = NOT(ARM carry-out of a-b); CFINV inverts C in one op). For each, find the CURRENT multi-op lowering in a64_sequences.cc / a64_seq_control.cc and judge whether a FlagM rewrite is provably equivalent. Be conservative: if equivalence is not airtight, put it on the do-NOT-apply list.',
  'OUTPUT RULES: cite the ARM Architecture Reference Manual (or ARM developer docs) for the EXACT semantics of RMIF (rotate-right by shift, then for mask bits insert tmp[3:0] into NZCV where mask bit3->N tmp[3], bit2->Z tmp[2], bit1->C tmp[1], bit0->V tmp[0] - VERIFY this exact bit/rotate mapping, do not trust it), SETF8, SETF16, CFINV. Use WebSearch/WebFetch. Mark anything unverified. The goal is a SHIPPABLE, gated, PROVEN-equivalent first FlagM step (ADD_CARRY if it passes the 0/1 proof) + a clear list of what is safe to extend and what is NOT.'
].join('\n');

const RESEARCH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    area: { type: 'string' },
    findings: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      claim: { type: 'string' }, detail: { type: 'string' }, source: { type: 'string' },
      confidence: { type: 'string', 'enum': ['verified', 'likely', 'unverified'] }
    }, required: ['claim', 'detail', 'confidence'] } },
    rmifExactSemantics: { type: 'string' },
    carryInIsProvablyZeroOrOne: { type: 'string', 'enum': ['yes', 'no', 'unknown'] },
    carryInEvidence: { type: 'string' },
    emitterExposesFlagM: { type: 'string' },
    featureEnumLocation: { type: 'string' },
    sources: { type: 'array', items: { type: 'string' } }
  },
  required: ['area', 'findings', 'sources']
};

phase('Research');
const researchAgents = [
  { label: 'research:flagm-semantics', prompt: 'Use WebSearch/WebFetch on the ARM Architecture Reference Manual / ARM developer docs to establish the EXACT semantics of the FEAT_FlagM/FlagM2 instructions: RMIF (the rotate amount direction, and the precise mask-bit -> NZCV-flag and source-bit mapping), SETF8, SETF16, CFINV. Specifically PROVE OR REFUTE the claim that `rmif Xn, #63, #2` sets PSTATE.C = Xn bit 0 (and clears nothing else). State the exact NZCV bit numbering and the rmif pseudocode. Cite the ARM ARM section. Fill rmifExactSemantics precisely.' },
  { label: 'research:repo-ir-carry', prompt: 'Read the xenia-thor repo to settle: (1) does OPCODE_ADD_CARRY guarantee its carry-in operand is in {0,1}? Inspect the OPCODE_ADD_CARRY definition (src/xenia/cpu/hir/) and every emitter of it - especially the PPC frontend adde/addc/addic/subfe lowering in src/xenia/cpu/ppc/ppc_emit_alu.cc (how it produces the carry-in: is it a 0/1 boolean from XER[CA] / a DID_CARRY / a comparison, or could it be an arbitrary value?). Set carryInIsProvablyZeroOrOne + carryInEvidence with file:line. (2) Does A64Emitter (a64_emitter.h) expose rmif/setf8/setf16/cfinv (inherited from xbyak CodeGenerator)? (3) Where is the kA64Emit* feature enum defined and what is the exact XBYAK_AARCH64_HWCAP_* constant for FlagM (grep third_party/xbyak_aarch64 util)? (4) List the CURRENT multi-op lowerings for CR/XER/overflow/borrow materialization that FlagM could shorten (a64_sequences.cc, a64_seq_control.cc), with file:line.' }
];
const research = (await parallel(researchAgents.map(function (a) {
  return function () { return agent(a.prompt + '\n\n' + CONTEXT, { label: a.label, phase: 'Research', schema: RESEARCH_SCHEMA, agentType: 'general-purpose' }); };
}))).filter(Boolean);
log('Research: ' + research.length + '/' + researchAgents.length);
const researchDigest = JSON.stringify(research).slice(0, 24000);

phase('Verify');
const VERIFY_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    lens: { type: 'string' },
    addCarryLoweringIsBitExact: { type: 'string', 'enum': ['proven-equivalent', 'not-equivalent', 'equivalent-only-if-carry-0-1', 'uncertain'] },
    proofOrCounterexample: { type: 'string' },
    safeToShipGatedDefaultOff: { type: 'boolean' },
    problems: { type: 'array', items: { type: 'string' } },
    doNotApply: { type: 'array', items: { type: 'string' } }
  },
  required: ['lens', 'addCarryLoweringIsBitExact', 'proofOrCounterexample', 'safeToShipGatedDefaultOff']
};
const verifyAgents = [
  { label: 'verify:equivalence', prompt: 'Adversarially attack the proposed ADD_CARRY FlagM lowering (rmif <carry>,#63,#2 ; adc dest,src1,src2) vs the current full-value-add lowering. Try to find ANY input where they DIFFER (especially: carry-in operand values other than 0/1; the I64 uxtb full-byte add; constant-fold paths; dest==src aliasing; 32- vs 64-bit). Conclude addCarryLoweringIsBitExact with a proof or a concrete counterexample. If it is only equivalent when carry-in is 0/1, say so and demand the research-established proof that it is. Default to NOT-equivalent if the carry-in 0/1 guarantee is not airtight.' },
  { label: 'verify:safety-gating', prompt: 'Adversarially check the SAFETY of shipping this blind: (1) is the default path provably bit-identical (gated behind a NEW default-false cvar AND require GetFeatureFlags()&kA64EmitFlagM)? (2) is the runtime FlagM detection correct (HWCAP constant, a64_extension_mask interaction)? (3) does RMIF clobber any flags the surrounding sequence relies on (does ADD_CARRY run in a context where NZCV must be preserved across it)? (4) any RMIF/SETF semantics still unverified? List doNotApply for any secondary site (CR/overflow/borrow/SETF) whose equivalence is not airtight. Prefer rejecting over shipping a guess.' }
];
const verify = (await parallel(verifyAgents.map(function (a) {
  return function () { return agent(a.prompt + '\n\nRESEARCH FINDINGS: ' + researchDigest + '\n\n' + CONTEXT, { label: a.label, phase: 'Verify', schema: VERIFY_SCHEMA, agentType: 'general-purpose' }); };
}))).filter(Boolean);

phase('Synthesize');
const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    goNoGo: { type: 'string', 'enum': ['ship-add-carry-flagm', 'ship-other-site-only', 'do-not-ship-blind'] },
    confidence: { type: 'string' },
    implementationPlan: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      step: { type: 'string' }, file: { type: 'string' }, change: { type: 'string' }
    }, required: ['step', 'change'] } },
    cvarName: { type: 'string' },
    featureFlagName: { type: 'string' },
    exactEmitSequence: { type: 'string' },
    doNotApply: { type: 'array', items: { type: 'string' } },
    deviceValidationOnReconnect: { type: 'string' },
    honestCaveats: { type: 'array', items: { type: 'string' } }
  },
  required: ['goNoGo', 'confidence', 'implementationPlan', 'exactEmitSequence', 'deviceValidationOnReconnect']
};
const synth = await agent('Synthesize a SHIPPABLE, gated, PROVEN-equivalent FlagM implementation plan for the a64 JIT, targeting Lost Odyssey. Only recommend shipping what the verifiers PROVED bit-exact-equivalent. Give: the exact files+changes (feature-enum add kA64EmitFlagM, platform_arm64.cc detection mirroring kA64EmitLSE, the new default-false cvar, the exact gated emit sequence for ADD_CARRY_I32/I64 with the fallback), the exact RMIF/ADC instruction sequence, the do-NOT-apply list, and how to validate on Lost Odyssey on reconnect (what to compare; remember the CPU is the lever there, not the GPU). If the carry-in 0/1 guarantee was NOT proven, set goNoGo=do-not-ship-blind and explain. Be honest: this is unprofiled (no Lost Odyssey profile, device gone) and unvalidated; the win is a few instructions per carry op and only matters if adde/addc is hot.\n\nRESEARCH: ' + researchDigest + '\n\nVERIFY: ' + JSON.stringify(verify).slice(0, 12000) + '\n\n' + CONTEXT, { phase: 'Synthesize', schema: SYNTH_SCHEMA, agentType: 'general-purpose' });

return { research: research, verify: verify, synthesis: synth };
