# XamAlloc unk Assert Softened (autonomous iter 8) — DEVICE PROVEN

## Change

`src/xenia/kernel/xam/xam_info.cc` `XamAlloc_entry(unk, size, out_ptr)` asserted
`unk == 0` and aborted the whole emulator otherwise. `unk` is unused (the body
only uses `size` + `out_ptr`, allocating from the system heap), so a nonzero
`unk` is harmless. Replaced the assert with log-once + continue.

## Proof (device)

APK `C5F75B10...`. Gears 3 launched deterministically via `-InputSequence`
(`hid_nop`) — packet `scratch/thor-debug/game-pass-gears3-xamfix-20260529-211614`:

- `crash_dump=0`, `abort_msg=0` — no abort/crash dump (was the proof that matters).
- `XamAlloc: nonzero unk=10000000 ...` softening log fired (one-shot — the static
  flag suppresses repeats, so it logs once regardless of call count). Gears passes
  a nonzero unk and is now handled instead of aborting.

So the intermittent `xam_info.cc:344 XamAlloc "unk == 0"` crash (which had been
killing Gears 3 mid-test, e.g. during the UMA A/B) is resolved. Combined with the
deterministic `hid_nop` input, Gears testing is now far more reliable. (The
captured frame this run was a darker menu/transition, no crash; render-state
characterization continues under the per-title sequence work.)

## Fixable-assert pattern (running tally)

Same softening pattern now applied to three independent ARM64-port aborts that a
CPU-side/kernel gap should not turn into a full crash:
- shader interpreter unhandled ALU opcode (commit 2e562ab74)
- vertex-fetch zero stride (commit 9bb6db6d3) — the real Gears boot fix
- XamAlloc nonzero unk (this)

## Next

- Per-title deterministic reach-scene `-InputSequence` library so each title lands
  in a known scene reproducibly; then characterize Gears 3 gameplay reach.
- High-leverage: the shared "boots-but-black" present/render-target class.
