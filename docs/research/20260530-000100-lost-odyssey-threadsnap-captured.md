# Lost Odyssey Thread-Snapshot Captured (iter 27) — parse glitched, packet good

## What happened

Captured a Lost Odyssey thread-snapshot packet to identify the never-exiting
join-target thread 0014A018. The device run SUCCEEDED (black, near_black=1,
~169 fps, vdswap=10362, 2.8 MB logcat). Parsing then hit a GENUINE tooling glitch:
a PowerShell grep returned its literal command text instead of evaluating
(`wait_trace=$wt ...` echoed verbatim), and a prior query truncated. Per the
end-turn-on-corruption guardrail, I stopped rather than fire blind follow-ups.

This is a REAL glitch (verified: literal `$wt`/`$prof` echoed), not the earlier
premature "deferred" pattern — the capture is sound, only the analysis shell
misbehaved.

## Packet (durable artifact)

`scratch/thor-debug/game-pass-lo-threadsnap-20260530-000110` — Lost Odyssey
(deterministic launch) with `arm64_speed_profile_thread_snapshot` +
`_on_idle` + `xboxkrnl_thread_wait_trace` (budget 256, after_ms 30000).

## Observations before the glitch

- Broad grep for `0014A018` in this packet's logcat: 0 hits. That MAY mean the
  thread-snapshot does not print the guest_object id in the form used by the wait
  trace, OR the snapshot uses a different field. Needs a clean re-parse — do NOT
  conclude "0014A018 absent" from a glitched session.
- The `snapshot` literal also returned 0; the snapshot emit tag is still unknown.

## Next iteration (clean parse, no device re-run needed)

Parse `game-pass-lo-threadsnap-20260530-000110/*.logcat.txt` with fresh tooling:
1. Find the thread-snapshot emit format: grep broadly for the A64 speed-profile /
   thread-snapshot output (try 'A64', 'idle', 'tid=', 'guest tid', 'pc=', 'r1=',
   'XThread' with last-PC). The wait trace ('Xboxkrnl wait trace') is known to be
   present and reliable — use it to cross-reference thread ids.
2. Enumerate all guest threads + their last PC/wait state; find 0014A018 (the
   join target) and report whether it is itself waiting (wait chain), never
   scheduled, or spinning.
3. If the snapshot truly isn't in this packet, the cvar may need a different
   name/trigger — verify arm64_speed_profile_* emit path in
   src/xenia/cpu/backend/a64 and thor_xenia_debug.ps1, then re-capture.

## Status

Packet captured and sound; parse blocked by a real shell glitch this turn.
LO root cause remains: never-completing thread-join on guest thread 0014A018
(committed 2a319eae3 with disasm of the 827CACA8 delay-poll wrapper). No code
change.
