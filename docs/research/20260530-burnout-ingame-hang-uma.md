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

## Net for "get the speed"
At menus/movies both UMA on and off are vsync-capped ~60fps, so the user-visible
speed there is NOT gated on UMA. UMA's win is throughput on heavy guest-memory
traffic (the staging-copy elimination), which matters for in-gameplay scenes not
yet reached. Stable path today = UMA off, ~60fps to the SAVE/LOAD menu (verified).
