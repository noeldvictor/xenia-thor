# UMA Upload-Path Perf Counter — Implementation Plan (iter 14)

## Why this iteration is a plan, not the patch

Tooling was unstable this turn (a corrupted Read result + failed Greps on
`vulkan_presenter.cc`). Editing live engine source (the diagnostic-counters
struct + record fns + LogSnapshot) under corrupting reads risks a botched
multi-file change that breaks the build. So this iteration produces the exact
plan as the durable result; the next iteration executes it against clean reads.

## Goal

A cap-independent UMA metric. `buffer_barriers/frame` was shown invalid (UMA
removes the staging COPY, not the barrier). The real signal is staging-copy
bytes/calls: ~nonzero with `gpu_uma_direct_shared_memory=false`, ~zero with it
`true`.

## Exact changes

1. `src/xenia/ui/vulkan/vulkan_presenter.cc`, `struct VulkanPerfCounters` (around
   line 107-136): add four atomics —
   `shared_memory_staging_copies`, `shared_memory_staging_copy_bytes`,
   `shared_memory_direct_writes`, `shared_memory_direct_write_bytes`.

2. Same file: add record fns following the `RecordBufferBarrier` pattern (guard
   on `VulkanPerfCountersEnabled()`, `fetch_add(..., relaxed)`):
   - `VulkanPerfCountersRecordSharedMemoryStagingCopy(uint64_t bytes)` ->
     increments copies by 1, copy_bytes by bytes.
   - `VulkanPerfCountersRecordSharedMemoryDirectWrite(uint64_t bytes)` ->
     increments direct_writes by 1, direct_write_bytes by bytes.
   Add both to the `VulkanPerfCountersLogSnapshot` printf line (same style as
   `buffer_barriers=...`): `shared_memory_staging_copies=... 
   shared_memory_staging_copy_bytes=... shared_memory_direct_writes=... 
   shared_memory_direct_write_bytes=...`.

3. `src/xenia/ui/vulkan/vulkan_diagnostic_counters.h`: declare the two new record
   fns.

4. `src/xenia/gpu/vulkan/vulkan_shared_memory.cc`:
   - `#include "xenia/ui/vulkan/vulkan_diagnostic_counters.h"` (verify not already).
   - In `UploadRanges` (staging path), at each `CmdVkCopyBuffer` emission, call
     `VulkanPerfCountersRecordSharedMemoryStagingCopy(total_region_bytes)` (sum of
     the emitted `upload_regions_` sizes for that copy). There are two copy
     sites (the mid-loop flush and the final flush) — instrument both.
   - In `UploadRangesDirect` (UMA path), in the per-range memcpy loop, call
     `VulkanPerfCountersRecordSharedMemoryDirectWrite(length_bytes)` per range.

## Validate

- `git diff --check`; PowerShell parser N/A (C++). Build NativeCore + ApkShell.
- Deploy; deterministic A/B on Gears 3 menu (renders ~30fps) via -InputSequence,
  `--ez vulkan_trace_perf_counters true`, once `--ez gpu_uma_direct_shared_memory
  false` then `true`.
- Expect: OFF -> staging_copies>0, staging_copy_bytes>0, direct_writes=0; ON ->
  staging_copies~0, direct_writes>0, direct_write_bytes>0. Report the bytes delta
  = the copy traffic UMA removes. That is the cap-independent UMA win number.

## Process reminders for next iter

- ScheduleWakeup must be the ONLY call in its message.
- Run git and file-reads sequentially in their own messages (stderr cancels
  parallel siblings).
- Re-read vulkan_presenter.cc cleanly before editing (this turn's read corrupted).
