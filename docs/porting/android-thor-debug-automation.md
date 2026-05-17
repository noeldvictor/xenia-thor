# Android Thor Debug Automation

Created: 2026-05-17 15:05:00 -04:00

## Purpose

Make Thor debugging repeatable enough that every failed launch produces useful
evidence for the next ARM64/JIT/Vulkan step.

## Known Legal Test Target

Blue Dragon Disc 1 is on the connected Thor SD card at:

```text
/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso
```

This is a local user-owned test path. Do not commit game content, extracted
files, keys, or copyrighted screenshots.

## ADB Loop

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode DeviceInfo
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode FindContent
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode Install
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode LaunchBlueDragon
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -DeviceSerial c3ca0370 -Mode Capture
```

`LaunchBlueDragon` discovers Blue Dragon candidates under `/sdcard/roms/xbox360`
and `/storage/*/roms/xbox360`, then launches the first match with:

- `gpu=vulkan`
- `cpu=arm64`
- `apu=nop`
- `hid=nop`
- `discord=false`

Until the AArch64 backend exists, the useful success condition is a clean log
that reaches the explicit ARM64 JIT-not-implemented error.

## Evidence Bundle

Every on-device launch attempt should record:

- Branch name.
- APK SHA256.
- Exact target path or content URI.
- Full logcat.
- Filtered logcat.
- Screenshot.
- Device serial and model.
- Expected result versus actual result.

Helper output lands in `scratch/thor-debug/`.

## Build Split

Use `tools/thor/thor_build.ps1` so rebuilds match the thing being tested:

```powershell
# Native C++/Vulkan/ARM64 backend core only.
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore

# Android Java/XML/resources/manifest shell only, using the previous native lib.
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShell

# Full native plus APK package.
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullApk
```

See `docs/porting/thor-smart-debug-system.md` for the full lane split.

## Ghidra Loop

Use Ghidra in two separate tracks.

Native Android ARM64:

- Import the unstripped `libxenia-app.so` from:
  `android/android_studio_project/app/build/intermediates/ndkBuild/githubDebug/obj/local/arm64-v8a/libxenia-app.so`.
- Use this for JNI, Vulkan, ARM64 backend, content URI handling, and crash stack analysis.
- Prefer symbolized local builds over stripped APK libraries.

Guest Xbox 360 PPC:

- Only analyze legally owned, locally extracted/decrypted guest code.
- Start from Xenia logs, XEX metadata, entry points, import thunks, and PPC addresses implicated by the emulator.
- Do not commit extracted XEX files or game assets.

Headless import wrapper:

```powershell
$env:GHIDRA_HOME = "C:\path\to\ghidra"
powershell -ExecutionPolicy Bypass -File tools\thor\ghidra_headless_import.ps1
```

If `GHIDRA_HOME` is not set, pass `-GhidraHome` or `-AnalyzeHeadless`.

## RenderDoc Loop

RenderDoc is only for Vulkan frame captures. Keep it off for CPU/backend
debugging unless the specific question is graphics-related.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode Status
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode EnablePerApp
powershell -ExecutionPolicy Bypass -File tools\thor\thor_renderdoc.ps1 -DeviceSerial c3ca0370 -Mode Disable
```

Check the actual installed RenderDoc layer package before enabling:

```powershell
adb shell pm list packages | grep -i renderdoc
```
