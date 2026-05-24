# 82282490 Recursive Edge And Stack Sync Probe

Date: 2026-05-24 02:11 EDT

## Goal

Rebaseline the Blue Dragon opening route after the `82485DD8` word copy-loop
speed preset and test whether A64 host/guest stack synchronization is a
measurable part of the current `82282490` recursive wall.

## Context

The prior preset rebaseline
`docs/research/20260524-014050-word-loop-preset-rebaseline.md` moved the main
CPU/JIT wall back to `82282490`:

- `82282490=25383515`
- `82281D28=7175068`
- `82490030=3480696`
- `82486178=1358411`
- `82485DD8=618538`
- `82486018=457433`

The word-loop fastpath remains enabled only by the Blue Dragon Thor speed pack;
the backend cvar is still globally default-off.

## Fresh 82282490 Evidence

Delayed block body-time under the current speed preset:

- Capture: `scratch/thor-debug/20260524-014437-*`
- HEAD: `c702bd0c4`
- APK SHA-256:
  `CB234AD454F2F458BB22E436F16CCDC7DBD8EA7510DDD8005B8AC2E2B9E0CA3C`
- Route: visible Blue Dragon opening sky/wing scene
- Fatal-marker search: clean

Final body-time rows:

- `82282490=51530078`
- `82281D28=8273724`
- `82282388=2399112`
- `82282410=2302068`

Final internal block body-time rows for `82282490`:

- `822825E0`: `body_ticks_total=36823133`, peak delta `17534486`,
  peak ticks/entry `54`
- `8228252C`: `body_ticks_total=4141749`, peak delta `1283886`,
  peak ticks/entry `5`
- `822825C8`: `body_ticks_total=3428049`, peak delta `1093051`,
  peak ticks/entry `596`
- `822824F0`: `body_ticks_total=1394125`, peak delta `718642`,
  peak ticks/entry `1`

Delayed call-edge capture under the current speed preset:

- Capture: `scratch/thor-debug/20260524-014858-*`
- HEAD: `c702bd0c4`
- APK SHA-256:
  `CB234AD454F2F458BB22E436F16CCDC7DBD8EA7510DDD8005B8AC2E2B9E0CA3C`
- Route: visible Blue Dragon opening sky/wing scene
- Fatal-marker search: clean

Final dynamic call-edge rows:

- `822825E0 -> 82282490`: `calls_total=117425`,
  `body_ticks_total=28602334`, peak delta `12237024`,
  peak ticks/call `1244`
- `822825C8 -> 8227FEE8`: `calls_total=9661`,
  `body_ticks_total=11951162`, peak delta `8961581`,
  peak ticks/call `4736`
- `8228252C -> 82287788`: `calls_total=528479`,
  `body_ticks_total=3517935`, peak delta `1300312`,
  peak ticks/call `31`
- `822824F0 -> 82274DB0`: `calls_total=528479`,
  `body_ticks_total=634454`, peak delta `319442`,
  peak ticks/call `1`

Static HIR confirms `822825E0` is a tiny direct recursive call block, not a
large local codegen target:

- `instr=18`
- context loads/stores `2/6`
- memory loads/stores `1/0`
- branches `1`
- calls `1`
- barriers `2`
- call target `0x82282490`

## A64 Call Path Inspection

The direct guest call path in `src/xenia/cpu/backend/a64/a64_emitter.cc` uses
generated code to generated code `blr` when the target already has machine
code. It does not use the host-to-guest thunk for every recursive call. The
host-to-guest thunk remains the host C++ entry path.

The remaining likely per-call costs in this lane are:

- guest function prolog/epilog work;
- stackpoint push/pop;
- optional guest/host stack synchronization after guest calls;
- guest-call return-address slot maintenance;
- body-time/call-edge instrumentation when profiling is enabled.

## Patch

Exposed existing `a64_enable_host_guest_stack_synchronization` to Android/Thor
launches:

- `EmulatorActivity` now copies the boolean extra into launch arguments.
- `tools/thor/thor_xenia_debug.ps1` accepts
  `-A64EnableHostGuestStackSynchronization`.
- Capture metadata records
  `a64_enable_host_guest_stack_synchronization=...`.
- The Blue Dragon speed pack explicitly keeps the old behavior by defaulting
  the flag to `true`.

## Same-APK Stack Sync A/B

Both captures used APK SHA-256:
`5FD5488026AA7152ED86E352864EC7FA160BE9ADBE1E0D4B7B2827FF554E85CF`.

Experiment, stack sync off:

- Capture: `scratch/thor-debug/20260524-020019-*`
- Cvar: `a64_enable_host_guest_stack_synchronization=false`
- Route: visible Blue Dragon opening sky/wing scene
- Fatal-marker search: clean
- Final `82282490`: `body_ticks_total=26462740`,
  final interval `ticks_per_entry=119`, `code_size=85104`
- Final `82281D28`: `body_ticks_total=7602770`,
  final interval `ticks_per_entry=89`, `code_size=94020`
- Final perf sample: Main Thread `92.3%`, GPU Commands `19.2%`,
  XMA Decoder `50.0%`, Draw Thread `3.8%`

Control, stack sync on:

- Capture: `scratch/thor-debug/20260524-020408-*`
- Cvar: `a64_enable_host_guest_stack_synchronization=true`
- Route: visible Blue Dragon opening sky/wing scene
- Fatal-marker search: clean
- Final `82282490`: `body_ticks_total=27192906`,
  final interval `ticks_per_entry=131`, `code_size=87168`
- Final `82281D28`: `body_ticks_total=7664801`,
  final interval `ticks_per_entry=91`, `code_size=96908`
- Final perf sample: Main Thread `103%`, GPU Commands `3.8%`,
  XMA Decoder `7.6%`, Draw Thread `0.0%`

## Read

Turning stack synchronization off is route-clean in this one same-APK A/B and
is a small generated-code/body-time win:

- `82282490` total body ticks: `27192906` -> `26462740`, about 2.7% lower.
- `82282490` final interval ticks/entry: `131` -> `119`.
- `82282490` code size: `87168` -> `85104`.
- `82281D28` code size: `96908` -> `94020`.

This is not enough to promote global stack synchronization off. The flag guards
the imported A64 stackpoint/host-guest stack path, so a broad default-off change
could break longjmp/setjmp, exception, or unwind-sensitive behavior outside
this route.

## Decision

- Keep `a64_enable_host_guest_stack_synchronization` exposed in Thor tooling.
- Keep the Blue Dragon speed pack default at `true` for now.
- Do not globally disable stack synchronization.
- Treat stack-sync-off as a promising default-off Blue Dragon route experiment,
  not a proven FPS fix.
- The current wall remains CPU/JIT-heavy and concentrated in the recursive
  `822825E0 -> 82282490` lane, with `822825C8 -> 8227FEE8` still secondary.

## Next

Run one more route-matched stack-sync control sandwich or add a lower-risk
stackpoint/prolog overhead profiler before changing the Blue Dragon speed pack.
If the off result repeats, consider a Blue Dragon-route-gated preset toggle with
a clear rollback flag. Otherwise continue with `822825E0 -> 82282490`
recursive-call/prolog cost reduction instead of returning to stale VMX/vector,
r1 live-in, or call-edge probes.
