# Blue Dragon Delayed Body-Time Profile

Date: 2026-05-21 17:07 EDT

## Goal

Keep the Blue Dragon speed route reaching the opening scene while measuring
generated-code body cost. Entry counts alone made tiny helpers look more
important than they are, so this pass adds a delayed body-time lane that starts
after the fragile title/input transition.

## Change

Added `arm64_speed_profile_body_time_after_ms`:

- The A64 backend records the speed-profiler start host uptime.
- Body-time probes stay inactive until the requested delay expires.
- Generated function prologues store a zero body-time start value while inactive.
- Function epilogues skip `CNTVCT_EL0` delta accounting if the start slot is
  zero.
- Android launch extras and `tools/thor/thor_xenia_debug.ps1` now forward and
  record the delay value.

This keeps the expensive `CNTVCT_EL0` instrumentation out of the early route
where Blue Dragon is timing-sensitive, then enables it once the opening scene is
already running.

## Validation

Build/deploy passed after the code change:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

APK SHA-256:

```text
164E233402EEFDD254586CB15EEA45DF936B575E4CDFBB0D54B16FEB8AB444A9
```

## Captures

| Capture | Key settings | Result |
| --- | --- | --- |
| `scratch/thor-debug/20260521-164314-*` | `arm64_blue_dragon_jump_table_inline_in_caller=true` | Post-input-fix negative. The run black-idled after the first two intervals; `entry_delta=0` from 16:44:02 onward. Keep the flag default-off. |
| `scratch/thor-debug/20260521-164744-*` | body-time from boot for `82274DB0,82287788,826BF770,82274E38,82282490` | Negative profiler route. It black-idled after 16:48:32 with repeated `entry_delta=0`; final screenshot was black. |
| `scratch/thor-debug/20260521-165657-*` | same body-time filter, `arm64_speed_profile_body_time_after_ms=120000` | Reached the rendered opening sky/wing scene. No searched fatal markers. |
| `scratch/thor-debug/20260521-170107-*` | delayed body-time plus block filter for `82282490` | Reached the opening "Microsoft Game Studios Presents" scene. No searched fatal markers. |

## Delayed Body-Time Read

At the final delayed body-time interval in `20260521-165657`, entry-count top
rows still put `82274DB0`, `82287788`, `826BF770`, and `82274E38` ahead of
`82282490`. Body-time changes the priority:

| Function | body ticks delta | entries delta | ticks/entry | code size |
| --- | ---: | ---: | ---: | ---: |
| `82282490` | `9488930` | `65296` | `145` | `94608` |
| `82281D28` | `3499367` | `31152` | `112` | `104348` |
| `82287788` | `1158957` | `259310` | `4` | `36756` |
| `82282388` | `1029356` | `5940` | `173` | `95528` |
| `82282410` | `989518` | `4356` | `227` | `95528` |
| `82274DB0` | `284895` | `263516` | `1` | `1812` |
| `826BF770` | `187089` | `183743` | `1` | `324` |
| `82274E38` | `170097` | `155188` | `1` | `2880` |

So `826BF770` and `82274DB0` are frequent but cheap. The large
`82282490`/`82281D28` cluster is the better next optimization target.

At 175 seconds in the same capture:

- Main Thread: about `92.3%` CPU.
- GPU Commands: about `7.6%` CPU.
- XMA Decoder: about `7.6%` CPU.
- Draw Thread: about `3.8%` CPU.

This still points at CPU/JIT work first, not a broad GPU/RenderDoc pass.

## `82282490` Block Profile

`scratch/thor-debug/20260521-170107-*` kept the route alive and added block
counters for `82282490`. The function remained body-time top:

| Time | `82282490` body ticks delta | entries delta | ticks/entry |
| --- | ---: | ---: | ---: |
| 17:03:25 | `18002862` | `124656` | `144` |
| 17:03:40 | `13163396` | `89384` | `147` |
| 17:03:55 | `11379895` | `66192` | `171` |
| 17:04:10 | `12100200` | `49396` | `244` |

The hottest repeated blocks were:

| Block | Guest PC | Pattern |
| ---: | --- | --- |
| 1 | `822824B8` | Same high deltas as blocks 3, 7, and 8. |
| 8 | `822825F4` | Same high deltas as blocks 1, 3, and 7. |
| 3 | `822824F0` | Same high deltas as blocks 1, 7, and 8. |
| 7 | `822825E0` | Same high deltas as blocks 1, 3, and 8. |
| 5 | `8228252C` | Second tier, still hot. |
| 4 | `00000000` | Address extraction failed, but deltas are significant. |
| 0 | `82282490` | Lower tier. |
| 9 | `82282600` | Lower tier, tracks block 0. |
| 2 | `822824EC` | Lower tier. |
| 6 | `822825C8` | Low but present. |

At 175 seconds in the block run:

- Main Thread: `100%` CPU.
- GPU Commands: about `26.9%` CPU.
- Draw Thread: about `3.8%` CPU.
- XMA Decoder: `0%` in that sample.

The GPU command thread is active, but the main thread is still pegged. The next
speed pass should classify the PPC/HIR shape around `82282490`,
`822824B8`, `822824F0`, `8228252C`, `822825E0`, `822825F4`, and `82282600`
before changing codegen.

## Decision

Keep `arm64_speed_profile_body_time_after_ms` as the default way to run
body-time on the opening-scene route. Use `120000` ms for the current Blue
Dragon lane unless a later capture proves a better activation point.

Keep `arm64_blue_dragon_jump_table_inline_in_caller=false`. After the nop
keystroke fix, `20260521-164314-*` black-idled rather than proving a win.

Focus the next optimization on the `82282490` cluster. The immediate task is
disassembly/HIR classification, not another broad helper fastpath.
