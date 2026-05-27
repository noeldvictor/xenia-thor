# Android Native Guest Crash Status

## Context

Project Sylpheed title/menu geometry is fixed. The remaining visible failure is
the guest heap / `RtlRaiseException` class, previously classified only by host
logcat tooling. The launcher recent-games UI still showed the coarse
`May have crashed/closed` label because the app had no native guest-crash
signal.

## Change

Added a behavior-neutral native-to-Android status bridge:

- `WindowedAppContext::NotifyGuestCrash(classification, details)`.
- Android JNI dispatch to `WindowedAppActivity.onNativeGuestCrash`.
- `EmulatorActivity` records the diagnostic in `XeniaAndroidSettings`.
- Launcher last-run and recent-game rows now distinguish:
  - `Guest crash`
  - `Guest heap / exception`

For Project Sylpheed, the native guest crash at title ID `535107D4` in
function `0x8245BD80-0x8245BE64` is tagged as
`project_sylpheed_guest_heap_rtlraiseexception`. This is diagnostic UI only;
it does not change emulation behavior, game geometry, heap handling, or crash
policy.

## Validation

- `git diff --check` passed with expected CRLF warnings only.
- `tools/thor/thor_build.ps1 -Mode NativeCore` passed.
- `tools/thor/thor_build.ps1 -Mode ApkShell` passed.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` passed.
- Installed APK SHA256:
  `5653C2E003F9F734328DC1BE1C52E4E5A18252864F71084F7B5AB155F7658472`.
- `tools/thor/thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Status`
  confirmed the device is connected and the app is not currently running.

## Next

Trigger Project Sylpheed through the real app picker and confirm the launcher
recent-game row changes from `May have crashed/closed` to
`Guest heap / exception` when the known guest crash occurs. After that, continue
with the actual heap / `RtlRaiseException` compatibility work.
