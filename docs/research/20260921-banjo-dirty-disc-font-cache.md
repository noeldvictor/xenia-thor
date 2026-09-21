# Banjo dirty-disc on the device: the XUI font-cache redirect, 2026-09-21

## Result

The device "Disc Read Error" in Banjo-Kazooie: Nuts & Bolts (4D5307ED) was not a
backend miscompile. It was the Banjo profile cvar `xam_redirect_xui_font_cache`
(added 2026-06-26 for a white screen). The redirect maps
`GAME:\xuifontcachemeta\metafont_euro.ini` and `GAME:\xuifontcachefont\euro.sbp`
to `cache:\xuifontcachemeta` and `cache:\xuifontcachefont`: the file name is
dropped, so the open fails with `C0000034`. Both files are on the disc. The PC
build never had the redirect and never showed the dialog.

With the redirect removed from the profile the device ran 120 s with no dialog
(`tools/thor/banjo_dialog_check.py 120 noredirect`) and the font-cache opens
resolve on the disc.

## The chain, with the evidence for each step

1. The dialog comes from the request completion pump. The export trap on
   `XamShowDirtyDiscErrorUI` with pause (`tools/thor/guest_trap_context.py`)
   gives the guest chain: `8271C5C8` worker thread entry, `823DFEB4` task loop,
   `8220F3B8` task dispatch (table at `82FAB408`), `82327600` the request loop,
   `826EF618` the completion pump, `82273078` the request's completion callback,
   `82714718` the fatal handler that calls the export.
2. `826EF618` calls the request's `vtable[2]` (poll), stores the DOS status at
   `op+0xC`, skips `3E5`/`3E4` (pending), and on completion calls the callback at
   `request+4` with `r3 = op`, `r4 = request+8`. At the hit: request
   `406C0870 = {vtable 8219D9C4, callback 82273078, context 7018E6A0, op 406C0840}`,
   op `+0xC = 00000002` (ERROR_FILE_NOT_FOUND).
3. The context lives on the main thread's stack (`7018E6A0`, main r1 `7018E630`)
   and holds the resource id `2E07DBDB` at `+0xC`. The main thread waits on it in
   XUI code (`829CE7C0 829CECA0 82A01D3C ...`) with the text
   `aid_xuifont_common_digistrip.ttf` in its frames.
4. The lookup trace (`RtlEnterCriticalSection` trap with `lr=826EFC54`, dump
   `r25:32`) gives 3,671 distinct ids on the device and 3,957 on the PC. The
   device set is a subset of the PC set except one id, the last: `2E07DBDB`.
   So every id before it is right, and the order differences between the two
   traces are thread scheduling, not data.
5. The bundle index (`Bundle\50\685374`: header 0x14 bytes, then 12-byte
   entries `{id, offset, size}`) holds the ids. `013191CC` is the texture
   `banjox_ui_photosandvideos_polaroid`; `01C55D55` is
   `banjox_ui_photosandvideos_xboxlivephotos`. The id is
   `(type << 24) | (~crc32(name) & 0xFFFFFF)` with the `aid_texture_` prefix
   removed. `~crc32("common_digistrip.ttf") & 0xFFFFFF = 07DBDB`. The bundle
   `Bundle\4f\685374` holds `9C07DBDB` (type 0x9C, the TrueType file). The title
   built `2E07DBDB`: type `0x2E`, the character `.`. The runtime path that maps a
   font file name to a resource id does not know `.ttf`; the title never uses it
   when the XUI font cache opens.
6. The font cache did not open because of the redirect (step 0). Without the
   redirect XUI reads the cached glyphs from the disc and never asks for the
   TrueType file.

## Why four months went by

Six commits since 2026-06-07 fixed a real kernel gap each (XCTD info class,
XFileSectorInformation, crypto exports, EOF status, the object-signature
stamp) and each time the residual dialog was declared "the a64 recompiler" with
no direct evidence. The loop had three faults:

- The failing operation was never captured. The dialog was the only signal, one
  bit per five-minute device cycle, and the levers (cvars, backends, opts) were
  sampled one at a time against it.
- The device-only differences were not listed first. The profile cvars
  (`GameProfiles.java`), the `XE_ANDROID_DEFAULT` code defaults, and the launcher
  extras are the whole set of things the PC build does not do. The redirect was
  the first entry in that list.
- The comparison signal was not validated. Per-thread lookup sequences "diverged
  at index 18"; as sets they were identical. Multi-threaded loaders reorder.

## The method that closed it in one hour

1. Trap the export with pause and print the guest chain, the request objects,
   and the other threads' chains and stack text (`guest_trap_context.py`).
2. Read the failing request: its id, its status, who queued it.
3. Compare the id sets, not the sequences, between the PC and the device.
4. Find the id in the game data and crack the id function offline
   (`tools/pc/gdfx_read.py` reads the disc; the bundle table is plain).
5. Ask what the PC never does that the device does: the profile.

## Tools added

- `tools/thor/guest_trap_context.py`: trap, chain, request objects, all threads.
- `tools/thor/guest_disasm.py`: guest disassembly ranges through the in-app server.
- `tools/thor/banjo_dialog_check.py`: one run, dialog time or none, cause lines.
- The `threads` endpoint no longer fails on a host tid outside the int range.
- `XFileSectorInformation` returns the real disc sector for disc image files
  (unique, the same on the PC and the device) and logs a collision on the hash
  fallback. It was not the cause (no collisions in either hash set for the
  1,981 files) but it is what the console returns.
- The trap dump spec accepts a register offset (`r25-24:32`).
