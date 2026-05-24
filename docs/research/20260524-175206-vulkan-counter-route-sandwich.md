# Vulkan Counter Route Sandwich

Date: 2026-05-24 17:52 EDT

## Goal

Run the first route-clean Blue Dragon Thor control/counter comparison after
adding the default-off Vulkan performance counter skeleton.

This answers the user's "push more GPU" concern with on-device evidence instead
of a broad Vulkan rewrite.

## Captures

Control:

- Capture: `scratch/thor-debug/20260524-174125-*`
- Commit: `73c0d8059`
- APK SHA: `C1F35AF00D9CED398333E591326A4E69C7178CB20B27CCCB30C3D417BD6A91A6`
- Cvars: `vulkan_trace_perf_counters=false`
- Route: visible opening sky / dragon-wing screenshot.
- Fatal-marker search: clean.
- Final thread sample: Main Thread `103%`, GPU Commands `3.8%`,
  Draw Thread `3.8%`, XMA Decoder `11.5%`.

Counter run:

- Capture: `scratch/thor-debug/20260524-174443-*`
- Commit: `73c0d8059`
- APK SHA: `C1F35AF00D9CED398333E591326A4E69C7178CB20B27CCCB30C3D417BD6A91A6`
- Cvars: `vulkan_trace_perf_counters=true`,
  `vulkan_trace_perf_counters_log_interval=120`
- Route: matching visible opening sky / dragon-wing screenshot.
- Fatal-marker search: clean.
- Final thread sample: Main Thread `96.1%`, GPU Commands `7.6%`,
  Draw Thread `3.8%`, XMA Decoder `42.3%`.

Commands:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -LiveCaptureSeconds 180 -PerfSampleSeconds "60,120,180" -LogLevel 1 -VulkanTracePerfCounters false
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -LiveCaptureSeconds 180 -PerfSampleSeconds "60,120,180" -LogLevel 1 -VulkanTracePerfCounters true -VulkanTracePerfCountersLogInterval 120
```

## Final Counter Row

From `scratch/thor-debug/20260524-174443-speed-logcat-filtered.txt`:

```text
snapshots=1680 issue_swaps=840
pipeline_cache_hits=0 pipeline_creates=56 pipeline_create_failures=0 pipeline_create_us=10107505
queue_submits=840 queue_submit_failures=0 queue_submit_us=1496623 queue_cmd_buffers=840 queue_wait_semaphores=8
barrier_submits=115307 barrier_groups=115307 buffer_barriers=121138 image_barriers=110588 barrier_force_end_render_pass=81030
render_pass_begins=50458 presenter_render_pass_begins=841
present_acquires=841 present_acquire_failures=0 present_acquire_us=20310
present_submits=841 present_submit_failures=0 present_submit_us=1425436 present_submit_cmd_buffers=841
presents=841 present_failures=0 present_us=188391
```

## Interpretation

- The counter skeleton is route-clean with the current opening route.
- No fatal markers, present failures, queue submit failures, or pipeline create
  failures appeared.
- The route still looks CPU/JIT-heavy: Main Thread is around one full core, and
  GPU Commands remains far below a full core in both captures.
- Android `gfxinfo` still reports only the Java UI surface frames, not game FPS,
  so do not use it as Blue Dragon FPS evidence.
- The counter run emitted about `840` IssueSwap calls over the 180-second
  window, but IssueSwap rate is a diagnostic route/progress counter, not a
  sustained FPS proof.
- Pipeline creation time is visible but not yet isolated as a route-wide wall:
  `56` creates accumulated about `10.1s` host time across the run.
- Barrier/render-pass churn is high enough to justify better GPU diagnostics,
  but not enough to pivot away from A64 while the main thread remains the wall.

## Decision

Do not start RenderDoc-only FPS claims, Swappy/frame pacing, or a broad Vulkan
rewrite from this result.

The Vulkan counter lane did its job: it proved the route can carry low-overhead
GPU diagnostics and still points back to CPU/A64 as the primary Blue Dragon
speed wall.

## Next

Return the main sprint to A64 state-carrier/edge-variant work around the
`82282490 -> 82287788` path, or add a narrow second-stage GPU counter expansion
for descriptor churn, copies/uploads/readbacks, resolves, and completion waits
only if the next CPU-side route capture shows GPU Commands or present/submit
waits rising.
