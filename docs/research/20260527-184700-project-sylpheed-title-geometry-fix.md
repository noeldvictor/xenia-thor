# Project Sylpheed Title Geometry Fix

Date: 2026-05-27

## Result

Project Sylpheed's title/menu geometry bug is fixed in the installed Android APK. The user confirmed the title screen no longer shows the top-left quadrant anchored in the lower-right.

## Evidence

- Bad title/menu proof:
  `scratch/thor-debug/20260527-sylpheed-vd-scaler-output-fix/user-says-still-bad-now.png`.
- Presenter sanity proof:
  `scratch/thor-debug/20260527-project-sylpheed-solid-presenter-probe/solid-probe.png`
  filled the full Thor display, proving the Android surface and presenter could paint fullscreen.
- App-path proof after the fix:
  `scratch/thor-debug/20260527-sylpheed-app-stable-launch/screenshot-30s.png`
  reached Project Sylpheed through the real Android launcher path, fullscreen.
- User confirmation after pressing through to the title path: "title screen fixed".

## Fixes

- Corrected Xbox VD packed width/height decoding in
  `src/xenia/kernel/xboxkrnl/xboxkrnl_video.cc`.
  `0x050002D0` is `1280x720`, not `720x1280`.
- Removed the Project Sylpheed-specific Android launcher hardcode that forced
  `480p` and `gpu_use_vd_scaler_output_for_swap`.
- Kept Vulkan swap guest output at the source/frontbuffer size; the swap gamma
  pass samples 1:1 and is not a scaler.
- Forced Vulkan QuadList conversion to CPU triangle lists on Android. The bad
  title/menu path used `kQuadList` draws on Adreno 740, and the geometry-shader
  expansion path matched the lower-right quadrant failure mode.
- Added stable Android app launch overrides so old research cvars in
  `xenia.config.toml` do not affect normal app launches.

## Remaining Sylpheed Issues

- Some direct diagnostic launches still hit the known guest heap/runtime class:
  `BaseHeap::Release failed because address is not a region start`,
  `PhysicalHeap::Release failed due to parent heap failure`, and
  `RtlRaiseException(702DF8D0(E06D7363))`.
- After pressing Start from the app-path movie capture, one host capture saw a
  black frame while the user-confirmed title path was fixed on-device. Treat the
  next Sylpheed lane as crash/black-frame compatibility, not title positioning.

## Validation

- `tools/thor/thor_build.ps1 -Mode NativeCore` passed.
- `tools/thor/thor_build.ps1 -Mode ApkShell` passed.
- `tools/thor/thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode Install` passed.
- `git diff --check` passed with expected CRLF warnings only.
