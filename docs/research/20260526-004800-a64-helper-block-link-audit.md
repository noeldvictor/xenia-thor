# A64 Helper / Block-Link Audit

Date: 2026-05-26

## Question

After the `0x82282490` register-allocation audit found no allocator spill
pressure, determine whether the next Blue Dragon CPU/JIT slice should target
helper ABI, block/direct-call linking, resolver overhead, or a different lane.

## Inputs

- Capture log: `scratch/thor-debug/20260526-000910-speed-logcat.txt`
- Capture metadata: `scratch/thor-debug/20260526-000910-meta.txt`
- Capture commit: `8fee7f3d2`
- APK SHA256:
  `4B3B48CD3DB69B093CF3D47FA84BBC211DD09D23063F08DDD0064496FF6AC356`
- Hot body-time cluster: `82282490`, `82281D28`, `82287788`
- Offline tool:
  `tools/thor/thor_a64_helper_block_link_audit.ps1`

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_helper_block_link_audit.ps1
```

## Route Dispatch Result

The final route interval still had large dispatch volume:

- `entry_delta=3831037`
- `direct_delta=3357831`, `direct_total=56081841`
- `indirect_delta=469505`, `indirect_total=10809018`
- `extern_delta=389034`, `extern_total=5146442`
- `dispatch_delta_total=4216370`
- `dispatch_delta_per_entry_delta=1.10`
- `resolves_delta=5`, `resolves_total=6119`
- `resolve_misses_delta=0`, `resolve_misses_total=0`

Interpretation: warmed-route resolver overhead is not the wall. Direct,
indirect, and extern dispatch volume is material enough to justify current
call-edge profiling for the hot cluster before any behavior patch.

## Hot Body-Time Result

Latest delayed body-time rows in the same capture:

- `82282490`: `body_ticks_total=29894735`, `entries_delta=57876`,
  `ticks_per_entry=145`, `code_size=87168`
- `82281D28`: `body_ticks_total=8077356`, `entries_delta=27612`,
  `ticks_per_entry=93`, `code_size=96908`
- `82287788`: `body_ticks_total=3611977`, `entries_delta=229953`,
  `ticks_per_entry=5`, `code_size=35732`

## Source Findings

- Direct compiled calls use `fn->machine_code()` and `blr(x9)` from
  `A64Emitter::Call`.
- Uncompiled direct calls and indirect calls use the indirection table when
  available.
- Runtime resolver fallback is present, but route counters show it is not hot
  after warmup.
- Extern calls route through the guest-to-host thunk.
- Stackpoint and post-call stack synchronization are present in the A64 call
  path.
- Call-edge profiling support already exists.
- `A64Function` has a singleton `machine_code_` pointer and
  `CodeCacheBase::AddIndirection` is keyed by guest address only.

## Decision

Do not patch resolver behavior or retry exact edge payload storage.

The next useful Thor runtime slice is a route capture using existing
call-edge profiling for the wall cluster:

```powershell
-Arm64SpeedProfileCallEdgeFilter "82282490,82281D28,82287788" `
-Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" `
-Arm64SpeedProfileBodyTimeAfterMs 120000
```

This should identify whether the body-time wall is mostly direct recursive
calls, indirect dispatch, extern/helper ABI, or caller-exclusive generated code.
Only after those rows exist should we consider a block-linking or helper ABI
behavior patch.

## Closed By This Audit

- Resolver-first work for the current warmed route: `resolve_misses_total=0`
  and only `resolves_delta=5` in the final interval.
- One-edge entry replacement: current A64 entry and indirection structures are
  guest-address singleton paths, so edge variants require a general
  entry/fallback model.

## Validation

- PowerShell parser check passed for
  `tools/thor/thor_a64_helper_block_link_audit.ps1`.
- The tool ran successfully against `scratch/thor-debug/20260526-000910-*`.
