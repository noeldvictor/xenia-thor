# Project Sylpheed Reenter Repro ADB Blocker

## Summary

The next planned proof was a Project Sylpheed game-library New Game/default
difficulty repro with `xboxkrnl_reenter_audit` enabled. The APK build portion
completed, but deployment could not proceed because ADB lost the AYN Thor
transport.

## Attempt

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Result:

```text
BUILD SUCCESSFUL
adb: failed to run abb_exec. Error: closed
adb: retrieving the default device installation mode failed: closed
adb: failed to run abb_exec. Error: device 'c3ca0370' not found
adb.exe: connect error for write: device 'c3ca0370' not found
```

Recovery check:

```powershell
adb kill-server
adb start-server
adb devices
```

`adb devices` returned an empty device list after the restart.

## Decision

Do not clear logcat, force-stop, or attempt the Sylpheed repro while the device
is absent. The reenter audit patch is already committed and pushed at
`1fbc048a6`; the next useful slice is simply to reconnect/authorize Thor, deploy
current branch, and run the game-library repro with:

```powershell
-XboxkrnlReenterAudit true -XboxkrnlReenterAuditBudget 128
```

Then capture scoped screen/log/status rows before designing any non-throw
reenter compatibility path.
