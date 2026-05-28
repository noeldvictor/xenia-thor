# Project Sylpheed XMA Decoder Abort

## Summary

Project Sylpheed now reaches title/menu geometry correctly and no longer aborts
through Android/A64 reenter or invalid `pthread_join`, but the New Game path
exposed a separate audio/XMA process abort.

The direct crash is the XMA decoder thread asserting in
`XmaContext::GetNextFrame`, not the nearby BaseHeap / PhysicalHeap release
warning.

## Evidence

- Pre-fix packet:
  `scratch/thor-debug/remote-debug-20260528-150232`.
- Status classifier output after this slice:
  `classification=project_sylpheed_xma_decoder_native_abort`.
- Native abort:
  `xenia/apu/xma_context.cc:786: XmaContext::GetNextFrame(...): assertion "false" failed`.
- Crash thread: `XMA Decoder (F8)`.
- Heap rows are present in the same run:
  `base_heap_release_count=22`, `physical_heap_count=22`, but they occur before
  the direct abort site and remain a separate compatibility signal.

The repro route was the real Android game library, debug gamepad input, Project
Sylpheed title menu, New Game, Normal, save selection, then the opening
cutscene/audio path.

## Fix

Changed XMA frame-boundary parsing to soft-fail to packet end instead of
asserting:

- `GetNextFrame` now returns `0` for frame lengths smaller than the 15-bit
  length field plus trailing continuation bit.
- `GetNextFrame` returns `0` for truncated or invalid frame lengths without
  `assert_always`.
- `GetFrameNumber` and `GetPacketFrameCount` now guard the same undersized
  frame-length case before subtracting from an unsigned value.
- `GetPacketFrameCount` no longer asserts on `0x7FFF`; it treats it as an
  invalid/truncated packet end.

This matches the fallback the old code already took after the TODO assertion,
but avoids killing Android release/debug APKs.

## Tooling

`tools/thor/thor_android_game_status_report.ps1` now classifies this as
`project_sylpheed_xma_decoder_native_abort` when the native abort message names
`xma_context.cc`, `XmaContext::GetNextFrame`, or XMA.

## Validation

- `git diff --check`: passed with existing CRLF warning text only.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`: passed.
- APK SHA256:
  `DA3CB667C638BA6CA9616BBB862C540FDE3AE9FFE809A18B0E96BD62032E738D`.
- Classifier rerun on the old crash packet reports
  `project_sylpheed_xma_decoder_native_abort`.
- Post-deploy packets
  `scratch/thor-debug/remote-debug-20260528-150754` and
  `scratch/thor-debug/remote-debug-20260528-151705` show Project Sylpheed still
  alive with no scoped XMA/native abort marker.

## Remaining

The exact post-patch New Game cutscene crash proof is blocked by the current
save-selection route staying on the blank save slot in debug input. Use physical
controls, a clean save/storage state, or a focused save-screen input repro to
drive past `SELECT DATA` again.

Audio is still a separate product bug: Android still falls through to
`NopAudioSystem` for output even though the XMA decoder itself is active.
