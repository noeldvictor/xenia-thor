# Game Library Speed/Quality Sweep (AYN Thor c3ca0370)

## Status

Device-confirmed. Automated sweep of the installed library, each title launched
fresh with Start/A intro-movie skipping, then screenshot + guest-swap FPS +
near-black + crash classification.

- Tool: `tools/thor/thor_game_speed_quality_pass.ps1`
- Packet: `scratch/thor-debug/game-pass-all-20260529-184405/`
- APK at sweep time: `45B0DBCB...` (later A/B used `B7C4A17F...`).

## Method

Per title: `am force-stop`, `logcat -c`, `am start` with `--es target <iso>`,
wait for boot, tap keyevent 108 (Start) + 96 (A) across a window to skip
publisher logos / intro movies, settle, `screencap`, `logcat -d`. FPS is
computed from VdSwap timestamps over the last 5s (the on-screen badge samples
instantaneously and is unreliable). Quality judged from the screenshot
(near-black share, OSD corner ignored) confirmed by eye.

## Results (8 plain ISOs; 4 .m3u RPGs not yet swept)

| Game | title | FPS~ | rendered? | verdict |
|------|-------|------|-----------|---------|
| Back to the Future 30th | 443607D6 | 0 | no | native crash at boot |
| Banjo-Kazooie: Nuts & Bolts | 4D5307ED | ~30 | yes | healthy / rendering (NOT dirty disc) |
| Burnout Revenge | 454107DC | ~60 | yes | in-game; later RtlRaiseException gameplay crash |
| Gears of War | 4D5307D5 | ~30 | yes | rendered then native crash |
| Gears of War: Judgment | 4D530A26 | ~28 | yes | rendered then native crash |
| Gears of War 2 | 4D53082D | ~30 | yes | rendered ~30fps then black, no crash markers |
| Gears of War 3 | 4D5308AB | ~30 | yes (near_black 0.16) | rendering + native crash markers |
| Project Sylpheed | 535107D4 | 0 | no | guest CRASH DUMP PC 0x8245BDEC (known heap class) |

User confirmed Banjo is not a dirty-disc case; the prior dirty-disc note is
stale. Burnout reaches real gameplay (city highway scene renders clean at 60fps).

## Crash taxonomy (corrected)

The initial "everything crashed" read was a regex artifact: `RtlRaiseException`
is emitted routinely by guest code and is usually NON-fatal (counts: Back to the
Future 24, Gears: Judgment 9, Gears 3 7, Gears 1 5, others 1-2). Authoritative
fatal signals:

- Xenia guest CRASH DUMP (`==== CRASH DUMP ====`): only Project Sylpheed
  (PC 0x8245BDEC).
- Native abort (`#00 abort` -> `#01 __assert2`): Back to the Future, Gears 1,
  Gears 3, Gears: Judgment. These are failed C++ `assert()`s in
  `libxenia-app.so`, not memory corruption -> very fixable.

## Overlap (high-leverage)

The native crashes are the shared cluster:

- **Gears 3 == Gears: Judgment**: identical crash PCs
  `0xdcb6b4 / 0xdcaa6c / 0xdc9d40 / 0x9e9f34` (both UE3) -> one fix covers both.
- Gears of War (1): sibling assert at `0xeffb10...`.
- Back to the Future: another assert site at `0x1073cf8...`, fires at boot.

## Next

- Symbolize the libxenia-app.so offsets (addr2line vs the unstripped .so) to find
  which assertion fires; fix root cause to unblock the Gears cluster.
- Sweep the 4 `.m3u` RPGs (Blue Dragon, Infinite Undiscovery, Lost Odyssey,
  Magna Carta 2) that the discovery glob missed.
