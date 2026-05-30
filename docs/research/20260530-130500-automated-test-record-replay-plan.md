# Automated Tests via Input Record/Replay (plan, research-grounded)

Goal (user): EASY automated tests around reaching/validating game states. Method
question (user): research how to drive / record input.

## Research finding (web)

- `adb shell input` (what we've used for scripted presses) is HIGH-LEVEL and
  TIMING-LOSSY -> unreliable for precise game sequences (root cause of the flaky
  "reached level" failures).
- Robust approach: record raw events with `adb shell getevent -t` (device, type,
  code, value + timestamps from /dev/input/eventX; works for the Thor's gamepad),
  then REPLAY preserving inter-event delays (RERAN-style) -> deterministic repro.
- Caveat: `sendevent` is one-syscall-per-event (slow); preserve recorded timing.

## The key leverage: the USER can drive

The user reached Burnout in-game manually. So the reliable test workflow is:
  1. USER drives the game to a target state (menu, in-race, etc.) ONCE.
  2. We RECORD their raw input during that drive (getevent capture).
  3. Save it as a named REPLAYABLE FIXTURE (e.g. "burnout-to-race.events").
  4. Automated test = force-stop -> launch -> REPLAY fixture -> verify gate +
     read screenshot. Reproducible, no input-guessing by the agent.

This combines: user reliability (driving) + deterministic replay + the existing
verification gate (thor_verify_capture.ps1) for pass/fail. It also fixes the
agent-fabrication risk: tests assert from a screenshot read + gate verdict, not
from narration.

## Proposed tooling (to build, device-validated before trusting)

1. `thor_record_input.ps1` -Device c3ca0370 -Out <name>.events
   - identify the gamepad event node (`getevent -p` / list /dev/input/event*),
   - `adb shell getevent -t <node>` streamed to a file while the user plays,
   - stop on keypress; store raw lines + a parsed (ts,type,code,value) form.
2. `thor_replay_input.ps1` -In <name>.events
   - replay via `sendevent` (or a batched on-device script) preserving the
     recorded inter-event delays.
   - NOTE: must validate that sendevent to the gamepad node is honored by Xenia's
     HID path on device (Xenia reads Android input; confirm replayed events reach
     the guest). If not, fall back to the guest-side hid_nop_button_sequence
     (already wired) gated by the screenshot stepper.
3. `thor_test_title.ps1` -Title <iso> -Fixture <name>.events -Expect rendering
   - force-stop -> launch -> (optional movie-skip) -> replay fixture -> verify
     gate -> compare classification to -Expect -> PASS/FAIL with screenshot path.
   - This is the "easy automated test": one command per title -> verdict.

## Alternative already in hand (lower effort)

The guest-side `hid_nop_button_sequence` is deterministic input fed straight to
the guest (emulator-start-relative), more reliable than Android keyevents. The
weakness is fixed timing vs movies. Pairing it with the screenshot-gated stepper
(advance only on observed state change, settle-on-static for movies) already
removes most timing fragility WITHOUT record/replay. Record/replay is the more
robust long-term answer; the stepper is the available-now bridge.

## Honesty / caveat

None of the record/replay tools are built/validated yet — this is a plan. getevent
node + sendevent fidelity to Xenia's HID must be tested on device before any
"works" claim. Recommend the user records ONE successful Burnout-to-race drive so
we capture a real fixture to validate against.

## Status

Plan only, research-grounded. No code yet. Decision needed: build the
record/replay tools (best for easy reproducible tests; needs a user-recorded
fixture), or extend the screenshot-gated stepper first (available now). On master.
