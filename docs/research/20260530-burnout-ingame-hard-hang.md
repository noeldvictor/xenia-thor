# Burnout Revenge — present/GPU HANG from UMA direct-write (SOLVED), device-verified 2026-05-30

## TL;DR — ROOT CAUSE FOUND (A/B-confirmed)
The hang is caused by **gpu_uma_direct_shared_memory** (the UMA direct-write
shared-memory path, commit af5d1320a, default ON). With it ON, Burnout's present
pipeline wedges during the movie chain (VdSwap frozen, unresponsive). With it OFF,
Burnout runs ~63 fps straight through the movies to a live menu. This is the
"we used to get through movies" regression — it was introduced by my own UMA commit.

Two earlier wrong theories I committed and now retract:
- "infinite EOF read-loop on BG1_N.xmv is the root cause" (3715a595b/b78c5a422) —
  RED HERRING: present died 74 s before that loop; the identical loop on the prior
  movie CRRW_N.xmv was survived.
- "in-game 3D highway scene" — it's the attract/background MOVIE frame (film-grain
  vignette), not gameplay.

## Movie chain (distinct .xmv files seen, in order)
EA_E_N.xmv -> EAHD_E_N.xmv -> CRRW_N.xmv (Criterion) -> BG1_N.xmv (menu background)
All under \Device\Cdrom0\ovid\ .

## TIMELINE (epoch/clock from logcat, run at proc 11095)
- 14:18:08-09  CRRW_N.xmv past-EOF over-read: position pinned 2261630, requested_offset
  climbs 2359296 -> 3014656 (+0x20000 each), bytes_read=0, status=C0000011 EOF.
  => game CONTINUED past this (presents kept happening).
- 14:18:17.753 / .754  LAST two VdSwap calls. Present STOPS here.
- 14:19:31     BG1_N.xmv past-EOF over-read: position pinned 93342056 (~89.0 MB),
  requested_offset climbs 93454336 -> 94109696, bytes_read=0, status=C0000011 EOF.
  => This starts ~74 s AFTER present already stopped. NOT the trigger.
- 14:29:44-45  Android HID key events = MY manual start/A/dpad/A test inputs.
  VdSwap count stayed 1203 (no new present) => UNRESPONSIVE.

## Proof the present is truly hung (not a quiet menu)
- VdSwap count frozen at 1203; last present epoch 1780165097; still frozen at
  NOW=1780165797 => ~700 s (11.6 min) with zero presents.
- Sent keyevents 108/96/20/96 (start, A, dpad-down, A); waited; VdSwap STILL 1203.
  A live menu would keep re-presenting (count would climb). It does not.
- Two screenshots seconds apart are byte-identical (2556001 B), OSD reads 0.0 FPS.
- ps -T on proc 11095: GPU Commands, GPU VSync, Main XThread, Kernel Dispatch,
  XMA Decoder, Audio Worker all state 'S' (sleeping). Everyone parked = nobody
  driving the present.

## Size question — ANSWERED (not the bug)
disc_image_file.cc:26 ReadSync returns X_STATUS_END_OF_FILE only when
`byte_offset >= entry_->size()` (the REAL extracted file size). The pinned
position 93342056 IS BG1_N.xmv's true end. CRRW_N pinned at 2261630 likewise. So
our reader reports the correct size; the game intentionally read-aheads past EOF
and the kernel correctly returns EOF. No truncation / false-EOF bug.

## Kernel read path (READ-only; xboxkrnl_io.cc is a do-not-touch dirty file)
NtReadFile_entry (xboxkrnl_io.cc:313): line 332 `if (true || is_synchronous())`
forces the sync branch; file->Read returns C0000011 (info=0); line 390 overwrites
the return to X_STATUS_PENDING(0x103) for async files (= the logged
`xeRtlNtStatusToDosError 103 => 3E5` ERROR_IO_PENDING); event Set at :424. This is
plausible and is NOT where the hang is. No edit made.

## What is actually wrong (hypothesis, NOT yet proven)
The present/GPU pipeline stops mid-movie/at the menu background. Because "we used
to get through movies to in-game," this is most likely a REGRESSION in a recent
GPU/present change, prime suspects (git log on gpu/vulkan + ui/vulkan):
- af5d1320a  GPU/Vulkan: UMA direct-write shared memory (gpu_uma_direct_shared_memory)
- eb45ad643  VdSwap/GPU presenter tracing
- ce90899b9 / 73c0d8059  shared-memory perf counters
Video frames are decoded into guest memory then uploaded to the GPU; a regression
in the UMA shared-memory upload path could wedge present specifically on
video/large-upload frames while static menus elsewhere still work.

## A/B RESULT — CONFIRMED: gpu_uma_direct_shared_memory is the regression
Relaunched Burnout identically but with `--ez gpu_uma_direct_shared_memory false`.
Device-measured this run (UMA OFF):
- VdSwap count climbed continuously: 4303 -> 6672 -> 8927 -> 12387 -> 13142.
  Rate 12387->13142 in 12 s = ~63 presents/sec (full speed).
- Got PAST the movie chain: BG1_N.xmv reached AND the game moved on to a live
  menu/title-transition frame (screenshot burnout-uma-off.png, 2636159 B — DIFFERENT
  from the frozen 2556001 B hang frame).
- Foreground = EmulatorActivity; presenting and responsive.

Contrast — UMA ON (default, the hang): VdSwap frozen at 1203 for 700+ s,
unresponsive to injected input, all GPU/Main threads sleeping.

=> The unified-memory direct-write shared-memory path (commit af5d1320a,
gpu_uma_direct_shared_memory, default ON) WEDGES the present pipeline on Burnout.
With it OFF the title runs at ~60 fps through the movies. This is exactly the
"we used to get through movies to in-game" regression.

## Claims standing (all log/screenshot-backed, device-measured)
1. UMA ON: present pipeline hangs (frozen VdSwap 1203, 700+s, unresponsive, threads
   sleeping).
2. UMA OFF: present runs ~63 fps, climbs past the movie chain to a live menu frame.
3. The hang is NOT the BG1_N EOF loop (present died 74 s earlier; CRRW survived an
   identical loop) and NOT a file-size bug (disc_image_file.cc:26 EOF at real size).
4. Therefore gpu_uma_direct_shared_memory (af5d1320a) is the regression.

## RECOMMENDED FIX DIRECTION (not yet applied)
Short term / per-game: ship gpu_uma_direct_shared_memory=false in the Burnout
profile (thor_profiles.ps1) and the launch path. Cross-game: since UMA direct-write
is supposed to be the big Thor perf win, the real fix is to find WHY direct-write
wedges present (likely a missing flush/barrier or host-visible-coherent assumption
on Adreno for the swap/upload of large video frames) so it can be re-enabled safely.
Investigate src/xenia/gpu/vulkan/vulkan_shared_memory.cc direct-write path +
vulkan_presenter interaction. Default-OFF until fixed.
NOT claimed: a code-level fix (path identified, not yet diagnosed/edited).
