# Burnout Revenge — in-game HARD HANG (present-stall), device-verified 2026-05-30

## Summary (verified this session, numbers seen in tool output)
Burnout Revenge boots, skips intro movies (hid_nop sequence
`start@24000:400;a@28000:400;start@32000:400;a@36000:400`), and reaches a
fully-rendered **in-game 3D scene** (city highway: road, lane barriers/cones,
palm trees, skyscrapers). It then **stops presenting entirely** — a hard hang,
not a low framerate.

## Proof
- Launch: `am start ... EmulatorActivity ... --es target '/storage/2664-21DE/Roms/xbox360/Burnout Revenge (USA).iso'`
  → `Status: ok`, COLD launch. Foreground confirmed
  `topResumedActivity=...jp.xenia.emulator.EmulatorActivity` (NOT the launcher).
- Screenshot: in-game 3D highway scene, OSD reads **0.0 FPS**.
  (scratch/thor-debug/burnout-state/state.png and .../bstate2.png — both exactly
  2556001 bytes, i.e. byte-identical captures taken seconds apart = frozen frame.)
- VdSwap (guest frame-present) timeline, epoch seconds:
  - last VdSwap timestamp: **1780162299.898** (count then plateaued).
  - VDSWAP_COUNT = **1202**, unchanged across NOW = 1780162408, 411, 423, 426, 428.
  - AGE at NOW=1780162423 → **124 s** since last present; at NOW=1780162426 → 127 s.
  → ~127+ seconds with ZERO new VdSwaps while the app is foreground and showing a
  rendered frame = the present pipeline is fully stalled (guest hung).

## ROOT CAUSE (now verified from the xenia log tail, lines quoted)
The hang is an **infinite EOF read-loop on a background video file**, NOT an
exception/GPU stall. The last xenia lines before the present-stall (proc 12433
thread 12459), repeating ~5x/sec:

```
w> F80000FC NtReadFile status: path='\Device\Cdrom0\ovid\BG1_N.xmv'
   handle=F8000084 ... request=131072 requested_offset=93454336
   position_before=93342056 bytes_read=0 status=C0000011
   position_after=93342056 synchronous=false
i> F80000FC xeRtlNtStatusToDosError 103 => 3E5
```
and successive lines with requested_offset = 93585408, 93716480, 93847552,
93978624, 94109696 (each +131072 = +0x20000), ALWAYS bytes_read=0,
status=**C0000011 (STATUS_END_OF_FILE)**, position frozen at **93342056**.

Reading: the game streams the `.xmv` (Xbox Media Video) background movie in 128 KiB
async chunks. It keeps issuing reads at offsets PAST the file's end; each returns
EOF with 0 bytes; the demuxer never treats EOF as "done" and re-requests forever.
The frozen on-screen frame is the last decoded movie frame. (`ovid` = opening/
background video; `BG1_N` ~ background loop.) xeRtlNtStatusToDosError 103=>3E5:
0x3E5 = ERROR_IO_PENDING (consistent with synchronous=false async reads).

## Why this matters across games (cross-compat lever)
`.xmv` background/intro/menu movies are ubiquitous on Xbox 360. If our EOF
semantics for the guest CdRom/STFS file path differ from real hardware (e.g. real
HW returns a short read with bytes_read>0 up to the true end, or the game expects
a specific status), every movie-heavy title can wedge the same way. This is
directly the "skip movies is paramount" problem in engine form.

## OPEN QUESTION (must verify before any fix)
Is the read genuinely past a CORRECT end-of-file (game bug it should self-handle by
looping — then our job is just to not hang), OR did our ISO/file extent reader
report a WRONG (too-small) size so the game reads past a FALSE EOF at 93342056?
NEXT: get the real size of `ovid/BG1_N.xmv` inside the Burnout ISO and compare to
93342056 / the offsets being requested (~94 MB). If the file truly is ~93.3 MB and
the game asks for 94 MB+, it's the game over-reading; if our reader truncated it,
fix the size/extent path. Do NOT edit any file-IO code until this is answered.

## Code path (READ, not edited — xboxkrnl_io.cc is a do-not-touch dirty file)
NtReadFile_entry (src/xenia/kernel/xboxkrnl/xboxkrnl_io.cc:313):
- Line 332 `if (true || file->is_synchronous())` — a debug override forces EVERY
  read through the synchronous branch (the async branch at :397 is stubbed/dead).
- `result = file->Read(...)` returns C0000011 (EOF); io_status_block->status is set
  to C0000011, information = bytes_read = 0.
- Line 390 `if (!file->is_synchronous()) result = X_STATUS_PENDING;` overwrites the
  return to 0x103. That 0x103 is the logged `xeRtlNtStatusToDosError 103 => 3E5`
  (ERROR_IO_PENDING). The event is then Set() at :424.
So the kernel returns PENDING + signals the event + reports EOF in the status
block. That is plausible behavior, NOT an obvious kernel bug. The infinite loop is
on the GUEST side: Burnout's .xmv streamer keeps queuing read-ahead chunks past
EOF and never stops. Whether real HW would also return EOF here (making this a true
game expectation we must satisfy some other way) vs. whether our reported file size
is wrong (false EOF) is STILL the open question — and needs the real file size.

## DO-NOT-TOUCH note
xboxkrnl_io.cc is a pre-existing dirty file per standing instructions; the
`if (true || ...)` override is NOT mine and must not be reverted/committed without
explicit direction. No edit made.

## NOT claimed
No fps figure beyond the OSD's literal 0.0; no fix. Two behavioral claims, both
log-backed: (1) the present pipeline is stalled (frozen VdSwap count + age +
identical-byte screenshots); (2) the proximate cause is an infinite EOF read-loop
on ovid/BG1_N.xmv (quoted C0000011 lines). The size/extent question is OPEN.
