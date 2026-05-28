# Project Sylpheed Heap Abort Diagnostics

## Context

Project Sylpheed title/menu geometry is fixed. The remaining New Game failure is
a native abort after heap-release failures, not a launcher or presenter
positioning bug.

## Evidence From Existing Crash Packet

Packet:
`scratch/thor-debug/20260527-201400-user-new-game-crash-current`.

Updated `thor_android_game_status_report.ps1` now classifies the old packet as:

- `classification=project_sylpheed_heap_native_abort`
- `native_abort_message=terminating with uncaught exception of type xe::kernel::reenter_exception`
- `crash_thread=XThread3A822CB0`
- `base_heap_release_count=18`
- `physical_heap_count=18`
- `rtl_raise_exception=` empty

The old log proves the class, but it does not include the rejected heap address.
That was the blocker for a precise compatibility fix.

## Change

Added behavior-neutral diagnostics:

- `BaseHeap::Release` now logs the failing address, heap base, page size, page
  number, region base, region page count, state, and raw page-table entry when a
  release address is not a region start.
- `BaseHeap::Release` logs out-of-range release requests instead of allowing an
  unhelpful page-table access.
- `PhysicalHeap::Release` logs the physical address and translated parent heap
  address when parent release fails.
- `MmFreePhysicalMemory` logs the original type/base address and heap metadata
  when release fails.
- `thor_android_game_status_report.ps1` now reports abort message, crash thread,
  first heap-release lines, and new address fields when present.

This does not change successful release behavior or attempt to fix Sylpheed yet.
The goal is to make the next New Game repro point at the exact bad address and
region metadata.

## Validation

- `tools/thor/thor_android_game_status_report.ps1 -PacketDir scratch\thor-debug\20260527-201400-user-new-game-crash-current`
  reports `project_sylpheed_heap_native_abort` with abort thread/message.
- `tools/thor/thor_build.ps1 -Mode NativeCore` passed.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` passed.
- Installed APK SHA256:
  `35073A3271E81783A7824407BE91660086872B15E3E4C9104D2EAC80523FF29C`.

## Next

Reproduce Project Sylpheed New Game from the game library. The next crash packet
should include `base_heap_release_address`, `physical_heap_release_address`, and
`physical_heap_parent_address`; use those to decide whether this is an interior
free, physical-parent mapping mismatch, duplicate release, or heap-range
selection bug.
