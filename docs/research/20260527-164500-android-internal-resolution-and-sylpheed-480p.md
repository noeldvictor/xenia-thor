# Android Internal Resolution And Project Sylpheed 480p Sanity

## Summary

Added a user-facing Android internal resolution setting with 480p, 720p, and
1080p choices. On Android Vulkan this is implemented as a guest video-mode
setting through `kernel_display_resolution`, not as Vulkan render-target
upscaling, because the current Vulkan texture/render-target path is still
hardcoded to 1x draw resolution.

## Screen Check

- Fresh live screen check before the patch showed a black guest surface with
  only the top-left FPS badge:
  `scratch/thor-debug/20260527-live-check-now/live-screen-now.png`.
- That matched the previous forced-present source experiment being left active,
  not Android menu UI covering the game.

## Patch

- `SettingsActivity` now shows an `Internal resolution` radio group:
  - `480p (720 x 480)`
  - `720p (1280 x 720)`
  - `1080p (1920 x 1080)`
- `XeniaAndroidSettings` persists the selected value and passes it as
  `kernel_display_resolution` in launch arguments.
- `EmulatorActivity` also forwards direct ADB/app intent
  `kernel_display_resolution` extras.
- `VdQueryVideoMode` now reports the configured guest video mode:
  - `480p` -> `720x480`, progressive, widescreen, not hi-def.
  - `720p` -> `1280x720`, progressive, widescreen, hi-def.
  - `1080p` -> `1920x1080`, progressive, widescreen, hi-def.
- The launcher/settings copy now names video settings explicitly.

## Device Proof

- Settings picker proof:
  - `scratch/thor-debug/20260527-resolution-setting-proof/settings-resolution.png`
  - `scratch/thor-debug/20260527-resolution-setting-proof/settings-resolution-scrolled.png`
- Latest installed launcher proof:
  - `scratch/thor-debug/20260527-resolution-setting-proof/live-final-launcher.png`
- Short Project Sylpheed 480p sanity capture:
  - `scratch/thor-debug/20260527-project-sylpheed-480p-sanity/project-sylpheed-480p.png`
  - `scratch/thor-debug/20260527-project-sylpheed-480p-sanity/project-sylpheed-480p-logcat.txt`

Correction from live retest: the Square Enix logo is movie-path evidence, not
title/menu proof. Do not use it to claim the Project Sylpheed title/menu
geometry is fixed. The scaler trace still showed the game presenting a 1280x720
front buffer while the scaler output was 720x480:

```text
VdInitializeScalerCommandBuffer ... src_wh=050002D0 out_wh=02D001E0 front_wh=050002D0
```

This is enough to prove the new setting changes the guest video/scaler path, but
it is not proof that the title/menu positioning bug is fixed.

## Remaining Compatibility Problem

Project Sylpheed still emits the existing physical-heap/guest-exception class:

```text
BaseHeap::Release failed because address is not a region start
PhysicalHeap::Release failed due to parent heap failure
RtlRaiseException
```

So the resolution setting fixes presentation fit for this route, but it is not a
game-compatibility fix for Project Sylpheed yet.

## Validation

- `git diff --check` passed with the repo's expected CRLF warnings.
- `tools/thor/thor_build.ps1 -Mode NativeCore` passed.
- `tools/thor/thor_build.ps1 -Mode ApkShell` passed.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` passed.
