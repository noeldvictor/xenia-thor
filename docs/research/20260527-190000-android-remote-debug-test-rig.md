# Android Remote Debug Test Rig

## Problem

The Android usability sprint is moving quickly across app picker, controller,
OSD, scaling, and game-specific crash classes. The loop was still too dependent
on ad hoc ADB commands and long prompt memory, which caused repeated context
compaction and made fresh user reports harder to capture before state changed.

## Online Baseline

- Android's ADB docs frame `adb` as the command-line bridge for communicating
  with a device, and document `screencap` plus `screenrecord` as built-in
  screenshot/video proof tools.
- Android's logcat docs define logcat as the command-line dump of system and
  app logs; this remains the default crash/fatal evidence source.
- Android bug report docs recommend `adb bugreport` for device diagnostics and
  note that the zip includes dumpsys, dumpstate, logcat, and stack traces.
- Android Studio device mirroring and scrcpy both provide live physical-device
  control/mirroring, but neither should replace durable ADB proof packets.
- Perfetto is the right escalation path for system-wide scheduling, input,
  SurfaceFlinger, FrameTimeline, and present-attribution timing questions.

Primary sources:

- https://developer.android.com/tools/adb
- https://developer.android.com/tools/logcat
- https://developer.android.com/studio/debug/bug-report
- https://developer.android.com/studio/run/device
- https://github.com/Genymobile/scrcpy
- https://perfetto.dev/docs/getting-started/system-tracing

## New Repo Policy

Use `.agents/skills/xenia-thor-remote-debug/SKILL.md` for app-flow, screen,
input, OSD, scaling, and crash repro work. Keep Blue Dragon speed history out of
normal Android usability loops unless explicitly restarted.

When the user says "check now" or reports a fresh crash, capture the current
state before clearing logcat or force-stopping:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenshot
```

For transient controller/title/menu bugs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenrecord -Seconds 30
```

For Android launcher/settings/OSD focus problems:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode UiDump
```

For reproducible app or process crashes that logcat cannot fully explain:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode CrashBundle
```

## Tooling Added

- `.agents/skills/xenia-thor-remote-debug/SKILL.md`
- `tools/thor/thor_android_remote_debug.ps1`

Modes:

- `Status`: ADB devices, device build info, package PID, resumed activity, and
  scrcpy availability.
- `Screenshot`: metadata, `screencap`, full logcat, focused fatal/crash log.
- `Screenrecord`: short MP4, screenshot, full/focused logcat.
- `UiDump`: screenshot, Android UI XML dump, full/focused logcat.
- `CrashBundle`: screenshot, UI XML, logcat, focused log, and `adb bugreport`.
- `Mirror`: launches scrcpy for live control when installed.

## Stop Rules

- Do not claim a game, input, OSD, scaling, or crash fix without a screen/video
  of the exact route plus log evidence.
- Do not use bugreport or Perfetto as the first move for simple UI bugs.
- Do not use scrcpy/Android Studio mirror output as the only proof.
- Do not commit captures or bugreports.

## Next

Use this packet style for the next Project Sylpheed guest heap /
`RtlRaiseException` audit or any new user-reported Android app usability bug.
If a bug is visual, capture the screen first; if it is transient, capture video;
if it is Android UI focus, capture `ui.xml`; if it is a system/process crash,
capture the crash bundle.
