# Vulkan Perf Counter Skeleton

Date: 2026-05-24 17:36 EDT

## Goal

Add the first mature-port style Vulkan performance counter skeleton for Thor
Blue Dragon captures without changing default runtime behavior.

This is a diagnostics lane only. Blue Dragon remains CPU/JIT-biased until a
route-clean capture proves a GPU wall.

## Patch

Files changed:

- `src/xenia/ui/vulkan/vulkan_diagnostic_counters.h`
- `src/xenia/ui/vulkan/vulkan_presenter.cc`
- `src/xenia/gpu/vulkan/vulkan_command_processor.cc`
- `src/xenia/gpu/vulkan/vulkan_pipeline_cache.cc`
- `android/android_studio_project/app/src/main/java/jp/xenia/emulator/EmulatorActivity.java`
- `tools/thor/thor_xenia_debug.ps1`
- `tools/thor/thor_vulkan_counter_surface_audit.ps1`

New default-off cvars:

- `vulkan_trace_perf_counters=false`
- `vulkan_trace_perf_counters_log_interval=60`

Thor script flags:

- `-VulkanTracePerfCounters true`
- `-VulkanTracePerfCountersLogInterval <N>`

Counter surfaces wired in this first skeleton:

- `IssueSwap` count.
- Graphics pipeline cache-hit count.
- Graphics pipeline create count, failure count, and host time.
- Command-processor queue submit count, command buffers, wait semaphores,
  failure count, and host time.
- Presenter acquire, submit, and present count/failure/time.
- Buffer/image barrier pushes.
- Barrier submit batches, groups, buffer/image totals, and force-end-render-pass
  count.
- Game render-pass begins and presenter render-pass begins.

The implementation lives in the already-built Vulkan presenter translation
unit because the generated Android makefile did not auto-include a brand-new
`.cc` file during `NativeCore`.

## Validation

Commands run:

```powershell
$null = [System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_xenia_debug.ps1',[ref]$null,[ref]$null)
$null = [System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_vulkan_counter_surface_audit.ps1',[ref]$null,[ref]$null)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_vulkan_counter_surface_audit.ps1
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy
```

Results:

- PowerShell scripts parsed.
- `thor_vulkan_counter_surface_audit.ps1` reports
  `recommended_next_patch=thor_route_capture_with_vulkan_trace_perf_counters`
  and `surfaces_missing=0`.
- `NativeCore` passed after moving the implementation into
  `vulkan_presenter.cc`.
- `FullDeploy` passed and installed the APK.

## Thor Captures

First validation capture:

- Capture: `scratch/thor-debug/20260524-173014-*`
- APK SHA: `4CE45FC678B907500647DE054312833F65832D60B64F7E49B760CC27E6BA2848`
- Cvars: `vulkan_trace_perf_counters=true`,
  `vulkan_trace_perf_counters_log_interval=20`
- Result: black-idled before visible route progress.
- Fatal marker search: clean.
- Counter issue: the first implementation logged snapshots at info level, so
  `LogLevel 1` did not expose counter rows.

Fix after first capture:

- Changed explicit counter snapshots from info-level to warning-level so the
  standard Thor `LogLevel 1` diagnostic lane sees them.

Second validation capture:

- Capture: `scratch/thor-debug/20260524-173312-*`
- APK SHA: `C1F35AF00D9CED398333E591326A4E69C7178CB20B27CCCB30C3D417BD6A91A6`
- Cvars: `vulkan_trace_perf_counters=true`,
  `vulkan_trace_perf_counters_log_interval=20`
- Screenshot: loading spinner, not visible opening/gameplay.
- Fatal marker search: clean.
- Counter rows landed in
  `scratch/thor-debug/20260524-173312-speed-logcat-filtered.txt`.

Last counter row observed:

```text
snapshots=1420 issue_swaps=710
pipeline_cache_hits=0 pipeline_creates=44 pipeline_create_failures=0 pipeline_create_us=6843145
queue_submits=710 queue_submit_failures=0 queue_submit_us=1105984 queue_cmd_buffers=710 queue_wait_semaphores=7
barrier_submits=96807 barrier_groups=96807 buffer_barriers=102445 image_barriers=91408 barrier_force_end_render_pass=68601
render_pass_begins=41008 presenter_render_pass_begins=711
present_acquires=711 present_acquire_failures=0 present_acquire_us=17193
present_submits=711 present_submit_failures=0 present_submit_us=51212 present_submit_cmd_buffers=711
presents=711 present_failures=0 present_us=151963
```

## Decision

This patch is instrumentation proof, not speed proof.

The counter skeleton is route-script visible, default-off, build/deploy tested,
and confirmed on Thor with real counter rows. The second capture did not reach
opening within 140 seconds, so do not use it to claim FPS or GPU bottleneck
status.

## Next

Run a route-stabilized control/counter sandwich before any GPU behavior patch:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -LiveCaptureSeconds 180 -PerfSampleSeconds "60,120,180" -LogLevel 1 -VulkanTracePerfCounters true -VulkanTracePerfCountersLogInterval 120
```

If the route reaches visible opening/gameplay and the counters remain clean,
compare queue/present/pipeline/barrier totals against Main Thread/GPU Commands.
Only then decide whether to expand counters into uploads/readbacks/descriptors
or keep the sprint on A64.
