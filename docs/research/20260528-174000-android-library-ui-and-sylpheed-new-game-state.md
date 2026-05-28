# Android library UI and Sylpheed New Game state

## UI pass

The old launcher technically had a scanned library, but it read like a stack of
utility actions because the library section was below Scan/Add/Browse cards and
each game was a plain text row.

This patch keeps the existing native Android layout but makes the game library
the first controller target after the header:

- moves Game library / Recent games above the utility actions at runtime,
- focuses the first scanned game when the library has entries,
- replaces plain text rows with focusable game tiles,
- adds a compact cover placeholder using the game type (`ISO`, `XEX`, `M3U`),
- shortens paths to the useful folder/file tail,
- adds a status badge (`Ready`, recent status, guest heap, guest crash, etc.).

Proof screenshot:
`scratch/thor-debug/remote-debug-20260528-173943/screen.png`.

## Sylpheed New Game packet

Before deploying the UI patch, a no-clear remote debug packet captured the
current Project Sylpheed New Game state:
`scratch/thor-debug/remote-debug-20260528-173237`.

Status report:

- `classification=project_sylpheed_heap_release_failures_no_crash_marker`
- scoped `android_runtime_count=0`
- scoped `native_signal_count=0`
- `guest_crash_count=0`
- `vulkan_swapchain_count=4`
- first BaseHeap failure: `address=1DB18CC0`
- first PhysicalHeap failure: `physical_address=BDB18CC0`, `parent_address=1DB18CC0`

Visual/status joins:

- Screenshot was not a clean black-frame proof because the device burn-in /
  anti-image-retention overlay covered the bottom of the screen.
- `VdSwap` continued heavily (`vd_swap_count=25747`), so this packet is a live
  bad-state rather than a native process abort.
- Loading-loop audit still needs focused guest progress evidence:
  `guest_execution_evidence=missing_top_thread_snapshot`,
  `kernel_wait_evidence=missing_xboxkrnl_thread_wait_trace`.

Physical free-site audit on this packet:

- `failed_interior_free_count=17`
- `failed_interior_exact_allocation_result_count=0`
- owner allocation remains `BC220000:03A80000`
- failed free LR remains `821507BC`
- new CTR contexts include `82151590` and `82151650` in addition to the previous
  cluster.

## Decision

Do not hide or round down the physical free failures yet. The current New Game
failure is live/no-crash, and the free-site cluster still lacks guest
size/lifetime proof. The next compatibility slice should launch Project
Sylpheed from the game library with focused disassembly, thread wait tracing,
and thread snapshots so the LR/CTR cluster and live loading loop can be joined.
