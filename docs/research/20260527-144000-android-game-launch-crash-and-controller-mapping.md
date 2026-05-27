# Android Game Launch Crash and Controller Mapping Pass

## Problem

Manual ADB launches were not representative of the user-facing app path. ADB
tests passed raw `/storage/...` game paths, while the Android launcher handed
`content://com.android.externalstorage.documents/...` targets to the emulator.
Several titles also hit assertion-style aborts for unimplemented Xbox kernel
surface area, so user-launched games appeared to "just crash" with no useful UI
explanation.

The controller settings page also lacked a practical way to see the active
Xbox-to-Thor button mapping or remap a button visually.

## Tested Titles

| Title | Capture | Result | Primary evidence |
| --- | --- | --- | --- |
| Wallace & Gromit XBLA Episode 1 | helper launch only | Not a game compatibility result | shell quoting failed on `Gromit's` path before app launch |
| Back to the Future | `scratch/thor-debug/20260527-140427-*` | Hard abort | `NtQueryVolumeInformationFile(... XFileFsDeviceInformation ...)` assertion |
| Burnout Revenge | `scratch/thor-debug/20260527-140704-*` | Stayed alive, loading/black | no focused fatal/assert marker in 60s |
| Project Sylpheed | `scratch/thor-debug/20260527-140847-*` | Stayed alive, black | repeated heap release-from-non-region-start failures |
| Banjo-Kazooie: Nuts & Bolts | `scratch/thor-debug/20260527-141349-*` | Hard abort before fix | `XAudioGetVoiceCategoryVolumeChangeMask` driver pointer assertion |
| Banjo-Kazooie: Nuts & Bolts | `scratch/thor-debug/20260527-142655-*` | Abort avoided after fix | no focused fatal/assert/XAudio/NtQuery hits in 75s, but still black/stuck |

## Crash Classes

1. App picker path mismatch

   The launcher resolved SD-card games through Android document URIs. The native
   launch path expects filesystem paths for the current Android port. Raw ADB
   launch paths used `/storage/2664-21DE/Roms/xbox360/...`, while the app path
   could keep a document URI string. This is a user-facing launch bug, not a
   title-specific emulation result.

2. Unsupported kernel information classes aborting the process

   Back to the Future requested `XFileFsDeviceInformation`. The old path hit an
   assertion. The new path writes a conservative zeroed device-info payload and
   returns success for that class. `XFileXctdCompressionInformation` now also
   returns a minimal success payload because Banjo repeatedly queried it.
   Unknown info classes now return `X_STATUS_INVALID_INFO_CLASS` instead of
   asserting.

3. XAudio invalid-driver-pointer abort

   Banjo reached XAudio render-driver calls with a pointer outside the expected
   `0x4155xxxx` range. The old code asserted and killed the app. The new code
   logs the unexpected pointer and returns `X_ERROR_INVALID_PARAMETER` for the
   affected render-driver entry points instead of hard-aborting.

4. POSIX mapped-memory failure detection

   `mmap` failure is `MAP_FAILED`, not null. The Android memory mapper now checks
   `data == MAP_FAILED`, avoiding false success on failed mappings.

5. Non-fatal compatibility stalls

   Burnout and Project Sylpheed did not show the same assertion-abort pattern in
   the short captures. Burnout remained in loading/black-screen state. Project
   Sylpheed repeatedly failed physical heap releases for non-region-start
   addresses. Those need title-specific compatibility audits, but they are not
   fixed by the crash-abort patch.

## Implemented Fixes

- `XeniaAndroidSettings` now resolves external-storage document URIs into raw
  `/storage/<volume>/<path>` launch targets when possible.
- The launcher default SD-card document path now uses `Roms/xbox360`, matching
  the observed Thor SD-card capitalization.
- `mapped_memory_posix.cc` now treats `MAP_FAILED` as the failure sentinel.
- `xboxkrnl_io_info.cc` now handles `XFileFsDeviceInformation` and
  `XFileXctdCompressionInformation` without asserting.
- `xboxkrnl_audio.cc` now rejects unexpected XAudio render-driver pointers with
  `X_ERROR_INVALID_PARAMETER` instead of process aborts.
- Android settings now has `View / remap controller`.
- `ControllerMappingActivity` adds a landscape visual controller map with
  tappable controls, live selected action, last-input feedback, a scrollable
  button binding list, fixed analog-axis summary, reset, and done actions.
- `XeniaInputMapping` stores button remaps in preferences and remaps Android
  key events before they enter native HID. When a physical button is already
  assigned, remapping swaps the displaced action onto the old physical button so
  two Xbox actions do not silently share the same Thor button.

## Verification

- `git diff --check` passed; only existing CRLF warnings were reported.
- FullDeploy to Thor succeeded after the UI and native changes.
- Controller UI screenshots:
  - `scratch/thor-debug/20260527-controller-ui/mapping-final.png`
  - `scratch/thor-debug/20260527-controller-ui/mapping-selected.png`
  - `scratch/thor-debug/20260527-controller-ui/mapping-remap.png`
- Remap smoke:
  1. Opened Launcher -> Settings -> View / remap controller.
  2. Tapped Xbox `A` on the visual controller.
  3. Sent Android keycode `97` (`BUTTON_B`).
  4. UI updated to `A mapped to B`, preview changed `A` to physical `B`, and
     `Last input: B`.
  5. Reset button restored default mappings.

## Remaining Work

- Banjo no longer hits the XAudio assert in the short post-fix capture, but it
  still does not prove playable progress.
- Burnout and Project Sylpheed need separate compatibility triage because their
  observed symptoms are loading/black-screen or heap-manager issues rather than
  the fixed assertion-abort classes.
- Analog remapping is display-only for now. Button remapping is live.
