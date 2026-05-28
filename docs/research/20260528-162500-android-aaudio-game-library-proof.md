# Android AAudio Game-Library Proof

## Repro

- Branch / commit: `blue-dragon-route-engine-reset` / `73b4d0747`.
- Device: `c3ca0370` / AYN Thor.
- APK SHA256 from the deployed build:
  `42D64598CEB87F4FD05E5C95DEECF92A9E5F995935799AE938D9001B0E93C39E`.
- Launched `LauncherActivity`, captured UI packet
  `scratch/thor-debug/remote-debug-20260528-162119`, then tapped the
  launcher `Launch Last Game` card for Project Sylpheed.
- Captured proof packet:
  `scratch/thor-debug/remote-debug-20260528-162241`.

## Result

The app-library route reaches the native Android audio backend and opens AAudio:

- `AndroidAudioSystem selected native Android audio backend`
- `AndroidAudioSystem creating Android audio driver 0`
- `AndroidAudioDriver opened AAudio output: sample_rate=48000 channels=2`

There is no `NopAudioSystem created silent audio driver` row in the proof
packet. The screenshot shows the Project Sylpheed opening route with the FPS
OSD visible.

## Compatibility State

The packet still reports Project Sylpheed physical heap release failures:

- `classification=project_sylpheed_heap_release_failures_no_crash_marker`
- `base_heap_release_count=15`
- `physical_heap_count=15`
- `guest_crash_count=0`
- `android_runtime_count=0`
- `native_signal_count=0`

So Android audio is no longer blocked at NOP and the game-library path opens
AAudio, but Project Sylpheed compatibility still has a separate heap ownership
problem to solve before New Game can be considered fixed.

## Next

Continue Project Sylpheed compatibility from the heap ownership lane, or use
another title/game-library route to confirm audible output by ear if the user is
present on the handheld.
