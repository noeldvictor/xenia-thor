---
name: xenia-blue-dragon-route-capture
description: Use for repeatable Blue Dragon Disc 1 routing on AYN Thor, including default SD-card path, title/opening route proof, nop HID START/A sequences, screenshot/log capture, trace-light versus speed captures, and progress evidence without committing game content or private screenshots.
---

# Xenia Blue Dragon Route Capture

Use this skill when the task is to load Blue Dragon, prove how far it gets,
capture a screenshot/log, or compare whether a change preserved the route to
title, language select, or opening scene.

## Target

Default legal local path on the user's Thor SD card:

```text
/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso
```

Do not commit game data, extracted XEX files, ISO contents, saves, keys,
private screenshots, or raw device logs that may contain personal data.

## Route Modes

For current speed work, prefer the quiet speed route. It hides the Android OSD,
uses the known START/A nop HID sequence, enables the signed 2_10_10_10 fallback,
and disables noisy traces:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 100 -PerfSampleSeconds "45,90"
```

For trace-light visual proof of title/opening route:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonLiveCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 150 -HideAndroidOsd true -HidNopConnected true -HidNopButtonSequence "start@45000:3000;a@68000:3000;a@86000:3000;a@106000:3000" -GpuTraceSwap true -GpuTracePacketBudget 260 -GpuInterruptOnRingIdle false -GpuBlueDragonKickWaitToken false -VulkanForceSigned2101010UnormFallback true -VulkanForce2101010Rgba8Fallback false
```

For a quick still after the app is already running:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode Capture -DeviceSerial c3ca0370
```

## Evidence Search

After a capture, classify route progress:

```powershell
rg -n "Blue Dragon|VdSwap|PM4_XE_SWAP|Vulkan IssueSwap|hid_nop|Voice Language|DbgPrint|GPU is hung|guest crash|Fatal signal|AndroidRuntime|A64 speed profile" scratch\thor-debug
```

Use screenshots to verify visible route milestones, but keep the written result
short and artifact-path based.

## Route Labels

Use stable labels in worklogs and research:

- `boot-only`
- `title-prompt`
- `language-select`
- `opening-scene`
- `slow-opening`
- `crash-before-title`
- `black-screen-with-swaps`
- `trace-contaminated`

## Acceptance

Every route claim needs:

- capture directory;
- commit hash or dirty-state note;
- APK hash if installed;
- launch mode and special cvars;
- screenshot path;
- route label;
- whether the run was speed-clean or trace-heavy;
- next missing milestone.
