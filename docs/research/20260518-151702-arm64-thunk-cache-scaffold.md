# ARM64 Thunk And Code Cache Scaffold

- Date: 2026-05-18 15:17 EDT
- Branch: `master`
- Local fork: `xenia-thor`
- Goal: add the next Edge-shaped backend/code-cache runway without switching
  the current helper mini-JIT ABI yet.

## Source References

- `has207/xenia-edge`, branch `edge`, commit
  `302034249048dc9d664b12b6a3560a6d95933803`.
- Reference files:
  - `src/xenia/cpu/backend/a64/a64_backend.h`
  - `src/xenia/cpu/backend/a64/a64_backend.cc`
  - `src/xenia/cpu/backend/a64/a64_code_cache.h`
  - `src/xenia/cpu/backend/code_cache_base.h`
- This slice adapts backend/cache shape and metadata APIs. It does not copy the
  full Edge A64 emitter, sequences, stack layout, platform unwind emitters, or
  xbyak thunk-generation bodies.

## What Landed

- ARM64 backend now exposes Edge/x64-shaped thunk slots:
  `HostToGuestThunk`, `GuestToHostThunk`, and `ResolveFunctionThunk`.
- ARM64 backend now carries placeholder `capstone_handle_`, `emitter_data_`,
  and `kForceReturnAddress` fields so future emitter imports have the expected
  integration points.
- Added a simple ARM64 `ResolveFunction(void*, uint64_t)` helper that resolves
  guest addresses through the current `ThreadState` and returns guest machine
  code when available.
- ARM64 `CommitExecutableRange` now forwards to the ARM64 code cache instead
  of being a no-op.
- ARM64 code cache now has:
  - sparse reserved indirection table memory;
  - encoded 64-bit indirection targets with an external side table;
  - `set_indirection_default_64`;
  - `AddIndirection` and `AddIndirection64`;
  - `CommitExecutableRange`;
  - `PlaceData`;
  - `Arm64EmitFunctionInfo` placement overloads;
  - `UnwindReservation` placeholder metadata;
  - `generated_code_commit_mark_`;
  - debug-safe `LookupIndirection` that avoids reading uncommitted table pages.
- Existing helper mini-JIT placement now updates the indirection table for
  guest function addresses.

## Why This Slice

- The previous audit said the next wall was backend infrastructure, not another
  missing HIR switch case.
- The current mini-JIT compiled-program ABI is
  `bool(PPCContext*, ThreadState*, uint32_t)`, while Edge's real A64 guest
  entry ABI uses host-to-guest thunks and guest return-address conventions.
  Switching that ABI at the same time as importing table metadata would make
  failures much harder to isolate.
- This gives the future Edge-style emitter a real cache and indirection API to
  target while preserving today's known Thor execution path.

## Validation

- `git diff --check` passed with only CRLF normalization warnings.
- `tools/thor/thor_build.ps1 -Mode NativeCore -DeviceSerial c3ca0370`
  passed for `arm64-v8a` and `x86_64`.
- `tools/arm64/arm64_conversion_audit.ps1 -OutPath scratch/arm64-thunk-cache-scaffold-audit.md -LatestLogs 4`
  reports:
  - backend capability gaps: none;
  - code-cache capability gaps: none;
  - helper mini-JIT call sites still: 52.
- `tools/thor/thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`
  built and installed successfully after the final guard update.
- APK SHA-256:
  `860FA95A862220425406E1D51CBD3DDAA49EC1B35EEA846EEE87117D20E469C2`.
- Blue Dragon Disc 1 regression capture:
  `scratch/thor-debug/20260518-152107-*`.
  - The ARM64 code cache logs both generated-code and indirection-table
    addresses:
    `ARM64 code cache allocated 134217728 bytes ... indirection=...`.
  - The ARM64 backend initializes and guest mini-JIT compilation continues.
  - No `Fatal signal`, tombstone, `SIGSEGV`, `SIGILL`, or indirection
    `mprotect failed` line was found in the searched log patterns.
  - Blue Dragon still stops at the known D3D frame-0 watchdog with drained
    ring pointers `CP_RB_RPTR/WPTR 0x1f/0x1f`, then `0x25/0x25`.

## Still Not Done

- The thunk function pointers are integration slots, not generated thunks yet.
- The current helper mini-JIT still calls helpers directly and still has 52
  helper-call sites.
- The real next code batch is to import or re-create the Edge A64 thunk emitter
  and stack-layout ABI, then migrate guest calls and extern calls onto that ABI
  behind a cvar so the helper path can remain a fallback.
