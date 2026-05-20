# Blue Dragon Wait Yield and Timeout Probes

Date: 2026-05-19 20:00 EDT

## Question

Can the `8246B408` Blue Dragon draw-wait fastpath stop burning CPU by yielding,
sleeping, or shortening the observed `5000ms` wait threshold?

## Change

- Added default-off cvar
  `arm64_blue_dragon_draw_wait_fastpath_native_yield_stride`.
- Added default-off cvar
  `arm64_blue_dragon_draw_wait_fastpath_native_sleep_us`.
- Added cvar `arm64_blue_dragon_draw_wait_fastpath_timeout_ms`, defaulting to
  `5000`, which matches the current fastpath predicate.
- Routed all three through Android launch extras and `tools/thor/thor_xenia_debug.ps1`.

## Captures

Native yield only:

- `scratch/thor-debug/20260519-194044-*`
- Flags:
  `-Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitProbe true -Arm64BlueDragonDrawWaitFastpathNativeYieldStride 8192 -MountCache true`
- At 180s it reached the Microsoft Game Studios windmill scene.
- Follow-up capture `scratch/thor-debug/20260519-194617-screenshot.png` reached
  the child-on-rock scene.
- Stable, but not a speed win. `8246B408` still dominated later intervals at
  roughly `140M` entries per 15s.

Native sleep:

- `scratch/thor-debug/20260519-194811-*`
- Flags:
  `-Arm64BlueDragonDrawWaitFastpathNativeYieldStride 8192 -Arm64BlueDragonDrawWaitFastpathNativeSleepUs 100`
- Result: black screen / parked process after the early boot phase.
- Speed profile dropped to `entry_delta=0` after the initial setup.
- Treat `native_sleep_us` as a negative probe for now.

Timeout `1000ms`:

- `scratch/thor-debug/20260519-195422-*`
- Flags:
  `-Arm64BlueDragonDrawWaitFastpathTimeoutMs 1000`
- At 180s it reached the Microsoft Game Studios windmill scene.
- Follow-up capture `scratch/thor-debug/20260519-195931-screenshot.png` reached
  the child-on-rock scene.
- Stable, but not a visible speed win against the previous host-counter route.

## Interpretation

The remaining wall is not solved by cooperative yielding, and tiny host sleeps
can park the guest. Shortening the timeout to `1000ms` is stable but does not
move the route materially faster. The current safest command remains the
host-counter fastpath with default `5000ms` timeout, no native sleep, and
`MountCache true`.

The next useful work should either identify the producer/consumer event behind
`8246B408`, or attack the next non-wait helper families once the opening route
is stable: `__savevmx_124`, `__restvmx_124`, `__savefpr_27`, and
`__restfpr_27`.

## Next Experiment

- Do not use `-Arm64BlueDragonDrawWaitFastpathNativeSleepUs` except as a
  negative-control probe.
- Keep `-Arm64BlueDragonDrawWaitFastpathTimeoutMs 5000` unless testing.
- Use Ghidra/Xenia focused dumps to find what updates the wait token and owner
  fields touched by `8246B408`.
