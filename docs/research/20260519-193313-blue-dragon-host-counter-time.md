# Blue Dragon Host-Counter Time Fastpath

Date: 2026-05-19 19:33 EDT

## Question

Can the `8246B408` Blue Dragon draw-wait fastpath update `KTHREAD+0x58`
without a native guest-to-host clock thunk on every loop entry?

## Change

- Added default-off cvar
  `arm64_blue_dragon_draw_wait_fastpath_host_counter_time`.
- Routed it through Android and `tools/thor/thor_xenia_debug.ps1` as
  `-Arm64BlueDragonDrawWaitFastpathHostCounterTime`.
- Added `A64BackendContext::host_uptime_millis_base`.
- The fastpath can now read `CNTVCT_EL0`, divide by `CNTFRQ_EL0`, subtract the
  per-context uptime base, and store the resulting guest-shaped milliseconds to
  `KTHREAD+0x58` directly in generated A64.

This is still title-specific and default-off. It assumes Android's virtual
counter uptime tracks close enough to the host monotonic uptime base for this
Blue Dragon wait loop.

## Captures

Short probe:

- `scratch/thor-debug/20260519-192458-*`
- Flags:
  `-Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitProbe true`
- At 110s it reached the Blue Dragon loading spinner.
- Final sampled interval:
  `entry_delta=72636142`, `g2h=409826/3373061`,
  `direct=69927435/644398179`.
- Top function:
  `8246B408`, `delta=62832095`, `code_size=472`.

Long proof run:

- `scratch/thor-debug/20260519-192732-*`
- Flags:
  `-Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitFastpathHostCounterTime true -Arm64BlueDragonDrawWaitProbe true -MountCache true`
- At 180s it reached the visible Microsoft Game Studios opening scene.
- After waiting another 90s, `scratch/thor-debug/20260519-193236-screenshot.png`
  showed a rendered in-game/opening scene with the main character visible.
- No fatal signal, AndroidRuntime, device-lost, GPU-hung, tombstone, or ANR
  markers were found in the filtered proof logs.
- `MountCache true` removed the `ResolvePath(cache:\...) failed` noise seen in
  the no-cache short probe.

Final long-run interval before the follow-up screenshot:

```text
entry_delta=116069345
h2g=1230/43150
g2h=107939/5543819
direct=115807812/2097876628
indirect=258471/15161313
extern=104164/5403114
top 01: 8246B408 delta=114135711 code_size=472
```

## Interpretation

This is the best Thor route so far. The game is rendering real Blue Dragon
content on device, and the clock update is no longer the dominant guest-to-host
thunk storm. The remaining wall is still `8246B408`, but now it is a generated
A64 busy-wait rather than a host-call bottleneck.

The next speed task is to make the wait loop yield/sleep or advance by event
semantics instead of burning full CPU while preserving the route.

## Next Experiment

- Add a default-off wait-yield or wait-budget path inside the `8246B408`
  fastpath.
- Continue with `-MountCache true` for Blue Dragon captures.
- Consider inlining the visible helper families now appearing behind the wait:
  `__savevmx_124`, `__restvmx_124`, `__savefpr_27`, and `__restfpr_27`.
