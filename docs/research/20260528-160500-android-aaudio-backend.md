# Android AAudio Backend

## Why

Android launches used `apu=any`, but Android only registered `NopAudioSystem`.
That made the settings migration to unmuted audio ineffective: the app still
selected a silent backend.

## Patch

- Added `xenia-apu-android` with an `AndroidAudioSystem` and AAudio-backed
  `AndroidAudioDriver`.
- The driver opens shared low-latency 48 kHz stereo float output, converts
  Xenia's 6-channel big-endian float frame to stereo, queues submitted frames,
  releases the client semaphore as frames drain, and fills silence on underrun.
- Registered Android audio before NOP in `CreateAudioSystem`, so `apu=any`
  selects native Android audio on Android.
- Added an explicit `Android AAudio` setting while keeping `Auto` and
  `Disabled`.
- Raised Android min/native platform from 24 to 26 because AAudio is API 26+.
- Fixed `tools/thor/thor_xenia_debug.ps1` so scripted launches no longer
  hard-code `--es apu nop`; default is now `-ApuDriver any`.
- Fixed `tools/thor/thor_android_remote_debug.ps1` so PowerShell 5.1 does not
  abort on successful `adb pull` progress written through stderr.

## Validation

- `python xenia-build premake --target_os=android` regenerated Android NDK
  files and included `xenia-apu-android`.
- `tools/thor/thor_build.ps1 -Mode NativeCore` passed for Android ARM64 and
  x86_64.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` passed.
- Deployed APK SHA256:
  `42D64598CEB87F4FD05E5C95DEECF92A9E5F995935799AE938D9001B0E93C39E`.
- Direct Project Sylpheed launch with `-ApuDriver android` produced
  `AndroidAudioSystem selected native Android audio backend` in
  `scratch/thor-debug/remote-debug-20260528-161000-android-aaudio-proof/logcat-full.txt`.
- That packet has no `NopAudioSystem created silent audio driver` row.
- The same direct launch still hit
  `project_sylpheed_guest_heap_rtlraiseexception` at `0x8245BDEC`; this is
  separate from audio backend selection.
- `thor_android_remote_debug.ps1 -Mode Screenshot -DeviceSerial c3ca0370`
  completed and wrote `scratch/thor-debug/remote-debug-20260528-160025`,
  proving the ADB stderr handling fix.

## Remaining

This proves Android now selects a real native audio backend instead of NOP.
It does not yet prove audible output in Project Sylpheed because the proof
route crashed before an audio driver open / render-client path was observed.
Next useful audio proof is a game-library launch that reaches active audio and
logs `AndroidAudioSystem creating Android audio driver` plus
`AndroidAudioDriver opened AAudio output`.
