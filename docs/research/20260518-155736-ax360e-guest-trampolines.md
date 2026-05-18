# aX360e Guest Trampoline Import

- Date: 2026-05-18 15:57 EDT
- Branch: `master`
- Donor: `aenu1/ax360e` `main` at `2b4c889e8f849d07ede83334fa0b323f5c647828`
- Cross-check donor: `has207/xenia-edge` `edge` at `302034249048dc9d664b12b6a3560a6d95933803`
- License posture: Xenia-derived BSD-licensed code, adapted into this unofficial research fork with provenance noted here.

## Why This Slice

The current ARM64 path had generated host/guest/resolve thunks and an aX360e-style fixed high map, but it still lacked the guest trampoline API used by the A64 backend family. aX360e uses guest trampolines for host callbacks that are represented as guest-callable addresses, especially kernel/runtime callback slots.

This slice imports the structural trampoline pieces before trying to land the full A64 emitter.

## Imported / Adapted

- `BitMap::AcquireFromBack()` from aX360e so long-lived trampoline slots can allocate from the back of the pool.
- Backend API hooks from aX360e:
  - `InitializeBackendContext`
  - `DeinitializeBackendContext`
  - `PrepareForReentry`
  - `SetGuestRoundingMode`
  - `PopulatePseudoStacktrace`
  - `CreateGuestTrampoline`
  - `FreeGuestTrampoline`
  - `GuestTrampolineGroup`
- aX360e-style 256-byte backend context prefix before `PPCContext` allocation.
- ARM64 guest trampoline generation:
  - loads `proc`, `userdata1`, `userdata2`, and `guest_to_host_thunk`
  - emits `br x9`
  - flushes the host instruction cache
  - inserts a 64-bit host pointer into the ARM64 indirection table.

## Local Adaptations

- Kept the current `arm64` backend name and code-cache interface instead of renaming to `a64`.
- Allocated trampoline host code with anonymous executable memory because the current ARM64 code cache already supports external 64-bit indirection targets.
- Did not wire `KernelModule::GenerateTrampoline` or kernel object-type callbacks to the new backend trampolines in this slice. That needs a separate behavior change with guest-call proof.
- Kept the helper-backed mini-JIT path alive as fallback. This is infrastructure, not a full aX360e emitter import.

## Validation

Commands:

- `git diff --check`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore -DeviceSerial c3ca0370`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragon -DeviceSerial c3ca0370 -MmapAddressHigh 8`
- `powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode Capture -DeviceSerial c3ca0370`
- `powershell -ExecutionPolicy Bypass -File tools\arm64\arm64_conversion_audit.ps1 -OutPath scratch\arm64-ax360e-trampoline-audit.md -LatestLogs 4`

Results:

- Native core build: passed for `arm64-v8a` and `x86_64`.
- Full deploy: passed and installed to Thor serial `c3ca0370`.
- APK SHA-256: `88B1942C34BF7CA14805617E0963C7A75415381EDD78A48828838709A30A747E`.
- Thor capture: `scratch/thor-debug/20260518-155837-*`.
- Device log proof:
  - guest memory: `0000000800000000`
  - ARM64 code cache: `0000000AA0000000`
  - indirection: `0000000A80000000`
  - thunks: `host_to_guest=0000000AA0000020`, `guest_to_host=0000000AA0000050`, `resolve=0000000AA0000070`
  - trampoline host pool: `000000775BD04000-000000775BF24000`
- No native `Fatal signal`, tombstone, `SIGSEGV`, or `SIGILL` was found in the capture grep.

## Result

This successfully imports another real aX360e/Edge ARM64 backend building block and proves it initializes on the AYN Thor. It does not make Blue Dragon reach title.

Blue Dragon still hits the known guest D3D watchdog:

`The GPU is hung! D3D version is 3529.0 retail, kernel is 65535, frame is 0.`

The next useful imports are either:

- wire kernel/runtime callback paths through backend-created guest trampolines with proof that ARM64 indirect calls hit them, or
- start landing the full Edge/aX360e A64 emitter backbone in buildable families rather than continuing to expand helper calls.
