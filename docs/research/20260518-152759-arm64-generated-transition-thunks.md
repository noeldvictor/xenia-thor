# ARM64 Generated Transition Thunks

Date: 2026-05-18 15:27 EDT

Branch: `master`

## Goal

Generate real AArch64 thunk code for the ARM64 backend's host-to-guest,
guest-to-host, and resolve-thunk slots, using the Xenia Edge A64 backend shape
as the reference while keeping the current Thor helper mini-JIT ABI alive.

## Source References

- Xenia Edge local audit checkout:
  `scratch/upstream/xenia-edge`
- Branch: `edge`
- Commit: `302034249048dc9d664b12b6a3560a6d95933803`
- Reference area: `src/xenia/cpu/backend/a64/a64_backend.cc`, especially the
  A64 thunk emitter and transition-thunk generation shape.
- Local assembler dependency: `third_party/xbyak_aarch64`, pinned earlier to
  Fujitsu `xbyak_aarch64` commit
  `59c6a7e7368cd2dbb07c46abe83239ace4d58849` under Apache-2.0.

## What Landed

- Added a small `Arm64ThunkEmitter` in
  `src/xenia/cpu/backend/arm64/arm64_backend.cc` using
  `xbyak_aarch64`.
- Generated and placed three backend-owned thunks in the ARM64 code cache:
  - host-to-guest thunk
  - guest-to-host thunk
  - resolve-function thunk
- Set the ARM64 indirection table default target to the generated resolve
  thunk.
- Committed the force-return address range through the ARM64 code cache.
- Added a startup log line with generated thunk addresses so device captures
  prove the thunks exist in executable memory.

## Important Caveats

- This is not the full Xenia Edge A64 transition ABI.
- The generated thunks do not yet include Edge's full backend context,
  stackpoint, vector register save/restore, unwind, or exception recovery
  behavior.
- The current Thor helper mini-JIT still invokes compiled guest helpers through
  its existing `bool(PPCContext*, ThreadState*, uint32_t)` ABI. It is not yet
  routed through these thunks.
- This slice should be treated as runway for the native emitter and
  indirection path, not as a compatibility fix by itself.

## Validation

- `git diff --check` passed with CRLF normalization warnings only.
- Native core build passed:
  `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore -DeviceSerial c3ca0370`
- Full deploy passed and installed to Thor:
  `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`
- APK SHA-256:
  `186EA0502949D8AF4ACB5691C70563E42E0956A6006440AAC8F913CF43FB022D`
- Blue Dragon Disc 1 regression capture:
  `scratch/thor-debug/20260518-152749-*`

Device log evidence:

- ARM64 code cache allocated at startup:
  `ARM64 code cache allocated 134217728 bytes at 0000007734805000 mode=rwx_debug indirection=000000773C805000`
- Generated thunk addresses:
  `ARM64 transition thunks generated host_to_guest=0000007734805020 guest_to_host=0000007734805050 resolve=0000007734805070`
- Blue Dragon still reaches the known game watchdog:
  `The GPU is hung! D3D version is 3529.0 retail, kernel is 65535, frame is 0.`
- Ring pointers are still drained and matching:
  `CP_RB_RPTR/WPTR 0x1f/0x1f`, then `0x25/0x25`.

Searched crash signatures:

- No `Fatal signal`
- No tombstone line
- No `SIGSEGV`
- No `SIGILL`
- No indirection `mprotect failed` line

## Decision

Keep this generated-thunk slice. It proves xbyak_aarch64 thunk generation,
placement, and executable-code-cache startup on Thor without destabilizing the
current helper mini-JIT path. The next useful batch is to wire native guest
direct/indirect calls onto the indirection path behind a controlled backend
flag, then start replacing helper-call opcode families with native AArch64
lowering and differential checks.
