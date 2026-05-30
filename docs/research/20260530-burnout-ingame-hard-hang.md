# Burnout Revenge — present/GPU HANG after movies (CORRECTED), device-verified 2026-05-30

## TL;DR (this supersedes my earlier "EOF loop is the root cause" claim — that was WRONG)
Burnout boots, plays the movie chain, then the **present/GPU pipeline stalls** while
showing a movie/menu-background frame (film-grain vignette = attract/BG video, NOT
gameplay). The infinite EOF read-loop on BG1_N.xmv I previously committed as ROOT
CAUSE is a **red herring**: the present died ~74 s BEFORE that loop started, and an
IDENTICAL EOF loop on the previous movie (CRRW_N.xmv) was survived. Earlier
commits 3715a595b / b78c5a422 over-claimed; this note corrects them.

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

## NEXT STEP (concrete, verifiable, no fabrication)
A/B the UMA flag without rebuilding: relaunch Burnout with
`--ez gpu_uma_direct_shared_memory false` and see if it gets PAST the movie chord
into a responsive menu/gameplay (VdSwap keeps climbing + responds to input). If
OFF fixes it, the UMA direct-write path is the regression. If not, bisect the
other GPU commits. Only then touch code.

## Claims standing (all log/screenshot-backed)
1. Present pipeline is hung (frozen VdSwap + age + unresponsive + sleeping threads).
2. The hang is NOT caused by the BG1_N EOF loop (present died 74 s earlier; CRRW
   survived an identical loop).
3. BG1_N.xmv size is correct (real EOF), not a truncation bug.
NOT claimed: the regression commit (hypothesis only, A/B pending); any fps > 0.
