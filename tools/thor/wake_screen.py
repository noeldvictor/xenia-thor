"""Turn the Thor's panel on: the MCP's xenia_wake_screen from a shell.

  python tools/thor/wake_screen.py

Only when `dumpsys power` says the panel is not awake: KEYCODE_WAKEUP, then
`wm dismiss-keyguard`, then the state again (JSON: before, after, keyguard).
KEYCODE_WAKEUP is the one keyevent the rules allow (user, 2026-10-02): the
window manager consumes it to wake the device and never passes it to the
foreground app. Waking the panel is not permission to use the device.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'mcp'))
import xenia_thor_mcp as m  # noqa: E402

if __name__ == '__main__':
    print(m.xenia_wake_screen())
