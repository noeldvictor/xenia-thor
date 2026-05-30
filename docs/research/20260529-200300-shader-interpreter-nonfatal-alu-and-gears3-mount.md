# Shader-Interpreter Non-Fatal ALU + Gears 3 Flaky Disc Mount (autonomous iter 2)

## Change (committed)

`src/xenia/gpu/shader_interpreter.cc`: the CPU-side shader interpreter aborted the
whole emulator via `assert_unhandled_case` on an unimplemented ALU opcode
(vector default ~line 611, scalar default ~898; symbolized as the Gears 3 /
Judgment crash in 20260529-200000). Converted both to **log-once + safe
continue**:

- vector default: `vector_result[4] = {}` is zero-initialized, so an unhandled
  vector opcode now yields zero and execution continues.
- scalar default: retains `state_.previous_scalar` (like `kRetainPrev`).

Each logs the opcode value once at error level for follow-up. Rationale: a
CPU-interpreter gap should not SIGABRT the emulator. Builds clean (NativeCore +
ApkShell exit 0; APK `0E74E70B...`).

## Device verify (honest status: UNPROVEN on Gears)

Ran Gears 3 on the fix APK (`game-pass-gears3-fix-20260529-200208`). It did **not**
reach shader interpretation this run: `vdswap=0`, screen returned to the
launcher ("Last run: may have crashed - Gears of War 3"). The abort was a
different, earlier failure:

```
!> Disc image could not be mapped
x> Unable to mount disc image; file not found or corrupt.
Abort message: 'x> ... Unable to mount disc image; file not found or corrupt.'
SIGABRT in tid (Emulator)
```

Symbolized to `xe::FatalError` <- `Emulator::LaunchDiscImage` (`emulator.cc:993`,
`DiscImageDevice::Initialize()` returned false) <- `LaunchPath` <-
`EmulatorThread`.

The earlier sweep (`game-pass-all-20260529-184405`) launched this exact ISO to
~620 VdSwaps of rendering, so the disc is valid -> this is a **transient
flaky-mount** at launch (the documented flaky-mount/torn-read class), likely a
mount race after the force-stop+relaunch or a momentary SD/USB read failure. It
is unrelated to the ALU fix, and it aborted before any shader ran.

So: the ALU fix is a sound robustness change but is NOT yet proven to advance
Gears; no behavioral claim made.

## Next experiments

1. Retry the Gears 3 launch (likely succeeds) to actually exercise the ALU fix and
   see whether the shader-interpreter crash is gone and where it gets to.
2. Consider making the disc-mount failure non-fatal: `LaunchDiscImage` already
   returns `X_STATUS_NO_SUCH_FILE` right after the `FatalError`, but `FatalError`
   aborts first. Converting it to a logged error + clean return would let the
   launcher show a mount error (and allow a retry) instead of SIGABRT — but
   requires the caller chain (`CompleteLaunch`/`LaunchPath`) to surface the error
   gracefully. Validate the callers before changing.
