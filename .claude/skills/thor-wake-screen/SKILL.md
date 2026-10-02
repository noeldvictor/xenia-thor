---
name: thor-wake-screen
description: Turn the AYN Thor's screen on before an approved device session, when the preflight or xenia_wait_ready says "screen is asleep". Wakes the panel with the system wake key only, never game input.
---

# Wake the Thor's screen

Use this when an approved device session (heat probe, scoreboard, live A/B,
launch) is blocked because the panel is asleep. A sleeping panel stops the
preflight, and a game started behind it renders slowly or not at all.

Waking the panel is **not** permission to use the device: every Thor session
still needs the user's yes, and the device is shared - check that no other
session's emulator is in the foreground first.

## Steps

1. Wake it - one call:
   - MCP: `xenia_wake_screen` (server `xenia-thor`), or
   - shell: `python tools/thor/wake_screen.py`
   It does nothing when the panel is already awake. Otherwise it sends
   `KEYCODE_WAKEUP`, runs `wm dismiss-keyguard`, and returns the wakefulness
   before and after as JSON.
2. Check the result: `"after": "Awake"` means the panel is on. If `keyguard`
   shows a lock screen that stays (a PIN or pattern), ask the user to unlock.
3. Continue with the session. `xenia_wait_ready` (used by `heat_probe.py` and
   the scoreboard) already calls `xenia_wake_screen` while it waits, at most
   every 30 s, so a session started through it needs no extra step.

## Rules

- `KEYCODE_WAKEUP` is the only keyevent allowed (user, 2026-10-02). The window
  manager consumes it to wake the device and never passes it to the
  foreground app, so it cannot press anything in another session's game. Never
  send other keyevents (`POWER` toggles the panel off again; game input goes
  through the in-app debug server's `press` and `route`).
- Do not change global device settings to keep the screen on (`svc power
  stayon`, the screen timeout): the device is shared.
