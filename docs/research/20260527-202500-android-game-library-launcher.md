# Android Game Library Launcher

## Context

The Android launcher still felt like a file picker wrapper. The user asked for
an emulator-style library closer to Dolphin, and also reported no audio plus a
Project Sylpheed crash after selecting New Game.

## Change

- Added a launcher game library:
  - `Scan Games` scans common Xbox 360 ROM paths, including
    `/storage/2664-21DE/Roms/xbox360`.
  - `Add Game Folder` persists a Storage Access Framework folder grant and
    scans that tree.
  - `Launch Game` remains as a single-file picker fallback.
  - Library rows launch directly from the scanned URI/path and keep readable
    titles in last-game and recent-game state.
- Changed Android defaults from `apu=nop` to `apu=any` and migrated old default
  `nop` preferences to Auto audio with mute disabled. Manual Disabled remains
  available in Settings.
- Updated the host status classifier so the Project Sylpheed New Game abort is
  tagged as `project_sylpheed_heap_native_abort` when BaseHeap/PhysicalHeap
  failures align with native SIGABRT but no guest exception callback is present.

## Device Proof

- Deployed to AYN Thor with `tools/thor/thor_build.ps1 -Mode FullDeploy`.
- Pressed the focused `Scan Games` card and captured:
  - Top launcher controls:
    `scratch/thor-debug/remote-debug-20260527-202313/screen.png`.
  - Scanned library list:
    `scratch/thor-debug/remote-debug-20260527-202251/screen.png`.
- The launcher found 19 games from `/storage/2664-21DE/Roms/xbox360`, including
  Project Sylpheed, Blue Dragon discs, Lost Odyssey discs, and others.
- Device preferences now show `apu_driver=any` and `mute_audio=false`.
- Running the classifier on the user's latest New Game crash packet reports:
  `classification=project_sylpheed_heap_native_abort`.

## Validation

- `git diff --check` passed with expected CRLF warnings only.
- `tools/thor/thor_build.ps1 -Mode ApkShell` passed.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` passed.
- `tools/thor/thor_android_game_status_report.ps1` on
  `scratch/thor-debug/20260527-201400-user-new-game-crash-current` reports the
  Project Sylpheed heap native abort class.

## Next

Use the game library for normal UX testing. Project Sylpheed title/menu geometry
is still user-confirmed fixed; the remaining New Game issue is the native
BaseHeap/PhysicalHeap abort compatibility class.
