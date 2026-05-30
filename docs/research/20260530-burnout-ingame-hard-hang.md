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

## Interpretation
This is the real blocker for Burnout being "fast/playable": at this point it is a
**complete hang AFTER reaching the in-game scene**, not a framerate that is merely
low. The earlier user observation (~7-9 fps, black-except-HUD in-race) is a related
collapse; here the captured state is a fully-rendered frame frozen at 0 fps.

The reach-scene library note for burnout says it "eventually [hits an]
RtlRaiseException gameplay path." That is a PLAUSIBLE cause but was NOT confirmed
this session — the post-stall logcat tail did not return in tool output, so the
hang's root cause is **UNVERIFIED**. Do not assume RtlRaiseException without
reading the log lines after timestamp 1780162299.898.

## Next step (concrete)
Read logcat from just after epoch 1780162299.898: look for RtlRaiseException,
an unhandled guest exception, a kernel wait that never returns, a GPU/JIT error,
or a thread going zombie (cf. Lost Odyssey zombie-join). That localizes whether
the hang is CPU/JIT (guest threw + handler looped), kernel-wait, or GPU-submit.
Only then propose a fix.

## NOT claimed
No fps figure beyond the OSD's literal 0.0; no root cause; no fix. The hang itself
is the only behavioral claim, and it is backed by the frozen VdSwap count + age +
identical-byte screenshots above.
