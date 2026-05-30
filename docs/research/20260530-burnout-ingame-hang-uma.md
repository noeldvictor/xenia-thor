# Burnout movie hang vs UMA direct-write — CORRECTED: intermittent race, not a clean regression

Device: AYN Thor c3ca0370. master. 2026-05-30. All numbers below were read from
device this session (VdSwap counts via logcat, screenshots Read).

## Corrected conclusion
gpu_uma_direct_shared_memory (UMA direct-write shared memory) causes an
**INTERMITTENT** present-pipeline hang on Burnout, NOT a deterministic regression.
- UMA ON, run A & B: present wedged — VdSwap froze at ~1202/1203 for 700+s,
  unresponsive to injected input, GPU/Main threads sleeping.
- UMA ON, run C (this run, same launch args): did NOT hang — VdSwap climbed
  28865 -> 45647 (~60fps) and reached the SAVE/LOAD menu @59.4fps (screenshot
  burnout-uma-on-running.png), identical to UMA-off.
- UMA OFF: stable ~60fps every run.
=> intermittent = a CPU/GPU race, consistent across runs only in being unstable.
My earlier commit f0811797c ("SOLVED ... regression") OVER-CLAIMED determinism;
this corrects it. UMA-off is the stable config; UMA-on sometimes works.

## cvar default
DEFINE_bool(gpu_uma_direct_shared_memory, false, ...) in vulkan_shared_memory.cc:32.
So UMA direct-write is ALREADY OFF by default. "Shipping off" = the status quo
default + the explicit gpu_uma_direct_shared_memory=false in the Burnout profile
(tools/thor/thor_profiles.ps1). Nothing renders UMA on unless a launch passes
--ez gpu_uma_direct_shared_memory true.

## Two earlier WRONG root-cause theories (retracted)
1. "infinite EOF read-loop on BG1_N.xmv is the cause" — RED HERRING. Present died
   ~74s BEFORE that loop; an identical past-EOF over-read on the prior movie
   CRRW_N.xmv was survived. Over-reading past EOF is normal demuxer read-ahead.
2. file-size/truncation bug — NO. disc_image_file.cc:26 returns EOF only at the
   real entry_->size(); pinned read positions ARE the true file ends.
Also retracted (failed Edits, never committed): "missing MakeRangeValid" and
"barrier misses INDEX_READ" — the direct path at vulkan_shared_memory.cc:543-578
already does MakeRangeValid + flush + a barrier covering
VERTEX_INPUT|INDEX_READ|SHADER_READ. The gap is NOT barrier coverage.

## Actual mechanism (subagent-mapped, evidence-cited)
UploadRangesDirect (vulkan_shared_memory.cc:521) does a CPU std::memcpy straight
into the persistently-mapped HOST_VISIBLE|DEVICE_LOCAL buffer at command-RECORDING
time (line 548). If a PRIOR, still-in-flight submission's draws are reading those
same shared-memory pages, the memcpy overwrites data mid-read -> corruption ->
present intermittently wedges. The STAGING path avoids this: its upload-pool pages
are submission-tagged (GraphicsUploadBufferPool::Page::last_submission_index_) and
only reclaimed once GetCompletedSubmission() >= that index
(graphics_upload_buffer_pool.cc:21-34). The direct path has no such guard.

Submission model (single GPU worker thread, so no atomics needed):
- GetCurrentSubmission() vulkan_command_processor.h:160 (upcoming index)
- GetCompletedSubmission() vulkan_command_processor.h:163 (last GPU-finished)
- AwaitSubmissionAndUpdateCompleted(idx) gpu_completion_timeline.h:55-62 (blocking wait)
- RequestRange->UploadRanges runs during IssueDraw within an OPEN submission
  (vulkan_command_processor.cc:3548/3570), BEFORE that submission is queued at
  EndSubmission (:4331). So the danger is only PRIOR submissions still in flight.

## Planned fix (NOT yet implemented — delicate, build/deploy loop is slow)
Mirror the staging pool's submission-tracking for the direct path:
- Add per-system-page last-read-submission tracking (record GetCurrentSubmission()
  when pages are marked read in Use(kRead)).
