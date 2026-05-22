# Call-Edge Control Sandwich

Date: 2026-05-22 18:37 EDT

## Question

After fixing the first `arm64_speed_profile_call_edge_filter` bug, was the
black idle just normal route instability, or does the current call-edge
profiler path perturb startup before `82282490`?

## Inputs

All three captures used the same APK and commit:

- HEAD: `40a40c26e`
- APK SHA-256:
  `0E6F2CADBA61BAAF16866D1EA10B085C1DDC219570924260A2B292DCD975F311`
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`
- Common route flags: nop HID sequence, delayed body-time after `120000ms`,
  LSE kernel lock fastpaths on, RtlLeave audit off, permute zip fastpath on,
  r1 live-in off, idle thread snapshot on, stop app after capture.

## Control A

Capture: `scratch/thor-debug/20260522-182318-*`

`arm64_speed_profile_call_edge_filter` was blank and
`arm64_speed_profile_body_time_filter=82282490`.

Result:

- Reached the visible opening sky/wing route.
- No searched fatal markers were found in the log.
- `82282490` emitted delayed body-time rows:
  - 18:25:35: `body_ticks_delta=4595537`, `entries_delta=18337`,
    `ticks_per_entry=250`, `code_size=87168`
  - 18:25:50: `body_ticks_delta=3125109`, `entries_delta=16488`,
    `ticks_per_entry=189`
  - 18:26:05: `body_ticks_delta=10093471`, `entries_delta=77168`,
    `ticks_per_entry=130`
  - 18:26:20: `body_ticks_delta=8332807`, `entries_delta=62328`,
    `ticks_per_entry=133`
- Final thread sample was active: Main Thread about `103%`, XMA Decoder about
  `50%`, GPU Commands about `7.6%`.

## Edge Filter

Capture: `scratch/thor-debug/20260522-182705-*`

`arm64_speed_profile_call_edge_filter=82282490`; body-time filter was blank.

Result:

- The final screenshot was black with Android system bars.
- No searched fatal markers were found in the log.
- The route black-idled before `82282490`:
  - 18:27:22: `funcs=3539`, `entry_delta=3803046`
  - 18:27:37: `funcs=3866`, `entry_delta=216965`
  - 18:27:52 onward: `funcs=3866`, `entry_delta=0`
- Body-time activated at 18:29:07, after the route was already idle.
- No `A64 speed profile call edge top` rows were emitted.
- `tools/thor/thor_hir_call_path_report.ps1` reported no dynamic call-edge rows.
- Final thread sample was idle: one Java/UI-ish thread about `3.8%`, emulator
  guest threads and GPU/XMA threads at `0%`.

## Control B

Capture: `scratch/thor-debug/20260522-183118-*`

`arm64_speed_profile_call_edge_filter` was blank and
`arm64_speed_profile_body_time_filter=82282490`.

Result:

- Reached the visible opening sky/wing route.
- No searched fatal markers were found in the log.
- `82282490` emitted delayed body-time rows:
  - 18:33:36: `body_ticks_delta=4398705`, `entries_delta=16670`,
    `ticks_per_entry=263`, `code_size=87168`
  - 18:33:51: `body_ticks_delta=4346922`, `entries_delta=18155`,
    `ticks_per_entry=239`
  - 18:34:06: `body_ticks_delta=9419487`, `entries_delta=72716`,
    `ticks_per_entry=129`
  - 18:34:21: `body_ticks_delta=8202860`, `entries_delta=62328`,
    `ticks_per_entry=131`
- Final thread sample was active: Main Thread about `100%`, XMA Decoder about
  `7.6%`, GPU Commands about `7.6%`, Draw Thread about `3.8%`.

## Decision

This is a negative result for the current call-edge filter path. The two
same-APK controls bracketed the edge run and both reached opening plus
`82282490` body-time, while the edge-filter run black-idled before the target
function and emitted no dynamic edge rows.

Keep `arm64_speed_profile_call_edge_filter` default-off. Do not rerun the exact
edge capture unchanged.

## Next Experiment

Before trying call-edge timings again, make the profiler safer to diagnose:

- add a lower-overhead compile/activation audit that records whether the
  filtered function is compiled and whether any call-edge slots are installed;
- or change the call-edge profiler so a nonempty filter is inert until the
  filtered function is actually compiled/reached;
- then prove route safety with a same-APK control/diagnostic sandwich before
  using the edge rows for speed decisions.

The idle starts around 45 seconds, while delayed body-time activation is at
120 seconds, so the immediate suspicion is not the delayed activation callback
itself. Treat that as an inference until a compile/activation audit proves where
the perturbation enters.
