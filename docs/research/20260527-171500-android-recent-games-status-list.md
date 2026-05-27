# Android Recent Games Status List

## Context

The prior launcher status patch showed only one last-run line. That helped
distinguish a likely crash/forced close from an intentional `Exit to menu`, but
it still lost context when testing several titles back-to-back. The Android
usability lane needs a per-title status surface so the APK can preserve what
happened to Project Sylpheed, Burnout, and other legally owned test games while
compatibility work continues.

## Change

- Added a persisted recent-games JSON list in `XeniaAndroidSettings`.
- The list keeps up to five entries with:
  - launch URI
  - display title
  - resolved target path
  - last state
  - start timestamp
  - finish timestamp
- `recordLaunchStarted` now inserts or updates the recent-game row as
  `running`.
- `recordLaunchExitedToMenu` updates the matching recent-game row as
  `exited_to_menu`.
- The launcher now shows a `Recent games` section below the last-run chip.
- Each recent row is focusable/clickable and relaunches the stored URI/path.
- The row text exposes conservative per-title status:
  - `Exited to menu`
  - `May have crashed/closed`
  - `Unknown`

The UI still avoids claiming root cause from inside the APK. Host-side capture
reports remain the source of truth for compatibility classes.

## Device Proof

Proof directory:

`scratch/thor-debug/20260527-recent-games-proof/`

Artifacts:

- `recent-games-status.png`
- `recent-games-status-scrolled.png`
- `recent-games-status.xml`

The controlled proof used two short launches:

- Project Sylpheed:
  `/storage/2664-21DE/roms/xbox360/Project Sylpheed - Arc of Deception (USA, Europe) (En,Ja).iso`
- Burnout Revenge:
  `/storage/2664-21DE/roms/xbox360/Burnout Revenge (USA).iso`

Final visible launcher state:

- `Last run: exited to menu - Project Sylpheed ...`
- Recent row:
  `Project Sylpheed ... - Exited to menu`
- Recent row:
  `Burnout Revenge (USA).iso - May have crashed/closed`

## Validation

- `tools/thor/thor_build.ps1 -Mode ApkShell` passed.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370` passed.
- Device screenshots and UIAutomator XML proved the list content after deploy.

## Remaining Work

- Add host-side crash/stall classifiers to feed better row labels from capture
  reports.
- Project Sylpheed remains the next focused compatibility lane: it still logs
  physical-heap release failures and `RtlRaiseException`.
- Burnout remains a loading/stall class until a longer app-picker capture
  proves a specific blocker.
