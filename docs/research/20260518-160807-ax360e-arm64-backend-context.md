# aX360e ARM64 Backend Context Import

- Date: 2026-05-18 16:08 EDT
- Branch: `master`
- Donor: `aenu1/ax360e` `main` at `2b4c889e8f849d07ede83334fa0b323f5c647828`
- Cross-check donor: `has207/xenia-edge` `edge` at `302034249048dc9d664b12b6a3560a6d95933803`
- License posture: Xenia-derived BSD-licensed code, adapted into this unofficial research fork.

## Why This Slice

The previous aX360e import reserved a 256-byte backend prefix before `PPCContext`, but the ARM64 backend did not yet use it. The real Edge/aX360e A64 emitter expects backend-private context data for reserve state, cached FPCR values, guest tick pointer, stackpoint depth, and helper scratch storage.

This slice adds that backend context scaffold without switching to the full A64 emitter yet.

## Imported / Adapted

- ARM64 backend context structs:
  - `Arm64ReserveHelper`
  - `Arm64BackendStackpoint`
  - `Arm64BackendContext`
- ARM64 backend context lifecycle:
  - zero and initialize context prefix
  - attach backend reserve helper
  - cache FPU/VMX FPCR defaults
  - record `Clock::GetGuestTickCountPointer()`
  - clear stackpoint depth on reentry
  - clean stackpoint storage on thread teardown
- `Clock::GetGuestTickCountPointer()` helper so future native A64 code can read the guest tick pointer like aX360e.
- `Arm64Backend::SetGuestRoundingMode()` with ARM64 FPCR write guarded behind `XE_ARCH_ARM64`, and PPC `fpscr` state updated for all host architectures.

## Local Adaptations

- `Arm64BackendContext` is sized to fit inside the existing 256-byte prefix reserved by `ThreadState`.
- Stackpoint allocation remains dormant until the real A64 stack synchronization path lands.
- The current generated host-to-guest thunk still does not consume `x19` as backend context. That belongs with the full A64 transition ABI import.

## Validation

Commands:

- `git diff --check`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore -DeviceSerial c3ca0370`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragon -DeviceSerial c3ca0370 -MmapAddressHigh 8`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode Capture -DeviceSerial c3ca0370`
- `powershell -ExecutionPolicy Bypass -File tools\arm64\arm64_conversion_audit.ps1 -OutPath scratch\arm64-ax360e-backend-context-audit.md -LatestLogs 4`

Results:

- Native core build: passed for `arm64-v8a` and `x86_64`.
- Full deploy: passed and installed to Thor serial `c3ca0370`.
- APK SHA-256: `D7349937301314D7C7E96F2E7A613682271B78380F5F58AC9A35C5D928649787`.
- Thor captures:
  - `scratch/thor-debug/20260518-160908-*`
  - `scratch/thor-debug/20260518-160947-*`
- Device log still proves fixed high memory/code cache plus trampoline pool:
  - guest memory: `0000000800000000`
  - code cache: `0000000AA0000000`
  - indirection: `0000000A80000000`
  - trampoline pool: `000000775A7BF000-000000775A9DF000`
- No native `Fatal signal`, tombstone, `SIGSEGV`, or `SIGILL` was found in the searched capture output.

## Result

The aX360e ARM64 backend context scaffold now initializes on Thor and survives Blue Dragon launch. This is required groundwork for a real A64 emitter import, but it does not move Blue Dragon to title.

The current game wall remains:

`The GPU is hung! D3D version is 3529.0 retail, kernel is 65535, frame is 0.`

Next useful backend imports:

- full Edge/aX360e host-to-guest and guest-to-host ABI with `x19` backend context,
- native A64 direct/indirect call routing through the indirection table,
- stackpoint synchronization only after real A64 function prolog/epilog code exists.
