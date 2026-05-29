# 2026-05-29 19:18 EDT - Project Sylpheed VdSwap/GPU Presenter Probe Prep

## Context

The prior slice target was to add default-off instrumentation for a usable next packet:

- VdSwap enter/exit rows keyed by caller + status
- Compact presenter/GPU state snapshot around issue path
- Preserve existing physical-free and wait evidence unchanged

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_project_sylpheed_late_pc_wait_map.ps1 -PacketDir 'scratch\thor-debug\remote-debug-20260529-113736' -OutPath 'scratch\thor-debug\20260529-151000-sylpheed-presenter-gpu-stop-join.txt'
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
```

## Findings

- C++ compile now succeeds after adding explicit dependency on
  `xenia/kernel/xboxkrnl/xboxkrnl_error.h` in `xboxkrnl_video.cc`.
- Parser script still runs against the existing proof packet and confirms the same
  prior stop context (`last_vdswap_has_no_same_caller_status_and_no_later_presenter_gpu_rows`).
- The old packet did not include fresh `gpu_trace_vd_swap` rows, so this slice is
  considered preparatory instrumentation rather than new behavioral evidence.
- New traces are in code behind `gpu_trace_vd_swap` (default OFF):
  - VdSwap enter with full arg snapshot + caller
  - Parse-friendly `xeRtlNtStatusToDosError` rows for caller/status (`... 103` on
    entry, `0` on exit)
  - `VdSwap finished` trailer
  - Compact presenter state line in `VulkanCommandProcessor::IssueSwap` + forced
    compact perf snapshot under tag `vdswap`

## Next Step

Capture a fresh real-library packet with `gpu_trace_vd_swap` enabled and full
`screenshot+logcat` capture before handling the issue on first pass.