# Burnout Revenge live frontend stall

## Context

User reported Burnout crashes or gets stuck after movie/title input. This pass
used the real Android game library path on AYN Thor `c3ca0370` after the Android
IME fix APK was deployed.

## Packets

- Pre-clear screenshot/status packet:
  `scratch/thor-debug/remote-debug-20260528-200313`.
- Controlled 90 second route packet:
  `scratch/thor-debug/remote-debug-20260528-200427`.
- Mid-route stuck screenshot/status packet:
  `scratch/thor-debug/remote-debug-20260528-200531`.

Burnout title metadata in the controlled packet:

- `title_id=454107DC`
- `media_id=223759FA`
- `classification=launched_no_crash_marker`
- `android_runtime_count=0`
- `native_signal_count=0`
- `guest_crash_count=0`
- `vulkan_swapchain_count=2`

## Visual state

The 90 second screenrecord begins in the launcher, then reaches a full-screen
Burnout road/city background by about 30 seconds. No title logo, prompt, save
prompt, or player-name keyboard is visible in the final state.

Extracted frame proof:

- `scratch/thor-debug/remote-debug-20260528-200427/frame-030.png`
- `scratch/thor-debug/remote-debug-20260528-200427/frame-060.png`
- `scratch/thor-debug/remote-debug-20260528-200427/frame-085.png`

Sampled pixel comparison outside the FPS overlay:

- frame 030 vs frame 060: `AvgAbsRgbDelta=0.002`, `ChangedSamples=0`
- frame 060 vs frame 085: `AvgAbsRgbDelta=0`, `ChangedSamples=0`
- frame 030 vs frame 085: `AvgAbsRgbDelta=0.002`, `ChangedSamples=0`

This is effectively a static non-black frontend/background state while the app
continues presenting.

## Input and runtime evidence

Input rows in `remote-debug-20260528-200427/logcat-focused.txt`:

- Debug Start down rows: `16`
- Debug A down rows: `4`
- Physical Odin Start down rows: `1`
- Native Android HID debug-key down rows: `7`
- Native Android HID physical Start down rows: `1`

The route proves Android-side debug input delivery and at least early native HID
delivery. Later debug A/Start rows are still visible in Java logs, but the game
does not leave the static road scene.

Runtime rows:

- `AndroidAudioDriver opened AAudio output: sample_rate=48000 channels=2`
- `VdSwap` rows: `890`
- `Fatal signal`: `0`
- `AndroidRuntime`: `0`
- `RtlRaiseException`: `0`

`xeRtlNtStatusToDosError 103 => 3E5` appears in this packet (`825` rows), but
it also appears heavily in the earlier Burnout autosave/name-prompt packets
(`remote-debug-20260528-192326` has `3420` rows), so it is not sufficient as the
root cause by itself.

## Comparison with earlier Burnout packets

Earlier real-library packets reached further UI states:

- `scratch/thor-debug/remote-debug-20260528-192111`: title screen,
  `Press START to continue`, no scoped crash marker.
- `scratch/thor-debug/remote-debug-20260528-192326`: autosave prompt,
  no scoped crash marker.
- `scratch/thor-debug/remote-debug-20260528-193140`: old player-file-name prompt
  before Android IME replacement, no scoped crash marker.

The current packet is therefore not a simple launch failure. It is a live
frontend/progress stall after movie/title input where presentation continues but
the UI/prompt route does not advance.

## Decision

Classify current Burnout as
`burnout_live_static_frontend_background_no_scoped_crash`, not as an Android
process crash.

Do not patch controller mapping from this packet: Start mapping is correct in
source and physical/debug Start reaches the input stack. Do not treat the
`STATUS_PENDING` conversion rows as root cause without stronger correlation.

Next useful slice:

- Reproduce with focused guest progress evidence from the real library path:
  A64 thread snapshots, wait trace, and a later packet around the static road
  state.
- If guest progress is healthy, pivot to a Burnout frontend/UI render-target or
  overlay/presentation probe to determine why the background presents without
  title/save/name UI.
