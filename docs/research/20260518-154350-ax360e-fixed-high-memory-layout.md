# aX360e Fixed High-Memory Layout Import

Date: 2026-05-18 15:43 EDT

Branch: `master`

## Goal

Take a concrete Android ARM64 idea from aX360e instead of continuing to polish
the helper mini-JIT path: make the guest memory map and ARM64 code-cache map
usable in predictable high 32-bit windows.

## Source References

- aX360e local audit checkout:
  `scratch/upstream/ax360e`
- Branch: `main`
- Commit: `2b4c889e8f849d07ede83334fa0b323f5c647828`
- Reference files:
  - `app/src/main/cpp/xenia-canary/src/xenia/memory.cc`
  - `app/src/main/cpp/xenia-canary/src/xenia/cpu/backend/code_cache_base.h`
  - `app/src/main/cpp/xenia-canary/src/xenia/cpu/backend/a64/a64_backend.cc`
  - `app/src/main/cpp/xenia-canary/src/xenia/cpu/backend/a64/a64_emitter.cc`
- Related Edge reference:
  `scratch/upstream/xenia-edge`, branch `edge`, commit
  `302034249048dc9d664b12b6a3560a6d95933803`.

## What aX360e Does

- Adds `mmap_address_high` as a memory-layout knob.
- Maps guest memory at `mmap_address_high << 32`.
- Places A64 code-cache and indirection ranges in a related high address
  window, effectively `(mmap_address_high + 2) << 32`.
- Uses that predictable high-bias layout in the A64 emitter and guest
  trampoline path.

## What Landed Here

- Added `mmap_address_high`, default `0`, to keep the current dynamic mapping
  behavior unless explicitly requested.
- When `mmap_address_high` is in `1..124`, guest memory first tries
  `mmap_address_high << 32`, then falls back to the existing dynamic search if
  that fixed map fails.
- The ARM64 code cache now tries the aX360e-shaped high window when
  `mmap_address_high` is set:
  - indirection table: `((mmap_address_high + 2) << 32) | 0x80000000`
  - generated code: `((mmap_address_high + 2) << 32) | 0xA0000000`
- Added Android intent and Thor script plumbing:
  `tools/thor/thor_xenia_debug.ps1 -MmapAddressHigh 8`.

## Why This Matters

This is not a game-compatibility fix by itself. It is infrastructure that makes
the next native AArch64 emitter import less ugly:

- predictable guest memory base
- predictable code-cache high bits
- predictable indirection-table high bits
- easier ARM64 address construction for direct/indirect guest calls
- closer parity with the Android ARM64 fork that appears to run games

## Validation

Commands:

- `git diff --check`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore -DeviceSerial c3ca0370`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`
- `Get-FileHash android\android_studio_project\app\build\outputs\apk\github\debug\app-github-debug.apk -Algorithm SHA256`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragon -DeviceSerial c3ca0370 -MmapAddressHigh 8`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode Capture -DeviceSerial c3ca0370`
- `powershell -ExecutionPolicy Bypass -File tools\arm64\arm64_conversion_audit.ps1 -OutPath scratch\arm64-ax360e-high-map-audit.md -LatestLogs 4`

APK SHA-256:

- `28CF832C88A1F6AFB96CF5D5571A5702D55A53E9A60D2B4A81E08B965EF8A2A7`

Thor captures:

- early startup proof: `scratch/thor-debug/20260518-154216-*`
- longer Blue Dragon run: `scratch/thor-debug/20260518-154338-*`

Device evidence:

- guest memory fixed high base:
  `Mapped guest memory at fixed high base 0000000800000000`
- ARM64 code cache fixed high base:
  `ARM64 code cache allocated 134217728 bytes at 0000000AA0000000 mode=rwx_debug indirection=0000000A80000000`
- generated thunks in the fixed high cache:
  `host_to_guest=0000000AA0000020 guest_to_host=0000000AA0000050 resolve=0000000AA0000070`

Crash/watchdog evidence:

- No native `Fatal signal`, tombstone, `SIGSEGV`, `SIGILL`, or indirection
  `mprotect failed` line was found in the searched output.
- Blue Dragon still hits the same D3D frame-0 watchdog:
  `The GPU is hung! D3D version is 3529.0 retail, kernel is 65535, frame is 0.`
- Ring pointers are still drained and matching:
  `CP_RB_RPTR/WPTR 0x1f/0x1f`, then `0x25/0x25`.
- The `VdSwap` line in the log is still a symbol/import listing, not explicit
  runtime `GPU swap trace: VdSwap` proof.

## Decision

Keep the fixed high-memory layout as an opt-in Thor/aX360e compatibility
experiment. Do not enable it by default yet. Next aX360e-backed import should
be either:

- guest trampoline API and backend trampoline allocation, or
- the real A64 emitter/control-call path that consumes the fixed high
  indirection layout.
