# Avoid Testing Movies: Deterministic Guest Input (autonomous iter 7b)

## Problem

Tests launched each title and skipped intro movies with random Android
`adb input keyevent` taps. That is non-deterministic (Android focus/timing), wastes
cycles playing movies, and lands runs in different scenes — which directly
confounded the UMA A/B (see 20260529-210300). User: "we need a way to avoid
testing movies."

## What exists

- **No save-state / snapshot infra** in this fork (grep found only GPU
  perf-snapshots and GPU command traces, not guest state save/restore). A true
  "skip boot+movies entirely" snapshot would require serializing guest RAM + per-
  thread PPC/JIT context + GPU state — large, not attempted here.
- **`hid_nop` driver = a scripted virtual XInput controller** (already built,
  already forwarded through the launch-intent allow-list, so usable with NO
  rebuild). It feeds buttons straight to the guest on an emulator-start-relative
  schedule — far more reliable and reproducible than Android-side keyevents.
- **`time_scalar`** (also forwarded) can fast-forward movie playback to cut
  wall-clock.

### hid_nop_button_sequence format (from nop_input_driver.cc)

`BUTTONS@DELAY_MS:HOLD_MS` entries separated by `;` or `|`, timed from emulator
start. BUTTONS are `start,a,b,x,y,back,lb,rb,up/du,down/dd,left/dl,right/dr`
combined with `+`/`,`. Example:
`start@26000:300; start@31000:300; a@35000:300; start@40000:300`.

## Change

`tools/thor/thor_game_speed_quality_pass.ps1`: added `-InputSequence` (drives a
fixed `hid=nop` + `hid_nop_button_sequence` instead of Android keyevents) and
`-TimeScalar`. When `-InputSequence` is set the tool skips the keyevent loop and
just waits out the window; the guest controller plays the schedule.

## Validated (device)

Gears 3 launch logged `Nop HID research controller active via setup ...
sequence='start@26000:300;start@31000:300;a@35000:300;start@40000:300;a@44000:300;
start@48000:300'` — the sequence loaded and the nop controller is the active HID,
no rebuild. (This run then hit Gears' intermittent XamAlloc assert; see below.)

## Blocker surfaced (do next)

Gears 3 intermittently aborts on `xam_info.cc:344 XamAlloc_entry assertion
"unk == 0" failed` — a softenable kernel-call assert (same pattern as the Gears
zero-stride fix). Softening it should make Gears testing reliable.

## Next

1. Soften the XamAlloc `unk==0` assert (log + handle) so Gears stops crashing
   mid-test.
2. Build a small per-title "reach scene" sequence library (deterministic Start/A
   timings) so each title lands in a known scene reproducibly.
3. Redo the UMA A/B under a fixed sequence on a stably-rendering title.
