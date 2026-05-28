# Project Sylpheed Library New Game Repro

## Context

Project Sylpheed title/menu geometry is fixed. This slice used the Android game
library, not a manual file-picker shortcut, to reproduce the remaining New Game
heap-release failure class with the deployed diagnostics.

## Capture

- Packet:
  `scratch/thor-debug/20260527-205200-sylpheed-library-new-game-repro`.
- Route:
  `Launcher game library row -> Project Sylpheed -> Start -> A/New Game`.
- Pre-capture screen:
  `scratch/thor-debug/remote-debug-20260527-205112/screen.png` showed the
  Android launcher, so the repro did not overwrite an existing in-game state.
- Build:
  - Branch `blue-dragon-route-engine-reset`.
  - Commit `6bc802b25`.
  - APK SHA256
    `35073A3271E81783A7824407BE91660086872B15E3E4C9104D2EAC80523FF29C`.

The PID-scoped `thor_android_game_status_report.ps1` classifies the packet as:

- `classification=project_sylpheed_heap_release_failures_no_crash_marker`
- `title_id=535107D4`
- `media_id=2D2E2EEB`
- `android_runtime_count=0`
- `android_runtime_total_count=5`
- `native_signal_count=0`
- `native_signal_total_count=2`
- `base_heap_release_count=13`
- `physical_heap_count=13`
- `rtl_raise_exception=` empty
- `vulkan_swapchain_count=2`

The earlier unscoped status report overcalled this packet as a native abort
because unrelated AndroidRuntime/native-signal noise was present elsewhere in
logcat. `after-new-game.png` still shows the Project Sylpheed title/menu with
the OSD FPS badge, and the log continues to emit `VdSwap(...)` after the heap
release failures. This packet is heap compatibility evidence, not crash proof.

## Heap Evidence

The new diagnostics captured the address class that was missing from the old
packet:

- First failed parent release:
  `base_heap_release_address=1DB18CC0`.
- First failed physical release:
  `physical_heap_release_address=BDB18CC0`.
- First translated parent address:
  `physical_heap_parent_address=1DB18CC0`.
- Physical heap metadata:
  `heap_base=A0000000`, `page_size=10000`.
- Parent heap metadata:
  `heap_base=00000000`, `page_size=1000`, `region_base=1C220000`,
  `region_pages=14976`, `state=3`.

All 13 reported release failures follow the same pattern: the title path calls
`MmFreePhysicalMemory(type=0, base_address=...)` with a physical address that
translates into the interior of the same large parent heap region, not to the
region start. Examples:

- `BDB18CC0 -> 1DB18CC0`
- `BC79C900 -> 1C79C900`
- `BD07CA80 -> 1D07CA80`
- `BD610B80 -> 1D610B80`
- `BE078D80 -> 1E078D80`
- `BED2A080 -> 1ED2A080`
- `BF26A0C0 -> 1F26A0C0`

This makes the remaining compatibility issue a heap problem, not title geometry,
audio, launcher routing, or broad Vulkan presentation. It does not by itself
prove that this specific library repro crashed.

## Interpretation

The evidence points at an interior physical free / parent heap release mismatch:
`MmFreePhysicalMemory` receives physical addresses inside a large parent
allocation, while `BaseHeap::Release` currently requires the translated parent
address to be exactly a region start.

The older user-reported New Game packet
`scratch/thor-debug/20260527-201400-user-new-game-crash-current` still
classifies as `project_sylpheed_heap_native_abort` after PID scoping because it
has Xenia package native-signal markers and the abort message `terminating with
uncaught exception of type xe::kernel::reenter_exception`. That packet lacks the
new address diagnostics, so the next useful proof is a fresh crash repro through
the game library with the scoped classifier.

Do not paper over this by blindly normalizing every interior physical free to
the containing parent region base. That could release a much larger live parent
allocation. The next patch needs a source/semantics audit first: prove whether
Xbox 360 `MmFreePhysicalMemory` accepts interior physical frees, whether Xenia is
missing physical suballocation tracking, or whether the title is exposing a
duplicate/range-selection bug in our heap model.

## Next

Capture a fresh Project Sylpheed New Game crash through the game library before
attempting behavior work, then audit `MmFreePhysicalMemory`,
`PhysicalHeap::Release`, and parent `BaseHeap::Release` behavior for physical
interior frees. A safe implementation needs explicit proof that the released
range is the intended allocation, not the whole parent region.
