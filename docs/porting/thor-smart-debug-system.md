# Thor Smart Debug System

Created: 2026-05-17 16:31:29 -04:00

## Purpose

Keep AYN Thor debugging fast and evidence-driven while this fork is still a
research experiment. This is not official Xenia infrastructure.

## Build Lanes

Use `tools/thor/thor_build.ps1` instead of hand-running Gradle when iterating on
Android.

```powershell
# Full native plus APK rebuild.
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullApk

# Java/XML/resources/manifest APK packaging only. Uses the previous native lib.
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell

# C++/Vulkan/CPU backend core only. Does not package a new APK.
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore

# Build and install in one command.
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -DeviceSerial c3ca0370 -Mode FullDeploy
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -DeviceSerial c3ca0370 -Mode ApkShellDeploy
```

The script automatically maps the repo to a temporary no-space drive when the
workspace path contains spaces, then runs Gradle from
`android/android_studio_project`.

## Runtime Capture Lane

Use `tools/thor/thor_xenia_debug.ps1` for ADB launch and evidence bundles.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode StopNoise
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchBlueDragon
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode Capture
```

`Capture` writes:

- Full logcat.
- Filtered logcat.
- Screenshot.
- Metadata with branch, commit, APK hash, process id, and focused activity.

Use `StopNoise` before Blue Dragon runs when other emulator packages are
polluting logcat or stealing focus.

## Ghidra Lane

Native ARM64 import:

```powershell
$env:GHIDRA_HOME = "C:\path\to\ghidra"
powershell -ExecutionPolicy Bypass -File tools\thor\ghidra_headless_import.ps1
```

Use this for:

- `libxenia-app.so` native crash stacks.
- JNI and Android bridge analysis.
- Vulkan path analysis.
- ARM64 backend/interpreter/JIT analysis.

Guest PPC analysis is separate. Only use locally owned and legally extracted
guest code, and never commit guest binaries or extracted assets.

## RenderDoc Lane

RenderDoc is for Vulkan frame capture. It is not the default run mode.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode Status
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode EnablePerApp
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode Disable
```

If the installed RenderDoc Android package is not
`org.renderdoc.renderdoccmd.arm64`, pass the actual package:

```powershell
adb shell pm list packages | grep -i renderdoc
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode EnablePerApp -LayerPackage <package>
```

Pair every RenderDoc capture with a normal non-RenderDoc run. Do not report
fork-specific RenderDoc findings to upstream Xenia unless independently
reproduced on official Xenia builds.

## Commit Cadence

- Work on `master` only for this repo unless the user explicitly changes the
  branch policy.
- Commit and push after each validated slice:
  - Script/tooling slice.
  - Android UI slice.
  - ARM64 backend opcode slice.
  - Device capture milestone.
- Each commit should have a dated worklog entry and any needed dated research
  notes.
