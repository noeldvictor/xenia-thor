# Android Status Report PID Scoping

## Context

The latest Project Sylpheed game-library repro packet
`scratch/thor-debug/20260527-205200-sylpheed-library-new-game-repro` was
initially classified as `project_sylpheed_heap_native_abort`. Rechecking the
packet showed that `after-new-game.png` still displays the Project Sylpheed
title/menu with the OSD FPS badge, and the log continues to emit `VdSwap(...)`
after the heap release failures.

## Fix

`tools/thor/thor_android_game_status_report.ps1` now scopes AndroidRuntime and
native signal markers to the Xenia package or known Xenia log PIDs before
classifying a process crash. The report also prints total unscoped marker counts
so device-wide noise remains visible without being treated as an emulator crash.

For the 20:52 library repro, the corrected report is:

- `classification=project_sylpheed_heap_release_failures_no_crash_marker`
- `xenia_pid_count=1`
- `package_pid_count=2`
- `android_runtime_count=0`
- `android_runtime_total_count=5`
- `native_signal_count=0`
- `native_signal_total_count=2`
- `base_heap_release_count=13`
- `physical_heap_count=13`
- `base_heap_release_address=1DB18CC0`
- `physical_heap_release_address=BDB18CC0`
- `physical_heap_parent_address=1DB18CC0`

The older user-captured New Game packet
`scratch/thor-debug/20260527-201400-user-new-game-crash-current` still
classifies as `project_sylpheed_heap_native_abort` after PID scoping:

- `native_signal_count=6`
- `native_signal_total_count=12`
- `native_abort_message=terminating with uncaught exception of type xe::kernel::reenter_exception`
- `crash_thread=XThread3A822CB0`
- `base_heap_release_count=18`
- `physical_heap_count=18`

## Interpretation

The Project Sylpheed heap-release issue is real, but the 20:52 game-library
packet is not crash proof. It proves repeated interior physical frees inside the
same parent heap region while the process keeps presenting. The older packet
proves the native abort class, but it predates the address diagnostics.

## Next

Capture a fresh Project Sylpheed New Game crash through the game library with
the scoped classifier before attempting heap behavior changes. If the fresh
packet includes both the scoped native abort and the physical release addresses,
audit `MmFreePhysicalMemory`, `PhysicalHeap::Release`, and `BaseHeap::Release`
for a guarded interior-free compatibility fix.
