---
name: xenia-thor-remote-debug
description: Use for remote ADB, scrcpy or Android Studio mirroring, screenshots, videos, UI dumps, bugreports, Perfetto/FrameTimeline triage, and AYN Thor app-flow validation for the xenia-thor Android APK.
---

# Xenia Thor Remote Debug

Use this when the user reports "look at the screen", app-picker crashes, bad
controller behavior, black screens, bad scaling, OSD problems, or game-specific
Android failures. The goal is a repeatable proof packet, not a memory-heavy chat
history.

## Default Stack

- Use USB ADB for install, log capture, and first authorization.
- Use scrcpy or Android Studio device mirroring for live control, but keep ADB
  screenshots/videos/logcat as the durable proof.
- Use the real Android launcher/app picker for UX and compatibility claims.
  Direct `EmulatorActivity` launches are only for cvar diagnostics.
- Prefer still screenshot + focused logcat first; add video for transient UI or
  input problems; add bugreport or Perfetto only when simpler proof cannot
  explain the crash, scheduler, present, or frame-pacing issue.

## Session Packet

Create one timestamped directory under `scratch/thor-debug/` containing:

- `meta.txt`: branch, commit/dirty state, APK hash if known, device serial,
  launch path, settings/cvars, and exact repro steps.
- `screen.png` or short `screen.mp4`.
- full `logcat.txt` plus a focused fatal/crash filter.
- `ui.xml` when Android view focus, button reachability, or launcher layout is
  part of the bug.
- `bugreport*.zip` only for reproducible app/process/system crashes where
  logcat is not enough.
- `*.perfetto-trace` only for scheduler, input, SurfaceFlinger, FrameTimeline,
  or present-attribution questions.

## Repo Helper

Prefer the repo wrapper for quick packets:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Status
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenshot
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenrecord -Seconds 30
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode UiDump
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode CrashBundle
```

Use `-Mode Mirror` only for interactive live control. Do not treat the mirror
window itself as the only evidence.

## Tool Choice

- `screencap`: current-screen truth; use it immediately when the user says
  "check now" or describes a visual issue.
- `screenrecord`: title/menu transitions, controller input, OSD navigation,
  scaling bugs, and crashes that happen after a button press.
- `uiautomator dump`: launcher/settings/OSD focus and accessibility-style UI
  state. It will not explain native game rendering.
- `logcat`: always collect after a crash; clear before controlled repros, but
  do not clear if the user just reported a fresh crash.
- `adb bugreport`: slower, high-value crash bundle after a reproducible failure.
- Perfetto/FrameTimeline: only after logcat/screens/video show a pacing or
  scheduling question that needs system-wide timing.
- RenderDoc/AGI: Vulkan correctness questions, not launcher/controller bugs.

## Stop Rules

- Do not claim a game, input, OSD, or scaling bug is fixed without a device
  screenshot or video of the exact failing screen/route.
- Do not send fork-specific crash data upstream.
- Do not commit captures, bugreports, game paths with private data, dumps,
  game files, or keys.
- If the next message would be long, put details in a dated research note and
  keep the chat summary short.
