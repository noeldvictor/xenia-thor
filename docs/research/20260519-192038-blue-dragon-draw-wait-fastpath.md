# Blue Dragon Draw-Wait Fastpath

Date: 2026-05-19 19:20 EDT

## Question

Can the known Blue Dragon draw-thread wait function at guest `8246B408` be
lowered as a narrow A64 body to reduce HIR/prologue helper churn while
preserving the current visible-opening route on AYN Thor?

## Change

- Added default-off cvar `arm64_blue_dragon_draw_wait_fastpath`.
- Routed it through `EmulatorActivity` and `tools/thor/thor_xenia_debug.ps1` as
  `-Arm64BlueDragonDrawWaitFastpath`.
- The fastpath hand-emits the decoded wait predicate:
  - wait state `r3`;
  - draw object pointer at wait state `+0`;
  - abort byte `object+0x2A39`;
  - token pointer `object+0x2A10`, saved token `wait_state+0x8`;
  - current KTHREAD from PCR `r13+0x100`;
  - current tick `KTHREAD+0x58`;
  - owning thread field `KTHREAD+0x14C`;
  - owner fields `object+0x2A08` / `object+0x2A70`;
  - timeout threshold `5000` ms.
- The timeout helper call at guest `8247A440` is intentionally skipped in this
  fastpath and the whole feature is title-specific research-only.

## Captures

Baseline:

- `scratch/thor-debug/20260519-184120-*`
- Reached the visible opening scene.
- Final interval:
  `entry_delta=291310454`, `g2h=435891/4857227`,
  `direct=290962320/1681928431`.
- Top function:
  `8246B408`, `delta=289004366`, `code_size=1632`.

Fastpath with inline tick step:

- `scratch/thor-debug/20260519-190854-*`
- Flags:
  `-Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitInlineTickStep 1`
- Stayed alive but parked on a black screen.
- The profile showed a `5000`-entry `8246B408` burst, then zero entry deltas.
- Conclusion: artificial `+1 ms` per loop is too blunt and can trip the wait
  route early.

Fastpath with real clock update:

- `scratch/thor-debug/20260519-191314-*`
- Flags:
  `-Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitProbe true`
- Reached the visible opening scene with no fatal signal, AndroidRuntime,
  device-lost, GPU-hung, tombstone, ANR, unknown-register, or
  `NtCreateFile failed` markers in the filtered log.
- Final interval:
  `entry_delta=51037780`, `g2h=48992457/296760225`,
  `direct=50662272/341068379`.
- Top function:
  `8246B408`, `delta=48625230`, `code_size=448`.

Fastpath with real clock update every 4096 entries:

- `scratch/thor-debug/20260519-191802-*`
- Flags:
  `-Arm64BlueDragonDrawWaitFastpath true -Arm64BlueDragonDrawWaitProbeStride 4096`
- Stayed alive but parked on a black screen.
- The profile reached one `8246B408` burst of `4096`, then zero entry deltas.
- Conclusion: the current route needs frequent KTHREAD time progress; stride
  `4096` is too sparse for this fastpath.

## Interpretation

The direct A64 body is viable and much smaller (`448` bytes versus `1632`), but
the useful path currently converts the old direct-call wall into a
guest-to-host clock-update wall. This says the next speed work is not Vulkan;
it is timer/wait HLE or a cheaper generated-code way to provide guest uptime to
the draw wait without a native thunk on every loop entry.

## Next Experiment

Move the real-clock update to match the original PPC load site more closely, or
replace the per-entry native clock thunk with a cached guest-millisecond value
in `A64BackendContext` that generated code can read cheaply. Keep
`arm64_blue_dragon_draw_wait_fastpath` default-off until it is a clear route and
speed win.
