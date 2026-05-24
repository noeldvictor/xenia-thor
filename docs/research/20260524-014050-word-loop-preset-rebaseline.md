# Word Loop Preset Rebaseline

Date: 2026-05-24 01:40 EDT

Branch: `master`

Commit: `bb20d68ee`

Capture: `scratch/thor-debug/20260524-013649-*`

APK SHA:

`CB234AD454F2F458BB22E436F16CCDC7DBD8EA7510DDD8005B8AC2E2B9E0CA3C`

## Question

After promoting `Arm64BlueDragonWordCopyLoopFastpath` into the Blue Dragon A64
speed pack, what is the next body-time wall when the normal speed lane is used
without an explicit word-loop override?

## Capture Shape

Command shape:

- `LaunchBlueDragonSpeedCapture`
- `LiveCaptureSeconds 180`
- delayed body-time after `120000ms`
- no disassembly dump
- no block body-time profiler
- `Arm64BlueDragonVmxCopyLoopFastpath=false`
- no explicit `Arm64BlueDragonWordCopyLoopFastpath` argument

Metadata confirmed:

- `arm64_blue_dragon_word_copy_loop_fastpath=true`
- `arm64_blue_dragon_vmx_copy_loop_fastpath=false`
- `disassemble_functions=false`
- `arm64_speed_profile_block_body_time=false`

The final screenshot reached the visible Blue Dragon opening sky/wing route.
Fatal-marker search was clean, and no idle-owner lines were emitted.

## Result

Final delayed body-time rows:

| Function | Body Ticks Total | Read |
| --- | ---: | --- |
| `82282490` | `25383515` | dominant wall |
| `82281D28` | `7175068` | same large opening-scene cluster |
| `82490030` | `3480696` | lower after word-loop preset |
| `82282388` | `2082550` | likely nearby/related body-time row |
| `82282410` | `2004094` | likely nearby/related body-time row |
| `82486178` | `1358411` | still present, but not next target |
| `82485DD8` | `618538` | word-loop target remains reduced |
| `82486018` | `457433` | secondary |

Final perf sample:

- Main Thread `96.1%`
- XMA Decoder `7.6%`
- GPU Commands `3.8%`
- Draw Thread `3.8%`

The route is still CPU/JIT-bound. The Android UI frame counters are not game FPS
proof.

## Decision

The word-loop speed preset is working: `82485DD8` no longer looks like the next
main wall. Move the next speed slice back to the `82282490` opening-scene
cluster under the current preset.

Do not re-open the `82486178` VMX copy-loop fastpath without new evidence; keep
`arm64_blue_dragon_vmx_copy_loop_fastpath=false`.

## Next

Run a route-stabilized `82282490` block body-time capture under the current
Blue Dragon speed preset:

- keep `arm64_blue_dragon_word_copy_loop_fastpath=true`;
- keep `arm64_blue_dragon_vmx_copy_loop_fastpath=false`;
- use delayed body-time for `82282490,82281D28`;
- enable block body-time for `82282490`;
- avoid disassembly dumps unless the block capture identifies a new HIR target.

If the old `822825E0 -> 82282490` recursive wall still dominates, use call-edge
profiling or HIR/callee analysis there rather than returning to stale `82490030`
children.
