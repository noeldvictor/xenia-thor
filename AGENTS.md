# Agent Instructions

## Mission

Port this Xenia fork to native Android ARM64 for AYN Thor while keeping the
app usable as a handheld emulator. This is a local research fork, not official
Xenia. Do not send fork-specific crashes, traces, compatibility claims, or
half-built ARM64 questions upstream unless independently reproduced upstream.

## Current Focus

- Product priority: Android usability and compatibility on AYN Thor.
- Blue Dragon full-speed work is paused unless the user explicitly restarts it.
- Project Sylpheed title/menu geometry is fixed and user-confirmed. Do not
  reopen the old lower-right quadrant lane without fresh title/menu evidence.
- Do not treat the Square Enix movie logo as title/menu proof for future
  regressions.
- Current crash class for Project Sylpheed is guest/runtime heap failure with
  `BaseHeap`, `PhysicalHeap`, and `RtlRaiseException`, not an Android Java crash.
- Keep `-Arm64OffsetMemoryAddressFastpath true` for future Blue Dragon speed
  captures only; it remains globally default-off.

## Work Style

- Be concise. Put long evidence in dated research notes, not prompts.
- Every behavioral claim needs device proof: screenshot/video, logcat, build or
  APK hash, cvars/settings, and a reproducible path.
- Prefer one clear next slice over carrying long historical lane lists.
- If a lane is closed, cite the dated research note instead of repeating the
  entire closure in chat or automation prompts.
- Do not claim a game is fixed until the actual failing screen or route is
  shown working.

## Required Context

For command-heavy Android work, read:

- `.agents/skills/xenia-windows-powershell-command-hygiene/SKILL.md`
- `.agents/skills/xenia-thor-remote-debug/SKILL.md`
- `C:\Users\leanerdesigner\.codex\skills\ayn-thor-xenia-debug\SKILL.md`
- `scratch/thor-debug/codex-goal-loop.json` if present
- the one most relevant dated research note for the active bug

Avoid loading old Blue Dragon/A64 chronology unless the user restarts speed
work or the active task directly needs it.

## Windows And Tools

- This workspace normally runs in Windows PowerShell 5.1.
- Do not use Bash chaining such as `&&` or `||`.
- Prefer `rg` for searches and `multi_tool_use.parallel` for independent reads.
- Use `apply_patch` for repo edits.
- Use PowerShell-native path handling with `-LiteralPath` for paths with spaces.
- Before long build/deploy/capture/git work, check for active overlapping work.

## Remote Debugging

- Use USB ADB for installs, logcat, screenshots, and bugreports; use wireless
  only as a convenience after USB authorization is stable.
- Use scrcpy or Android Studio device mirroring for live control, but keep ADB
  `screencap`, `screenrecord`, `logcat`, and metadata as the proof packet.
- When the user says "check now" or reports a fresh crash, capture the current
  screen and log before force-stopping or clearing logcat.
- Use `adb bugreport` only for reproducible crash/system issues; use Perfetto
  only for scheduler, input, SurfaceFlinger, FrameTimeline, or present-attribution
  questions that screenshots/logcat cannot answer.

## Git

- Work on the current branch unless the user explicitly changes branch policy.
- Do not create feature branches unless asked.
- Commit and push validated progress often.
- Do not revert user changes or unrelated dirty work.
- Each commit should have a dated worklog entry; research-driven changes also
  need a dated research note.

## Documentation

- Research notes: `docs/research/YYYYMMDD-HHMMSS-topic.md`.
- Worklogs: `docs/worklogs/YYYYMMDD.md`.
- Worklogs should name commands, proof paths, findings, blockers, and next
  actions, but stay compact.
- Keep automation prompts short and point to repo notes instead of embedding
  long history.

## Target

- Device: AYN Thor / Thor Max.
- OS: native Android.
- ABI: `arm64-v8a`.
- SoC/GPU: Snapdragon 8 Gen 2-class / Adreno 740.
- Graphics: Vulkan first.
- CPU: PowerPC guest code must run through AArch64 paths, not x64.
- Legal content only; never commit game files, keys, private data, or dumps.

## Common Commands

```powershell
git status --short --branch
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode Capture
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Status
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenshot
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenrecord -Seconds 30
```

## Useful Current Notes

- Android launch/controller/OSD status starts around
  `docs/research/20260527-144000-android-game-launch-crash-and-controller-mapping.md`.
- Recent games/status UI:
  `docs/research/20260527-171500-android-recent-games-status-list.md`.
- Project Sylpheed title geometry fix:
  `docs/research/20260527-184700-project-sylpheed-title-geometry-fix.md`.
- Remote debugging/testing reset:
  `docs/research/20260527-190000-android-remote-debug-test-rig.md`.
- Blue Dragon route-engine reset, if restarted:
  `docs/research/20260527-132500-blue-dragon-present-jank-attribution.md`.
