# Project Sylpheed Crash And OSD Fit Check

## Context

The Android usability sprint switched from Blue Dragon speed work to making the
Thor APK comfortable for real game testing. The user asked to try Project
Sylpheed and noted that the game view was not fitting the screen correctly,
with app UI still covering the content.

Target:

`/storage/2664-21DE/roms/xbox360/Project Sylpheed - Arc of Deception (USA, Europe) (En,Ja).iso`

Capture:

`scratch/thor-debug/20260527-161538-*`

## Device Result

Direct launch via `EmulatorActivity` reached Xenia runtime and loaded
`game:\default.xex`.

Metadata:

- Branch: `blue-dragon-route-engine-reset`
- Head at capture: `3301bffbf`
- APK SHA256:
  `4B463AAAB2169C5998C9DB2A6871471240B0394C3FCC55DDFB79832B20B09567`
- Screenshot:
  `scratch/thor-debug/20260527-161538-screenshot.png`
- Log:
  `scratch/thor-debug/20260527-161538-logcat.txt`

The app process did not Android-crash. Xenia displayed its guest crash dialog:

`The guest has crashed. Xenia has now paused itself.`

## Crash Class

This is a guest/runtime compatibility crash, not a launcher picker crash and
not a Java/Android process crash.

Important markers:

- `Launching module game:\default.xex`
- Title ID: `535107D4`
- `BaseHeap::Release failed because address is not a region start`
- `PhysicalHeap::Release failed due to parent heap failure`
- `BaseHeap::Alloc page count too big for requested range`
- `PhysicalHeap::Alloc unable to alloc physical memory in parent heap`
- `RtlRaiseException(702DF8D0(E06D7363))`
- Crash PC: `0x8245BDEC`
- Function: `0x8245BD80-0x8245BE64`

Current interpretation: the title enters guest code, repeatedly touches host
path/file cleanup, then fails in physical heap memory handling and raises a
guest C++ exception. The immediate fix path is a focused Project Sylpheed memory
/ file-lifetime compatibility audit, not Android launcher work.

## Screen Fit / Overlay Finding

The screenshot proved the app UI was still too intrusive in direct game
launches:

- A large title/path card covered the top of the game view.
- A bottom `Xenia Thor Android build` badge covered the lower game area.
- The game SurfaceView itself was fullscreen at `1920x1080`; Android system
  bars were not the visible fit issue.

Patch direction:

- Keep FPS as the only live gameplay overlay by default.
- Hide the title/path card and bottom build badge during gameplay.
- Keep controller-accessible menu, FPS toggle, controller map, settings, and
  exit-to-menu in the OSD menu rather than always-on top/bottom banners.
- Make the Thor debug launch helper default to hidden Android OSD for generic
  direct game launches.

## Next

## Post-Patch Verification

Post-patch capture:

`scratch/thor-debug/20260527-161935-*`

Metadata:

- APK SHA256:
  `C96D2F1073FC2381B48BEE08156E5C89BB7ADA49624DD3D4F0037C3D0F17D2D4`
- `hide_android_osd=true`
- Screenshot:
  `scratch/thor-debug/20260527-161935-screenshot.png`

Result:

- The title/path card is gone.
- The bottom build badge is gone.
- The only live Android overlay is the top-left FPS badge.
- The guest crash reproduces with the same physical heap / C++ exception
  markers: `BaseHeap::Release`, `PhysicalHeap::Release`,
  `BaseHeap::Alloc page count too big`, `PhysicalHeap::Alloc unable`,
  `RtlRaiseException(702DF8D0(E06D7363))`, PC `0x8245BDEC`.

The visible fit complaint for this capture is fixed at the Android overlay
layer. Keep Project Sylpheed as a compatibility crash title and investigate the
physical heap / `RtlRaiseException(E06D7363)` path separately.
