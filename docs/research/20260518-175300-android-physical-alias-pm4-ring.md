# 2026-05-18 17:53 - Android Physical Alias PM4 Ring Fix

This note is for the experimental xenia-thor fork only. It is not official Xenia
documentation, not a compatibility claim, and not a support request for upstream.

## Question

Why did Blue Dragon write PM4 command packets to the ring buffer while the GPU
command processor saw only zeroes on Thor Android?

## Evidence

- Blue Dragon initialized the ring with:
  - virtual ring alias: `BFC50000`
  - physical ring alias from `MmGetPhysicalAddress`: `1FC50000`
- In `scratch/thor-debug/20260518-173814-*`, the ARM64 guest store watch saw real
  command dwords written to `BFC50000`, including values such as `C0114800`,
  `000003FF`, `C0013F00`, and later draw/event packet data.
- In the same capture, the GPU command processor read `1FC50000` and saw all
  zeroes. That meant the virtual and physical aliases were backed by different
  host mappings.

## Root Cause

Two POSIX Android mapping issues combined:

- `MapFileView` used `MAP_PRIVATE | MAP_ANONYMOUS` even when given a file handle,
  so views did not share the intended backing object.
- Guest heap commit paths later replaced already mapped guest memory views with
  anonymous `AllocFixed` mappings, undoing shared aliasing even after
  `MapFileView` was corrected.

## Fix

- `memory_posix.cc`: map file views with `MAP_SHARED` and treat `MAP_FAILED` as
  a failed mapping.
- `memory.cc`: on POSIX guest heap commit, use `mprotect` on the existing shared
  view instead of remapping the committed range.

## Result

After the fix, `scratch/thor-debug/20260518-174358-*` showed the GPU command
processor reading real packets from physical memory:

- `PM4_ME_INIT`
- `PM4_INDIRECT_BUFFER`
- `PM4_EVENT_WRITE_SHD`
- `PM4_DRAW_INDX_2`
- `PM4_INVALIDATE_STATE`

This moved the Blue Dragon blocker from "empty command buffer" to actual GPU
completion/writeback and A64 generated-code crash debugging.
