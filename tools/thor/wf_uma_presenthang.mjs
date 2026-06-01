export const meta = {
  name: 'deep-thor-uma-present-hang-rootcause',
  description: 'Root-cause WHY gpu_uma_direct_shared_memory=true still present-HANGS Blue Dragon (B74: black, VdSwap=0, CPU spinning, GPU idle) EVEN WITH the smart-sync RAW + WAW upload-race guards - i.e. the hang is NOT the upload race. Find the real cause (present/swapchain or submission/fence wedge with the 512MB persistently-mapped buffer) + a candidate gated fix. Device disconnected - design/read only, validate later.',
  phases: [
    { title: 'Understand', detail: 'parallel readers map the UMA-direct path, present/swap, submission/fence, CP-thread spin sources, and the 512MB buffer alloc' },
    { title: 'Rootcause', detail: 'reason the most likely non-upload-race cause of the CPU-spin/GPU-idle/no-present hang' },
    { title: 'Verify', detail: 'adversarial verifiers attack the top hypotheses + the proposed fix' },
    { title: 'Synthesize', detail: 'root-cause verdict + a candidate gated, build-verifiable fix (device-validated later)' }
  ]
};

const FACTS = [
  'PROJECT: xenia-thor (Xbox360 emulator fork) -> AYN Thor / Adreno-740 (TBDR) / Vulkan. Device DISCONNECTED ~38h: design + code-read only, NO device, NO perf numbers. Cite file:line from the ACTUAL repo. Do not invent.',
  'BUG: with gpu_uma_direct_shared_memory=true (back the shared-memory buffer with a 512MB non-sparse HOST_VISIBLE|DEVICE_LOCAL persistently-mapped buffer and CPU-memcpy guest pages straight in), the emulator PRESENT-HANGS. B74 (device-verified 2026-05-31, Blue Dragon, evidence 20260531-230208): black screen, VdSwap=0 over the window AND a live 6s recheck, process ALIVE, GPU busy 0% (idle), SoC temp climbed 59->72C => CPU SPINNING with the GPU idle. Same family as the documented Burnout UMA present-hang.',
  'KEY: the smart-sync guards ALREADY address the upload race. 74a5d57b3 gpu_uma_smart_sync (default true): before a direct in-place write, wait only for the last prior CLOSED submission that READ the buffer (RAW guard). 4918d1b8d gpu_uma_smart_sync_writes (default true): also wait for the last prior WRITER submission (WAW guard) via uma_last_write_submission_. Both via command_processor_.AwaitSubmissionCompletion(). B74 shows UMA-ON STILL HANGS Blue Dragon WITH these guards on => THE HANG IS NOT THE UPLOAD RACE. Find the OTHER cause.',
  'SIGNATURE ANALYSIS: CPU spinning (72C) + GPU idle (0% busy) + process alive + zero VdSwap (no present) + black. This points at a CPU-SIDE spin or a wait-that-never-progresses on the command-processor (CP) thread or guest draw thread, NOT a GPU-side GMU TDR (which would show as a GPU hang / the older Burnout dmesg adreno-gen7-gmu GPU hang). Leading suspects to investigate: (a) AwaitSubmissionCompletion() called from the direct-write path BLOCKS/SPINS waiting for a submission that never completes (because the CP thread is the one that would submit/advance it -> self-deadlock, or it busy-polls); (b) the present/IssueSwap path waits on a fence/semaphore tied to the shared buffer that never signals when UMA-direct is on; (c) the 512MB non-sparse persistently-mapped allocation changes submission/flush/coherency behavior so the swapchain present wedges; (d) a guest fence/draw-wait spin (the arm64_blue_dragon_draw_wait_fastpath / guest Draw Thread) that only manifests when UMA-direct changes timing.',
  'RELEVANT FILES: src/xenia/gpu/vulkan/vulkan_shared_memory.cc (UploadRangesDirect, smart-sync, the buffer alloc + persistent map, the flush), vulkan_command_processor.cc (AwaitSubmissionCompletion / CheckSubmissionCompletionAndDeviceLoss, EndSubmission, IssueSwap/present, submission open/close + fences), the presenter (src/xenia/ui/vulkan/ swapchain/present), and how GetCompletedSubmission advances (the fence the GPU signals per submission).',
  'CONSTRAINT: any candidate fix must be gated behind a cvar (or refine an existing gpu_uma_* cvar), default-safe (UMA stays off by default), build-verifiable blind, and device-validated LATER. Prefer a fix that is OBVIOUSLY correct from the code (e.g. removing a self-wait, fixing a fence wait) over a speculative one. If the cause cannot be confidently localized blind, say so and propose the cheapest on-device diagnostic to run on reconnect (e.g. a targeted log at the suspected wait/spin).',
  'PRIOR (memory burnout-uma-present-hang-regression): earlier leads = guest blocked on a GPU fence/event that never signals at a specific submission boundary (~50% of launches for Burnout), and present/swapchain interaction with the 512MB buffer. A prior single-scalar read-guard (4beaeca65) was DEAD CODE; replaced by gpu_uma_serialize_before_write (full drain diagnostic) then smart-sync. Coherency + size were ruled OUT by device evidence. The dmesg GMU-TDR was seen for Burnout; B74 for Blue Dragon shows CPU-spin/GPU-idle (possibly a different proximate cause - do not assume identical).'
].join('\n');