- In UploadRangesDirect, before the memcpy of a page, if its last-read submission
  > GetCompletedSubmission(), AwaitSubmissionAndUpdateCompleted(that submission)
  first. Then memcpy is race-free.
- EDGE CASE to handle: never wait on the CURRENTLY OPEN submission (would deadlock
  - it hasn't been queued yet). Only wait on submissions strictly < current.
This keeps the UMA perf win (no staging copy) while making it safe. Until verified,
keep default OFF.

## ROOT CAUSE — CONFIRMED HARDWARE GPU HANG (dmesg, 2026-05-30)
The freeze is a REAL Adreno GPU hang (TDR), NOT a CPU-logic deadlock. Kernel log
during a UMA-on Burnout run (proc pid 13184, ctx_type VK):
```
adreno-gen7-gmu 3d68000.qcom,gmu: MISC: GPU hang detected
kgsl kgsl-3d0: or.github.debug[13184]: ctx 15 ctx_type VK ts 937 status 00E704E7
              rb 00e6/025e ib1 ...6AF11324 ib2 ...6AE48F04
adreno-gen7-gmu 3d68000.qcom,gmu: Suspended GMU
```
So the GPU itself faults/hangs while consuming the UMA direct-write buffer, ~50% of
runs. The chain to the app freeze:
1. UMA direct path -> GPU hangs (TDR) drawing from the persistently-mapped
   HOST_VISIBLE|DEVICE_LOCAL shared-memory buffer (intermittent = torn read /
   coherency / access-pattern race at the hardware level).
2. That submission's fence NEVER signals (GPU dead).
3. Every fence wait is vkWaitForFences(UINT64_MAX) with NO device-lost recovery on
   the wait path (vulkan_gpu_completion_timeline.cc AwaitSubmissionImpl) -> the CPU
   threads (GPU Commands / GPU VSync / Main XThread) hang forever too.

CORRECTION: a subagent analysis blamed my AwaitSubmissionCompletion read-guard
(vulkan_shared_memory.cc:561) as "THE culprit." That is WRONG - the same ~1190
hang happened BEFORE that code (commit 4beaeca65 added it; hangs predate it). The
read-guard is at most an additional infinite-wait SITE, not the trigger. The
trigger is the GPU TDR.

## FIX DIRECTIONS (two layers, neither yet implemented)
A) DEFENSIVE (stops the permanent freeze, doesn't make UMA correct): give the
   fence waits a FINITE timeout + handle VK_ERROR_DEVICE_LOST / timeout (mark device
   lost, surface it) instead of vkWaitForFences(UINT64_MAX). Then a GPU hang ends the
   session cleanly instead of wedging forever. Applies beyond UMA.
B) ROOT (make UMA safe so the GPU stops hanging): the GPU faults reading the
   512MB HOST_VISIBLE|DEVICE_LOCAL buffer. Hypotheses to test, in order:
   - Heap-budget / BAR window: a 512MB DEVICE_LOCAL+HOST_VISIBLE allocation may
     exceed the Adreno host-visible-device-local budget; check
     VkPhysicalDeviceMemoryBudget vs 512MB; if over budget, GPU access can fault.
   - Coherency/flush: confirm nonCoherentAtomSize handling + that the host write is
     fully flushed AND made available before the GPU read (the barrier covers
     access masks but verify HOST_WRITE visibility timing vs the actual submit).
   - Access-pattern: CPU writing pages the GPU may touch in the SAME frame (intra-
     submission), which the submission-level guard does not cover.
   Validate each on device (UMA-on, watch dmesg for "GPU hang detected").

## Net for "get the speed"
At menus/movies both UMA on and off are vsync-capped ~60fps, so the user-visible
speed there is NOT gated on UMA. UMA's win is throughput on heavy guest-memory
traffic (the staging-copy elimination), which matters for in-gameplay scenes not
yet reached. Stable path today = UMA off, ~60fps to the SAVE/LOAD menu (verified).
