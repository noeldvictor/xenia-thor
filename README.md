# xenia-thor

`xenia-thor` is a personal, agentic-AI research fork exploring a native Android
ARM64 Xenia-derived build on AYN Thor / Thor Max.

It is **extremely unstable**.

This is not an official Xenia build, not a compatibility fork, not a release
channel, and not a supportable emulator for normal users. It is a local research
playground for Android ARM64 bring-up, handheld usability, Vulkan debugging,
controller UX, and game-specific failure triage.

## No Support

There is no support promise here.

- Do not ask the official Xenia project for help with this fork.
- Do not report `xenia-thor` crashes to upstream Xenia, Xenia Discord, or other
  emulator communities.
- Do not treat any result here as a game compatibility claim.
- Do not expect builds to work from commit to commit.
- Do not use this as evidence that Xenia supports Android or AYN Thor.

If something breaks here, it probably belongs here.

## Thanks

Huge thanks to the original Xenia developers and contributors. Their years of
research, engineering, documentation, and open-source work made this experiment
possible at all.

- [xenia-project/xenia](https://github.com/xenia-project/xenia)
- [xenia.jp](https://xenia.jp/)

Please respect their time. This fork is not their responsibility.

## Current Focus

Target device:

- AYN Thor / Thor Max
- Android ARM64, `arm64-v8a`
- Snapdragon 8 Gen 2-class / Adreno 740
- Vulkan first

Current product priority:

- Make the Android APK usable on the real Thor.
- Validate game launching through the actual Android app picker.
- Improve controller mapping, OSD, FPS visibility, exit-to-menu, recent-game
  status, and handheld-friendly settings.
- Classify crashes with repeatable proof packets instead of one-off guesses.

Blue Dragon full-speed work is paused unless explicitly restarted. The current
active Android usability state includes launcher/app-picker proof, visual
controller remapping, Back-as-OSD, FPS badge, Exit to menu, recent game status,
internal-resolution setting, and the user-confirmed Project Sylpheed title/menu
geometry fix. Project Sylpheed's remaining known issue is a guest/runtime heap
and `RtlRaiseException` compatibility class, not the old title positioning bug.

## Remote Debugging And Testing

Use the repo-local remote debug stack for AYN Thor work:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Status
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenshot
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenrecord -Seconds 30
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode UiDump
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode CrashBundle
```

Use USB ADB for installs, screenshots, videos, logcat, and bugreports. scrcpy or
Android Studio device mirroring is useful for live control, but durable proof
should still be captured through ADB into `scratch/thor-debug/`.

For a title, menu, input, or crash claim, keep a compact packet:

- branch and commit or dirty-state note,
- APK hash if device-tested,
- launch path and settings/cvars,
- screenshot or short video of the failing or fixed screen,
- full logcat plus focused fatal/crash filter,
- UI dump for launcher/settings/OSD focus bugs,
- bugreport or Perfetto trace only when logcat/screens/video are not enough.

## Common Commands

```powershell
git status --short --branch
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchLauncher
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode Capture
```

## Local Notes

Useful current docs:

- [Agent Instructions](AGENTS.md)
- [PowerShell Command Hygiene](.agents/skills/xenia-windows-powershell-command-hygiene/SKILL.md)
- [Thor Remote Debug Skill](.agents/skills/xenia-thor-remote-debug/SKILL.md)
- [Android Game Launch and Controller Mapping](docs/research/20260527-144000-android-game-launch-crash-and-controller-mapping.md)
- [Android In-Game Menu and Overlay Cleanup](docs/research/20260527-151500-android-ingame-menu-overlay-controller-start.md)
- [Android OSD Exit To Menu](docs/research/20260527-152100-android-osd-exit-to-menu.md)
- [Android Internal Resolution and Project Sylpheed 480p](docs/research/20260527-164500-android-internal-resolution-and-sylpheed-480p.md)
- [Android Recent Games Status List](docs/research/20260527-171500-android-recent-games-status-list.md)
- [Project Sylpheed Title Geometry Fix](docs/research/20260527-184700-project-sylpheed-title-geometry-fix.md)
- [Android Remote Debug Test Rig](docs/research/20260527-190000-android-remote-debug-test-rig.md)

Worklogs live in `docs/worklogs/` and research notes live in `docs/research/`.

## Legal And Content Boundary

This fork is for emulator research and legally owned test content only. Do not
use it for piracy, bypassing access controls, redistributing game content, or
posting copyrighted assets.

Again: experimental fork, no support, no promises.