const READ_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    subsystem: { type: 'string' },
    hotspots: { type: 'array', items: { type: 'object', additionalProperties: false, properties: {
      file: { type: 'string' }, line: { type: 'string' }, what: { type: 'string' }
    }, required: ['file', 'what'] } },
    keyFindings: { type: 'array', items: { type: 'string' } },
    hangSuspicions: { type: 'array', items: { type: 'string' } }
  },
  required: ['subsystem', 'keyFindings']
};

const readers = [
  { label: 'read:await-submission', prompt: 'Read vulkan_command_processor.cc AwaitSubmissionCompletion + CheckSubmissionCompletionAndDeviceLoss (+ GetCompletedSubmission / GetCurrentSubmission / the per-submission fence). Question: can a call to AwaitSubmissionCompletion() from the UMA direct-write path (vulkan_shared_memory.cc UploadRangesDirect smart-sync) BLOCK or BUSY-SPIN forever - e.g. waiting on a submission that never completes because the SAME (CP) thread would have to submit/advance it, or because no further GPU work is submitted so the completed-submission counter never reaches the target? Does it sleep or busy-poll (CPU spin)? Is there a path where UploadRangesDirect runs on a thread that is itself responsible for submitting the work it waits on? file:line.' },
  { label: 'read:present-swap', prompt: 'Read the present path: vulkan_command_processor.cc IssueSwap + the presenter/swapchain (src/xenia/ui/vulkan/). How does present acquire/submit/wait (fences, semaphores), and does it depend on the shared-memory buffer or a submission that the UMA-direct path could stall? When UMA-direct is on, is there a fence/semaphore the present waits on that may never signal (because the CP thread is stuck in a UMA direct-write await)? file:line.' },
  { label: 'read:uma-direct-write', prompt: 'Read vulkan_shared_memory.cc UploadRangesDirect fully (the smart-sync RAW+WAW wait, the memcpy, the flush, the buffer barrier) + when/where it is called in the draw path. On which thread does it run? How often (per draw range upload)? Could its AwaitSubmissionCompletion wait, called frequently on the CP thread, serialize the CP thread so it never submits/presents (CPU spins issuing waits, GPU idle)? Is the wait target ever the CURRENT open submission (which can never complete)? file:line.' },
  { label: 'read:submission-lifecycle', prompt: 'Read the submission open/close/submit lifecycle in vulkan_command_processor.cc (BeginSubmission/EndSubmission, submission_open_, the fence signaled per submit, how GetCompletedSubmission is updated - polled vs callback). When does GPU work actually get vkQueueSubmit-ted, and what advances the completed counter? Identify any way UMA-direct could prevent a submit from happening (so the awaited submission never completes) -> CP thread waits/spins -> no present. file:line.' },
  { label: 'read:buffer-alloc-coherency', prompt: 'Read the gpu_uma_direct_shared_memory buffer allocation + persistent map + flush in vulkan_shared_memory.cc (the 512MB non-sparse HOST_VISIBLE|DEVICE_LOCAL path, buffer_host_coherent_, the vkFlushMappedMemoryRanges / gpu_uma_strong_coherency). Could the 512MB non-sparse alloc or the whole-buffer flush on every direct write be so slow/serializing that it manifests as a CPU spin + stalled present (vs a true deadlock)? Any device-lost / fence-timeout path that would leave the CP thread spinning? file:line.' }
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
function digestOf(f) { return { sub: f.subsystem, find: f.keyFindings, susp: f.hangSuspicions || [] }; }
const digest = JSON.stringify(findings.map(digestOf)).slice(0, 18000);

phase('Rootcause');
const RC_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    mostLikelyCause: { type: 'string' },
    mechanism: { type: 'string' },
    whyNotUploadRace: { type: 'string' },
    evidenceCited: { type: 'array', items: { type: 'string' } },
    confidence: { type: 'string' },
    candidateFix: { type: 'string' },
    alternativeCauses: { type: 'array', items: { type: 'string' } }
  },
  required: ['mostLikelyCause', 'mechanism', 'candidateFix', 'confidence']
};
const rc = await agent('Reason out the MOST LIKELY cause of the UMA-on present-hang (CPU spinning + GPU idle + no present + black, B74) given the upload race is ALREADY guarded. Anchor in the findings + file:line. Explain the mechanism, why it is not the upload race, and a CANDIDATE GATED FIX (obviously-correct preferred). Mark confidence honestly.\nFINDINGS: ' + digest + '\n' + FACTS, { phase: 'Rootcause', schema: RC_SCHEMA });

