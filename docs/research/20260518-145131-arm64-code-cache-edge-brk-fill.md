# ARM64 Code Cache Edge BRK Fill

- Date: 2026-05-18 14:51 EDT
- Branch: `master`
- Local fork: `xenia-thor`
- Goal: take one more safe Xenia Edge A64 debug hardening behavior without
  importing the full Edge `CodeCacheBase` stack yet.

## Source

- `has207/xenia-edge`, branch `edge`, commit
  `302034249048dc9d664b12b6a3560a6d95933803`.
- Reference file: `src/xenia/cpu/backend/a64/a64_code_cache.cc`.
- Relevant behavior: `A64CodeCache::FillCode` fills unwritten generated-code
  memory with `BRK #0` (`0xD4200000`).

## Imported

- Added `FillUnwrittenCodeWithBreakpoints` to
  `src/xenia/cpu/backend/arm64/arm64_code_cache.cc`.
- The Thor ARM64 code cache now fills its allocated generated-code region with
  `BRK #0` immediately after allocation.

## Why This Is Safe

- ARM64 instructions are fixed-width 4-byte words, and the cache allocation is
  page-sized, so the fill is naturally aligned for the current allocator.
- Placed guest/host code still overwrites its committed range and flushes that
  range before execution.
- Padding and uncommitted cache space now trap if control flow lands there,
  instead of executing zeroed memory.

## Not Imported

- Xenia Edge `CodeCacheBase`, indirection tables, unwind reservations,
  `PlaceData`, `CommitExecutableRange`, and platform-specific A64 cache files.
  Those are the next larger backend plumbing batch and need a real integration
  boundary.

## Validation

- `git diff --check` passed with only CRLF normalization warnings.
- `tools/thor/thor_build.ps1 -Mode NativeCore -DeviceSerial c3ca0370`
  passed for `arm64-v8a` and `x86_64`.
