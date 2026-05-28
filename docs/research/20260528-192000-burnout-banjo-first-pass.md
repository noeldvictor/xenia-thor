# Burnout / Banjo first-pass compatibility packets

## Context

User reports:

- Burnout crashes.
- Banjo-Kazooie: Nuts & Bolts has sound but only a black screen.

Both checks used the real Android launcher/library path on AYN Thor
`c3ca0370`, with logcat cleared before launch.

## Burnout Revenge

Packet:
`scratch/thor-debug/remote-debug-20260528-192111`.

Result:

- Title ID `454107DC`, media ID `223759FA`.
- Screenshot reaches the Burnout Revenge title screen with `Press START to
  continue`.
- `classification=launched_no_crash_marker`.
- Scoped markers: `android_runtime_count=0`, `native_signal_count=0`,
  `guest_crash_count=0`.
- Visual report:
  `scratch/thor-debug/20260528-192111-burnout-start-a-visual-status.txt`.
- Debug-gamepad Start/A broadcasts were delivered, but this packet still sits at
  the title prompt, so it is not proof that physical Start advances or that the
  user's later crash is gone.

Decision: Burnout is not an immediate launch crash in this route. Reproduce the
user's crash with physical controls or a longer/input-specific route before a
behavior patch.

## Banjo-Kazooie: Nuts & Bolts

Packet:
`scratch/thor-debug/remote-debug-20260528-191801`.

Result:

- Title ID `4D5307ED`, media ID `3E567DFF`.
- Screenshot is black aside from the FPS OSD.
- Android audio opens AAudio:
  `AndroidAudioDriver opened AAudio output: sample_rate=48000 channels=2`.
- `VdSwap` continues (`vd_swap_count=274` in the short packet).
- `classification=launched_no_crash_marker`.
- Scoped markers: `android_runtime_count=0`, `native_signal_count=0`,
  `guest_crash_count=0`.
- Visual report:
  `scratch/thor-debug/20260528-191801-banjo-visual-status.txt`.

Decision: this matches a live black-frame/audio-present class, not a process
crash. Next useful evidence is a focused GPU/presentation packet or game-specific
render-target/frontbuffer probe, not generic crash triage.

## Recent de-dupe

Android recents now de-dupe by normalized title before falling back to exact URI
or target. Existing duplicate saved rows disappear on read, and future inserts
replace old rows with the same normalized title.
