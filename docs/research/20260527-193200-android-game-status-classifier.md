# Android Game Status Classifier

## Context

Android usability work needs a short, repeatable way to say whether a failed
launch is an app/process crash, a Xenia guest crash, a heap/exception
compatibility class, or a launch/stall with no fatal marker. The launcher recent
games list currently has only coarse `running` versus `exited_to_menu` state.

## Change

Added `tools/thor/thor_android_game_status_report.ps1`.

Inputs:

- `-LogPath <logcat.txt>`
- or `-PacketDir <remote-debug-packet>` when the packet contains `logcat.txt`

Output is key/value text so it can be pasted into worklogs, attached to a
remote-debug packet, or later consumed by launcher/recent-game tooling.

Current classifications:

- `android_or_native_process_crash`
- `xenia_guest_crash`
- `guest_heap_rtlraiseexception`
- `project_sylpheed_guest_heap_rtlraiseexception`
- `launched_no_crash_marker`
- `no_xenia_runtime_evidence`
- `unknown`

`tools/thor/thor_android_remote_debug.ps1` now writes `status-report.txt`
beside `logcat.txt` and `logcat-focused.txt`.

## Project Sylpheed Validation

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_game_status_report.ps1 -LogPath scratch\thor-debug\20260527-161935-logcat.txt -OutPath scratch\thor-debug\20260527-161935-status-report.txt
```

Result:

```text
classification=project_sylpheed_guest_heap_rtlraiseexception
reason=Project Sylpheed title ID plus heap failure and RtlRaiseException
title_id=535107D4
media_id=2D2E2EEB
rtl_raise_exception=702DF8D0(E06D7363)
crash_pc=8245BDEC
crash_function=0x8245BD80-0x8245BE64
base_heap_release_count=1
base_heap_alloc_count=1
physical_heap_count=2
android_runtime_count=0
native_signal_count=0
```

This confirms the current Project Sylpheed failure remains a Xenia guest heap /
guest exception compatibility class, not an Android Java crash and not the
fixed title/menu geometry bug.

## Remote Debug Packet Smoke

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenshot
```

Packet:

`scratch/thor-debug/remote-debug-20260527-193155/`

The packet now includes `status-report.txt`. Because this was an opportunistic
current-log screenshot rather than a controlled repro, it intentionally did not
clear logcat first; stale prior Sylpheed markers remained visible. For a clean
controlled repro, pass `-ClearLogcat`.

## Next

Use the status report as the host-side bridge toward richer recent-game
classification. A later app slice can expose known classes such as
`Guest heap / RtlRaiseException` in the launcher, but only after deciding how
the app should safely ingest host-side evidence or native guest-crash state.
