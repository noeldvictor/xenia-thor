# Blue Dragon Persistent Context-Cache Cvar Contamination

Date: 2026-05-21 14:53 EDT

## Summary

The 2026-05-21 Blue Dragon speed/body-time capture
`scratch/thor-debug/20260521-144913-*` unexpectedly logged
`A64 context value cache enabled` even though the capture metadata recorded
`arm64_context_value_cache=`. The Android app's saved config file on Thor had
`arm64_context_value_cache = true`, so a previously rejected experiment was
leaking into new captures when the launcher did not send an explicit false
override.

## Evidence

- Capture metadata:
  `scratch/thor-debug/20260521-144913-meta.txt`
  - `head=21d4a2632`
  - `arm64_context_value_cache=`
  - `arm64_speed_profile_body_time_filter=8272A3A4,8272A8E8,826C5620,827294CC,826BF770`
- Filtered log:
  `scratch/thor-debug/20260521-144913-speed-logcat-filtered.txt`
  - `A64 context value cache enabled`
  - first active interval: `entry_delta=3931882`
  - later active interval: `entry_delta=5412392`
  - final screenshot was black/idle, so this capture is not a route-health proof.
- Device config check:
  `adb -s c3ca0370 shell "run-as jp.xenia.emulator.github.debug grep -n -E 'arm64_context_value_cache|...' files/xenia.config.toml"`
  - line 412: `arm64_context_value_cache = true`
- Existing local skill note says this cache is a negative result:
  `arm64_context_value_cache=true` previously reported `8272A3A4`
  `loads/hits=255/0` despite many cacheable stores.

## Decision

Blue Dragon speed/title launcher defaults now explicitly pass
`arm64_context_value_cache=false` unless the user intentionally overrides it.
This keeps stale Android config state from poisoning capture-to-capture
comparisons.

## Follow-up Validation

Capture `scratch/thor-debug/20260521-145441-*` confirmed the launch override:

- metadata: `arm64_context_value_cache=false`
- filtered log: no `A64 context value cache enabled`
- screenshot: visible Blue Dragon `press START`
- post-capture cleanup: `after_pid=`

Clean speed capture `scratch/thor-debug/20260521-145533-*` kept the cache off
and reached a Blue Dragon loading screen by the final 85-second screenshot. It
also showed the current performance shape:

- 45s top: main guest thread about `74%`, XMA decoder about `40.7%`, GPU
  commands about `14.8%`
- 80s top: file worker threads dominated during loading (`File04` about
  `53.8%`, `File03` about `42.3%`, `File02` about `30.7%`, `File01` about
  `26.9%`)
- 80s A64 interval: `entry_delta=26316075`, with `826C5620`, `827294CC`,
  `8272A3A4`, and `826BF770` dominating by entry count.

The XMA silence A/B capture `scratch/thor-debug/20260521-145734-*` is a negative
result. It reduced the XMA thread from heavy CPU use to nearly idle, but the
guest counters dropped to `entry_delta=0` after the early title interval and the
final screenshot was black. Do not use `xma_fast_silence=true` as a default
Blue Dragon speed setting without a separate timing/correctness fix.

## Next

Next backend work should focus on the post-title hot set seen in the clean run:
`826C5620`, `827294CC`, `8272A3A4`, `826BF770`, `8272A8E8`, plus the file-loader
thread phase during loading. XMA decode is expensive, but simple silencing is
not route-safe.
