# Banjo-Kazooie: Nuts & Bolts File Status Dirty Disc

## Summary

Banjo-Kazooie: Nuts & Bolts now has a focused Android/Thor dirty-disc proof
instead of a generic black-screen classification. The title launches, renders
through Vulkan, and then raises `XamShowDirtyDiscErrorUI`.

## Build

- Branch: `blue-dragon-route-engine-reset`
- Base commit at capture time: `c1a236af8`
- Dirty files: file-I/O status logging probe plus Thor launch helper plumbing.
- APK: `android/android_studio_project/app/build/outputs/apk/github/debug/app-github-debug.apk`
- APK SHA256: `A2306E04D185351877E97949F6149A1A66FF7E886509CD069BD0FB0F4DF0740E`

## Proof

- Packet: `scratch/thor-debug/remote-debug-20260529-banjo-file-status-final`
- Screenshot: `screen.png`
- Logcat: `logcat.txt`
- Status report: `status-report.txt`
- Device: `c3ca0370` / AYN Thor / Android SDK 33

Launch command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchEmulator -Target "/storage/2664-21DE/Roms/xbox360/Banjo-Kazooie - Nuts & Bolts (USA) (En,Ja,Fr,De,Es,It,Nl,Sv,No,Zh,Ko,Pl,Ru,Cs).iso" -XboxkrnlFileIoTrace true -XboxkrnlFileIoTraceBudget 3000 -XboxkrnlFileIoStatusLogBudget 512 -XboxkrnlNtCreateFileFailLogBudget 1024 -GpuTraceSwap true -GpuTracePacketBudget 20
```

Capture command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenshot -OutDir scratch\thor-debug\remote-debug-20260529-banjo-file-status -LogcatTailLines 200000
```

## Findings

- `status-report.txt` classifies the run as `launched_no_crash_marker`.
- Title ID: `4D5307ED`; media ID: `3E567DFF`.
- No Android runtime crash, native signal, guest crash marker, base heap release,
  physical heap release, or invalid pthread join marker was reported.
- The screenshot shows Xenia's `Disc Read Error` dialog at `0.0 FPS`.
- The captured log has `270` `Vulkan guest output callback submitted` rows and
  `540` `XE_SWAP` rows. Rendering is active before/while the dirty-disc dialog
  is shown, so this is not the older plain black-screen class.
- The only new file-I/O status row before dirty-disc is:
  `NtQueryFullAttributesFile status: path='GAME:\loctext\englishus\' status=C000000F missing=1`.
- The matching DOS mapping follows immediately:
  `xeRtlNtStatusToDosError C000000F => 2`.
- `XamShowDirtyDiscErrorUI: user_index=0` is logged `0.725s` after the missing
  `GAME:\loctext\englishus\` query.
- Later `C0000001 => 1F` mappings occur after dirty-disc teardown begins and
  remain secondary.

## Code Probe

Added default-off full file-I/O tracing plumbing for:

- `NtCreateFile`
- `NtReadFile`
- `NtReadFileScatter`
- `NtQueryFullAttributesFile`
- `NtQueryDirectoryFile`

The final proof relies on the budgeted non-success file-I/O status logger that
is on by default, following the existing default `NtCreateFile` failure logging
pattern. This is behavior-neutral and only records path/status context.

## Validation

```powershell
$errors = $null
[void][System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'tools\thor\thor_xenia_debug.ps1'), [ref]$null, [ref]$errors)
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell -NoSubst
adb -s c3ca0370 install -r android\android_studio_project\app\build\outputs\apk\github\debug\app-github-debug.apk
```

`FullDeploy` was attempted first, but Gradle hit the known subst-path
`:app:mergeGithubDebugResources` `!directory.isDirectory` failure. The validated
path was `NativeCore` plus `ApkShell -NoSubst` and manual install.

## Next

The next useful slice is to decide whether `GAME:\loctext\englishus\` should
resolve to an existing localized asset, a directory alias, or a more accurate
`NtQueryFullAttributesFile` status for missing optional localization paths.
Keep this behavior-neutral until a second proof shows whether suppressing or
remapping that status moves Banjo past the dirty-disc dialog.
