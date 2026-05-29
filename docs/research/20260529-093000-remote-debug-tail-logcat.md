# 2026-05-29 09:30 EDT - Remote Debug Tail Logcat

## Context

Fresh visual/crash reports require `tools/thor/thor_android_remote_debug.ps1`
before clearing logcat or force-stopping the app. The previous Burnout no-present
packet showed why the old default was too heavy: stale runtime logcat could grow
past hundreds of MB, making "check now" captures slow and risking another
blocked debugging loop.

## Change

- `thor_android_remote_debug.ps1` now defaults to `logcat -t 50000` instead of
  dumping the entire device buffer.
- Added `-LogcatTailLines` for bounded packet tuning and `-FullLogcat` for rare
  cases that genuinely need complete stale history.
- Logcat output streams directly from `adb` to `logcat.txt` instead of being held
  in a PowerShell array/string first.
- `meta.txt` records `full_logcat` and `logcat_tail_lines`.
- The focused filter keeps Xenia, crash, Vulkan, input, and `VdSwap` markers but
  no longer matches broad Wi-Fi "Signal" noise via a bare `signal` pattern.

## Proof

Validated parser health:

```powershell
$errors = $null; [System.Management.Automation.Language.Parser]::ParseFile('tools\thor\thor_android_remote_debug.ps1', [ref]$null, [ref]$errors)
```

Ran a live screenshot packet against AYN Thor `c3ca0370`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_android_remote_debug.ps1 -DeviceSerial c3ca0370 -Mode Screenshot -OutDir scratch\thor-debug\remote-debug-20260529-094000-tail-stream-proof
```

Result:

- `logcat.txt`: 6,384,198 bytes
- `logcat-focused.txt`: 219,367 bytes
- no empty `logcat.txt.err.txt` sidecar after successful `adb`
- `meta.txt`: `full_logcat=False`, `logcat_tail_lines=50000`
- `status-report.txt`: no scoped AndroidRuntime/native/guest/heap markers

## Decision

Default remote-debug packets are now bounded and fast enough for repeated
screen/log-first UX debugging. Use `-ClearLogcat` for controlled repros and
`-FullLogcat` only when old history is explicitly needed.
