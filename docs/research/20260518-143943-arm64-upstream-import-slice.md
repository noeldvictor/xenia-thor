# ARM64 Upstream Import Slice

- Date: 2026-05-18 14:39 EDT
- Branch: `master`
- Local fork: `xenia-thor`
- Goal: take the clean, immediately useful ARM64/Android pieces from aX360e
  and Xenia Edge without destabilizing the Thor build.

## Sources

- `has207/xenia-edge`, branch `edge`, commit
  `302034249048dc9d664b12b6a3560a6d95933803`.
- `aenu1/ax360e`, branch `main`, commit
  `2b4c889e8f849d07ede83334fa0b323f5c647828`.
- Xenia Edge root license: BSD-style Xenia license.
- aX360e README says to check file headers and directory licenses. The
  immediately imported Android build/manifest knobs are configuration shape,
  not copied implementation bodies. `libadrenotools` is BSD-2-Clause but was
  not imported in this slice.

## Imported Now

### From Xenia Edge A64 Backend

Reference files:

- `src/xenia/cpu/backend/a64/a64_backend.h`
- `src/xenia/cpu/backend/a64/a64_backend.cc`

Adapted into:

- `src/xenia/cpu/backend/arm64/arm64_backend.h`
- `src/xenia/cpu/backend/arm64/arm64_backend.cc`

What landed:

- ARM64 backend breakpoint patching with the fixed-width `BRK #0`
  instruction (`0xD4200000`).
- ARM64 breakpoint uninstall restoration.
- ARM64 illegal-instruction exception callback that recognizes the `BRK #0`
  trap and forwards it to `Processor::OnThreadBreakpointHit`.
- Temporary page-protection switching around breakpoint patching plus
  instruction-cache flush.

Why this slice first:

- It closes a real x64-to-ARM64 backend parity gap from the conversion audit.
- It improves debugability before the full A64 emitter is merged.
- It is small enough to validate with the existing native build.

### From aX360e Android Build Shape

Reference files:

- `app/src/main/cpp/CMakeLists.txt`
- `app/src/main/AndroidManifest.xml`

Adapted into:

- `premake5.lua`
- `android/android_studio_project/app/src/main/AndroidManifest.xml`

What landed:

- Android native build flags:
  - `-fno-omit-frame-pointer`
  - `-funwind-tables`
  - `-Wl,-z,max-page-size=16384`
- Android manifest:
  - `android:allowNativeHeapPointerTagging="false"`

Why this slice first:

- Frame pointers and unwind tables make ARM64 native crashes and generated-code
  debugging less blind.
- The 16 KiB max-page-size linker setting matches modern Android device
  requirements better and is harmless for Thor bring-up.
- Disabling native heap pointer tagging avoids tagged native pointers leaking
  into code paths that were written assuming raw host pointers.

## Not Imported Yet

- Full Xenia Edge A64 emitter/sequences/thunks/code-cache family. It is the
  right destination, but it depends on `CodeCacheBase`, A64 register allocation
  shape, backend context layout, indirection tables, and wider CPU API changes
  that do not match the current helper mini-JIT scaffold one-for-one.
- aX360e SAF devices, Android HID, AAudio/OpenSL audio, and AdrenoTools. These
  are promising Android references, but each needs a separate license/file
  audit and integration boundary before copying implementation files.
- aX360e `xe_a64_code_cache_posix.cpp`. Xenia Edge has a cleaner current POSIX
  A64 unwind/code-cache model, so use Edge first unless Android-specific
  behavior proves necessary.

## Validation

- `git diff --check` passed with only CRLF normalization warnings.
- `tools/thor/thor_build.ps1 -Mode NativeCore -DeviceSerial c3ca0370` passed.
- `python xenia-build premake --target_os=android` regenerated the ignored
  Android makefiles so the new premake Android flags are active locally.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` passed
  and installed on Thor.
- APK SHA-256:
  `4CE19E505D48CF50B5B3BC9C3699EC39750CE0EF033F721DBFA309AAAC7DF4AB`.
- Blue Dragon Disc 1 regression run:
  `scratch/thor-debug/20260518-144723-*`.
  - Target:
    `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`.
  - No `Fatal signal`, tombstone, or native crash signature was found in the
    captured logcat.
  - The ARM64 backend initialized and the helper mini-JIT still compiled guest
    functions, so this import slice did not disable the current execution path.
  - The game still does not reach title. It remains at the D3D frame-0 watchdog:
    `The GPU is hung! D3D version is 3529.0 retail, kernel is 65535, frame is 0`.
  - Ring pointers are still drained/matching in the watchdog dump:
    `CP_RB_RPTR/WPTR 0x1f/0x1f`, then `0x25/0x25`.
- Scratch conversion audit after this import:
  `scratch/arm64-import-slice-audit.md`.
  - The previous breakpoint/exception callback gap is closed.
  - Remaining x64-grade backend gaps include `HostToGuestThunk`,
    `GuestToHostThunk`, `ResolveFunctionThunk`, code-cache indirection,
    `CommitExecutableRange`, `PlaceData`, unwind reservation/commit metadata,
    and 52 helper-call sites in the mini-JIT.

## Next Import Batches

1. Port Xenia Edge's backend context/thunk skeleton behind our existing
   mini-JIT entry path.
2. Add code-cache indirection metadata compatible with guest trampolines and
   resolve thunks.
3. Start replacing helper-heavy scalar/control/memory op lowering with the
   Edge A64 emitter/sequences.
4. Import aX360e Android AAudio and HID only after deciding whether they live
   as native Xenia subsystems or Android app glue.
5. Evaluate AdrenoTools as an optional debug/perf lane, default-off and never
   required for correctness.
