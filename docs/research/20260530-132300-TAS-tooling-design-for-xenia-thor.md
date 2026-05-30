# TAS-Style Tooling Design for xenia-thor (research-grounded)

Goal (user): build TAS-like infrastructure (as other emulators have) to reliably
drive/record/replay gameplay so we can reach + profile scenes and validate fixes
deterministically. This also blunts the agent-fabrication problem: replayable
movies + the verify gate make every claim reproducible and checkable.

## How real TAS tools work (researched)

- libTAS (tasvideos.org/EmulatorResources/LibTAS): an intermediate layer feeding
  the game altered INPUTS + SYSTEM TIME; provides frame advance, input recording,
  savestates. Deterministic timer advances time by 1/fps per frame. Caveat:
  multithreaded games are hard to make fully deterministic.
- BizHawk .bk2 (bizhawk-emulator.com): ZIP of header + frame-by-frame input log +
  sync settings; deterministic playback requires same core/firmware/settings.
  TAStudio = piano-roll input editor with branches/savestate.
- Core primitives: (1) INPUT MOVIE = recorded input only (no video); (2) FRAME
  ADVANCE = step one frame; (3) SAVESTATE = snapshot/restore; (4) DETERMINISM via
  controlled time + fixed settings.

## Mapping to xenia-thor (what we have vs need)

| TAS primitive | xenia-thor today | gap / plan |
|---------------|------------------|-----------|
| Input movie   | hid_nop_button_sequence (BUTTONS@DELAY_MS:HOLD_MS, emulator-start-relative, deterministic, guest-side) + getevent/sendevent record/replay (Android raw) | FORMALIZE as a saved ".movie" file + runner. hid_nop is the deterministic one (fed straight to guest). |
| Frame advance | time_scalar live (slow-mo; 0.02 verified = slow not freeze); in-game Start pause (true 0fps where supported) | No true single-frame step. Slow-mo + pause is the practical substitute. |
| Savestate     | NONE (verified earlier: no guest save/restore) | BIGGEST GAP. A savestate (snapshot guest RAM+CPU ctx+GPU) would let us reach a scene ONCE and restore instantly = the highest-value TAS primitive for our reach-a-scene problem. Large effort; flag for design. |
| Determinism   | Xenia is multithreaded (host-threaded) -> NOT fully deterministic (matches libTAS caveat); time_scalar gives time control | Accept non-determinism; gate every result with a screenshot read (thor_verify_capture.ps1) rather than assuming replay = identical. |

## Concrete buildable-now tool: TAS movie runner

`thor_tas.ps1` (to build):
- -Record: capture a session as a movie. For hid_nop we can't "record" guest input
  (it's scripted), so RECORD mode = capture the user's getevent stream
  (adb shell getevent -t <gamepad node>) to <name>.rawmovie while they play.
- -Replay <name>: replay. Two backends:
  (a) hid_nop movie: launch with hid_nop_button_sequence built from the movie
      (deterministic, guest-side) — preferred for menu/reach sequences.
  (b) raw movie: sendevent replay of the getevent capture preserving inter-event
      delays — for arbitrary gameplay the user recorded.
- -SlowMo <scalar>: set time_scalar during replay so timing-sensitive steps land
  (research showed fixed-timing replay is fragile; slow-mo widens windows).
- End: ALWAYS run thor_verify_capture.ps1 + REQUIRE reading the final screenshot.

This gives: user records a reach-a-scene movie ONCE -> we replay it deterministically
to reach that scene every run -> profile/verify there. Reproducible, checkable.

## Why this directly serves the priorities

- Reach the slow ~7-9fps Burnout in-race scene reliably (replay a recorded movie)
  -> capture perf counters there vs a fast scene -> diff -> localize the
  framerate-collapse cost. Same for Lost Odyssey/Blue Dragon/Banjo.
- A movie per priority title = the "easy automated test" the user asked for:
  replay -> verify gate -> read frame -> tier. Reproducible regression checks.

## Honest caveats (must validate on device before trusting)

- getevent/sendevent fidelity to Xenia's HID path is UNVERIFIED — Xenia reads
  Android input; confirm replayed raw events reach the guest, else use hid_nop.
- No savestate = each test pays full boot+reach time; savestate is the real
  long-term win but is a large engine feature (flag for user/maintainer).
- Replay is NOT bit-deterministic (multithreaded) -> verify by screenshot, not by
  assuming identical frames.

## Status

Design committed. Next: build thor_tas.ps1 (record via getevent, replay via
hid_nop and/or sendevent, slow-mo + verify-gate end). Validate getevent node +
replay fidelity on device before any "works" claim. Savestate flagged as the
highest-value future primitive. master.
