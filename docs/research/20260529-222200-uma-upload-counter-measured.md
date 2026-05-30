# UMA Upload Win Measured (autonomous iter 15) — DEVICE PROVEN

## Result

A cap-independent A/B of `gpu_uma_direct_shared_memory` is now possible via new
shared-memory upload-path perf counters. On the Gears 3 menu (deterministic
hid_nop launch, `vulkan_trace_perf_counters` on, ~1560-1574 swaps each arm):

| metric | OFF (staging) | ON (UMA direct) |
|--------|---------------|-----------------|
| shared_memory_staging_copies | 16780 | 0 |
| shared_memory_staging_copy_bytes | 726,794,240 (~693 MB) | 0 |
| shared_memory_direct_writes | 0 | 18556 |
| shared_memory_direct_write_bytes | 0 | 765,206,528 (~730 MB) |
| issue_swaps | 1560 | 1574 |

Per frame: the staging path issues ~11 `vkCmdCopyBuffer` calls and ~0.44 MB of
copy traffic per frame; the UMA path replaces all of it with direct mapped writes
of equivalent bytes and **zero** `vkCmdCopyBuffer` calls. UMA path confirmed
engaged: `Shared memory: using unified-memory direct-write path (memory type 6,
host-coherent)`.

So the UMA direct-write path removes, per frame on this scene:
- ~11 transfer-queue buffer copies, and
- ~0.44 MB of redundant staging copy bandwidth (plus the matching staging
  allocation + transfer barrier the staging path needs),

while writing the same data straight into the GPU-visible buffer. The byte counts
match (~693 MB staging vs ~730 MB direct over similar frame counts), confirming
the same upload workload is taking the cheaper path.

## Why FPS didn't move earlier

Both arms run at the 60 fps vsync cap (here ~30 due to the title), so the saved
copy work shows as frame-time headroom / bandwidth, not higher FPS. The earlier
`buffer_barriers/frame` proxy was invalid (UMA removes the COPY, not the barrier);
these new copy-bytes counters are the correct cap-independent metric.

## Counters added

- `src/xenia/ui/vulkan/vulkan_presenter.cc`: 4 atomics
  (`shared_memory_staging_copy_count/bytes`, `shared_memory_direct_write_count/bytes`),
  `VulkanPerfCountersRecordSharedMemoryStagingCopy/DirectWrite`, and 4 fields in
  the `LogSnapshot` line.
- `src/xenia/ui/vulkan/vulkan_diagnostic_counters.h`: declarations.
- `src/xenia/gpu/vulkan/vulkan_shared_memory.cc`: records staged bytes at both
  `CmdVkCopyBuffer` sites in `UploadRanges`, and direct bytes in the
  `UploadRangesDirect` memcpy loop.

## Caveat / next

This proves the copy work removed, not yet a frame-time speedup. To convert to a
wall-clock number, capture queue_submit_us/present_us deltas on a GPU-bound (not
vsync-capped) scene, or temporarily uncap. The memory tradeoff stands: the UMA
path forces a non-sparse 512 MB shared-memory buffer. Recommendation: UMA
direct-write is a clear bandwidth/copy win on this UMA SoC; consider defaulting it
on for Thor after a broader on-device soak (it is correctness-proven and
host-coherent here).
