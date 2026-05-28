# Android library frontend and cover art

## Context

The launcher worked, but still felt too much like a utility list. The requested
direction was closer to PPSSPP/Dolphin: library first, controller-friendly game
tiles, and visible box art.

## Changes

- Reworked `activity_launcher.xml` into a library-first frontend:
  - large header and emulator-style top tabs,
  - main game library column,
  - right-side action rail for Scan, Add folder, Browse, Last game, Settings,
    GPU trace, and Window demo.
- Reworked launcher game rows into taller cover-style tiles.
- Added `XeniaCoverArt` with default database:
  `https://raw.githubusercontent.com/xenia-manager/x360db/main/games.json`.
- Cover lookup matches title IDs when present, then normalized title names.
- Cover database JSON is cached in the app cache for seven days; downloaded
  cover images are cached under `cache/cover-art`.
- Added a network security config allowing cleartext only for
  `download.xbox.com`, because the database cover URLs point to Xbox HTTP art.
- Updated the PowerShell hygiene skill with the fast UI loop:
  `tools\thor\thor_build.ps1 -Mode ApkShell -NoSubst`.

## Proof

Build/deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode ApkShellDeploy -NoSubst -DeviceSerial c3ca0370
```

Result: `BUILD SUCCESSFUL`, install `Success`.

Device screenshots:

- Initial frontend proof:
  `scratch/thor-debug/remote-debug-20260528-183628/screen.png`
- Cover-art proof after cache/download:
  `scratch/thor-debug/remote-debug-20260528-184134/screen.png`
- Dense shelf proof with smaller chrome, four visible cover tiles, and
  multi-disc badges:
  `scratch/thor-debug/remote-debug-20260528-184731/screen.png`

On-device cache proof:

- `cache/x360db-games.json` exists in the app cache.
- `cache/cover-art` contains downloaded Xbox cover JPEGs, including the visible
  Back to the Future and Banjo-Kazooie rows.

## Decision

Use the GitHub-backed Xenia Manager Xbox 360 database as the default online
cover source. ISO/XEX embedded art extraction remains a possible fallback, but
the online source is enough to make the game library feel like an emulator
frontend immediately.

The first PPSSPP-style pass still wasted space with large row cards. The current
version uses a compact four-column shelf and surfaces disc-set entries directly
with `Disc 1`, `Disc 2`, etc. badges on each tile.