phase('Verify');
const VERDICT_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    lens: { type: 'string' },
    holdsUp: { type: 'boolean' },
    problems: { type: 'array', items: { type: 'string' } },
    fixIsSafeBlind: { type: 'boolean' },
    verdict: { type: 'string', 'enum': ['fix-now-gated', 'needs-device-diagnostic', 'wrong-cause'] },
    notes: { type: 'string' }
  },
  required: ['lens', 'holdsUp', 'verdict']
};
const lenses = [
  { label: 'verify:deadlock-logic', prompt: 'Adversarially check the proposed root-cause + fix on the SUBMISSION/FENCE/await logic: does the proposed mechanism actually produce CPU-spin + GPU-idle + no-present? Could the fix deadlock differently, or break the upload-race guard it was protecting? Is AwaitSubmissionCompletion truly the spin, or is that wrong? Default skeptical.' },
  { label: 'verify:present-path', prompt: 'Adversarially check from the PRESENT/swapchain angle: is the hang really in the present path, or upstream (CP thread never reaching present)? Does the proposed fix actually let present proceed? Would it regress the non-UMA (default) path? Default skeptical.' },
  { label: 'verify:blind-safety', prompt: 'Adversarially check whether the candidate fix is SAFE TO BUILD BLIND + gated: is it obviously correct from the code, or speculative (needs device to confirm)? Does it keep UMA default-off and the default path bit-identical? If speculative, what is the cheapest on-device diagnostic to localize the spin on reconnect? Default skeptical; prefer recommending a device diagnostic over a speculative blind fix.' }
];
function verifyThunk(l) {
  return function () {
    return agent(l.prompt + '\n\nROOT-CAUSE: ' + JSON.stringify(rc) + '\nFINDINGS: ' + digest + '\n' + FACTS, { label: l.label, phase: 'Verify', schema: VERDICT_SCHEMA });
  };
}
const rawVerdicts = await parallel(lenses.map(verifyThunk));
const verdicts = rawVerdicts.filter(Boolean);

phase('Synthesize');
const SYNTH_SCHEMA = {
  type: 'object', additionalProperties: false,
  properties: {
    rootCauseVerdict: { type: 'string' },
    confidence: { type: 'string' },
    action: { type: 'string', 'enum': ['build-gated-fix-now', 'build-fix-plus-device-diagnostic', 'device-diagnostic-only'] },
    firstStep: { type: 'object', additionalProperties: false, properties: {
      title: { type: 'string' }, file: { type: 'string' }, change: { type: 'string' }, cvar: { type: 'string' }, buildVerify: { type: 'string' }, deviceValidation: { type: 'string' }
    }, required: ['title', 'change'] },
    openRisks: { type: 'array', items: { type: 'string' } },
    honestUncertainties: { type: 'array', items: { type: 'string' } }
  },
  required: ['rootCauseVerdict', 'confidence', 'action', 'firstStep']
};
const synth = await agent('Synthesize: the most-supported root cause of the UMA-on present-hang, and the action - a gated build-verifiable fix now (if obviously correct), a fix PLUS an on-device diagnostic, or a device-diagnostic-only step (if the cause cannot be confidently localized blind). Give the precise FIRST step (file, change, cvar, how to build-verify, how to validate on device in 38h). Be honest about confidence + what stays unknown until the device.\nROOT-CAUSE: ' + JSON.stringify(rc) + '\nVERDICTS: ' + JSON.stringify(verdicts) + '\n' + FACTS, { phase: 'Synthesize', schema: SYNTH_SCHEMA });

return { rootcause: rc, verdicts: verdicts, synthesis: synth };
