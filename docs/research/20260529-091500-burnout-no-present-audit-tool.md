# Burnout No-Present Audit Tool

## Context

The current Burnout packet already proved the FPS badge was no longer using
Android UI frames, but it was still a manual marker scan. I added a reusable
packet audit so future Burnout black-screen reports can be classified quickly
without pasting or scanning giant logs in chat.

Tool:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_burnout_no_present_stall_audit.ps1 -PacketDir scratch\thor-debug\remote-debug-20260529-085115 -OutPath scratch\thor-debug\20260529-085115-burnout-no-present-stall-audit.txt
```

The tool prefers `logcat-focused.txt`, streams with `System.IO.StreamReader`,
counts runtime `VdSwap(` rows, parses title/media/PE metadata, checks scoped
crash and heap markers, measures screenshot blackness while ignoring the OSD
corner, and reports whether input arrived after the final guest swap.

## Proof

Output for `scratch/thor-debug/remote-debug-20260529-085115`:

```text
classification=burnout_live_black_no_present_stall_no_scoped_crash
reason=guest swaps stopped long before the packet screenshot, screen is near-black, and no scoped crash markers were found
decision=capture_thread_snapshots_wait_trace_and_transition_window
title_id=454107DC
media_id=223759FA
original_pe=Burnout4_External.pe
line_count=1514288
vdswap_count=1238159
first_vdswap_time=05-28 23:16:46.676
last_vdswap_time=05-29 05:03:44.322
seconds_since_last_vdswap=13652.576
crash_marker_count=0
heap_marker_count=0
aaudio_open_count=1
input_after_last_vdswap_count=2
near_black_share=0.998051
average_luma=0.443
black_frame_likely=1
```

This reinforces the earlier decision: the `0.0 FPS` OSD is accurate for this
packet. The game process remains alive, but guest presentation has stopped.

## Decision

Do not reopen the Android FPS source bug for this packet.

The next Burnout compatibility slice should be a controlled real-library route
that captures the transition into the no-present state with:

- A64 thread snapshots
- `XboxkrnlThreadWaitTrace`
- a current screen/log packet near the transition
- optional save reset only if the player-file state blocks the route

Then decide whether the stop is guest wait/deadlock, frontend state, or render /
presentation state.
