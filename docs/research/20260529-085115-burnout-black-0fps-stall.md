# Burnout Black 0 FPS Stall

## Context

The user reported that the Android FPS OSD still looked wrong. I captured the
current device state first, without clearing logcat or force-stopping the app.

Packet:

- `scratch/thor-debug/remote-debug-20260529-085115`
- branch `blue-dragon-route-engine-reset`
- commit `bff1a6dc0`
- mode `Screenshot`
- activity `jp.xenia.emulator.EmulatorActivity`

The screenshot is a black game surface with the top-left OSD showing `0.0 FPS`.

## Title

The packet is Burnout Revenge, not Project Sylpheed:

```text
XEX_HEADER_ORIGINAL_PE_NAME: Burnout4_External.pe
Media ID: 223759FA
Title ID: 454107DC
```

The Android process stays alive as pid `5920`, and the package remains the
resumed emulator activity at packet time.

## Evidence

Targeted marker scans over `logcat-focused.txt` showed:

- `VdSwap(` rows: `1238159`
- scoped fatal / AndroidRuntime / guest-crash markers: `0`
- heap / physical-free / RtlRaiseException markers: `0`
- `AndroidAudioDriver opened AAudio output: sample_rate=48000 channels=2`

The last `VdSwap` row in the packet is:

```text
05-29 05:03:44.322  5920  5945 I xenia   : i> F8000028 VdSwap(...)
```

The screenshot packet was created at `2026-05-29T08:51:16.8979743-04:00`, so
there were no fresh guest swaps for roughly 3 hours and 47 minutes before the
black `0.0 FPS` screen was captured.

Input still reaches the Android activity after presentation has stopped:

```text
05-29 08:38:59.535  5920  5920 I XeniaInput: key device=13 name="Odin Controller" source=0x501 KEYCODE_BUTTON_A->KEYCODE_BUTTON_A down
05-29 08:38:59.707  5920  5920 I XeniaInput: key device=13 name="Odin Controller" source=0x501 KEYCODE_BUTTON_A->KEYCODE_BUTTON_A up
```

No `VdSwap` rows follow that input.

## Decision

Classify this packet as
`burnout_live_black_no_present_stall_no_scoped_crash`.

This is not evidence that the FPS OSD is still using Android UI frames. The OSD
is reporting `0.0 FPS` because the guest-side swap count has stopped advancing.
The old misleading case was the opposite: Android UI frames continued while the
badge still claimed about 60 FPS. This packet proves the current guest-swap FPS
source is doing the right thing for a no-present stall.

## Next

Do not reopen the FPS source bug from this packet. The useful Burnout lane is a
controlled real-library route with focused guest progress evidence:

- A64 thread snapshots
- `XboxkrnlThreadWaitTrace`
- current screen/log packet around the first transition into black/no-present
- optional save reset only if the player-file state blocks the route

Then decide whether this is a guest execution wait/deadlock, a render-target
selection issue, or a presentation path that stopped submitting after the
frontend/new-game flow.
