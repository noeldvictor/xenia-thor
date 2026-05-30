# Gears 3 Zero-Stride Vertex-Fetch Fix (autonomous iter 3) — DEVICE PROVEN

## Result

Gears of War 3 now boots **past the crash to its title screen** ("PRESS START")
rendering cleanly at ~30 fps. Previously it aborted during boot.

- APK `0E30244D...`, packet `scratch/thor-debug/game-pass-gears3-stridefix-20260529-201836`.
- Proof: screenshot = Gears 3 title screen @ 29.6 FPS; `vdswap=2209` @ ~30 fps;
  logcat has the fix's `Shader vertex fetch: zero-stride new binding (fetch
  constant 93); allowing instead of asserting` and **no** `==== CRASH DUMP ====`,
  no `__assert2`/abort, no stride assertion. (`crash=3` in the sweep table were
  RtlRaiseException noise.)

## Root cause (corrected from iter 1/2)

The real Gears 3 / Judgment crash was NOT the shader-interpreter ALU path
(addr2line `-i` mis-attributed `0xdcb6b4`). The authoritative abort message was:

```
shader_translator.cc:429: GatherVertexFetchInformation:
assertion "(fetch_instr.attributes.stride) != 0" failed
```

Gears issues a `vfetch` that creates a **new** vertex binding with stride 0
(fetch constant 93). `assert_not_zero(fetch_instr.attributes.stride)` aborted.

## Fix

`src/xenia/gpu/shader_translator.cc` `GatherVertexFetchInformation`: the
existing-binding path above already tolerates a zero stride
(`assert_true(!stride || ...)`); the new-binding `assert_not_zero` was
inconsistent paranoia. Replaced it with: allow the zero-stride binding and log
once. Minimal, consistent with existing tolerance, Vulkan permits a zero binding
stride.

## Notes / next

- The iter-2 shader-interpreter ALU non-fatal change (commit 2e562ab74) was a
  sound general robustness fix but did NOT address Gears (its log never fired);
  kept, not claimed as the Gears fix.
- A prior "disc-mount FatalError" on Gears 3 was self-inflicted: a wrong ISO
  filename (dropped ",Hu"). Always use exact `ls` filenames.
- Next: verify Gears: Judgment (shares the identical assert PC) reaches title
  with this build; then try pressing Start to advance Gears 3 past the title into
  menu/campaign; then continue the goal queue (RPG sweep, UMA quantification).
