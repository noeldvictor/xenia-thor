# A64 Guarded-Stub Entry Design Audit

## Scope

After the no-wrap memory lane closed, the next structural option was to decide
whether the guest-call fast-entry idea can reopen as a guarded-stub /
late-bound-entry design. This slice is source/tooling only. It changes no
generated behavior and is not speed proof.

## Tooling

Added:

```powershell
tools/thor/thor_a64_guarded_stub_entry_design_audit.ps1
```

Ran:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_guarded_stub_entry_design_audit.ps1
```

Output:

```text
scratch/thor-debug/20260526-084500-a64-guarded-stub-entry-design-audit.txt
```

## Findings

The audit confirms the current source contract:

- `normal_entry_singleton=true`: `A64Function` still exposes one
  `machine_code_` pointer and no alternate entry storage.
- `global_indirection_singleton=true`: the assembler publishes one normal
  target through the code-cache indirection table.
- `normal_entry_abi_only_return_x0=true`: normal guest entry receives only the
  PPC return address in `x0`.
- `direct_calls_use_normal_machine_code=true`: compiled direct calls load
  `fn->machine_code()`, load `GUEST_CALL_RET_ADDR` into `x0`, and branch to
  normal entry.
- `late_bound_unresolved_stays_normal=true`: indirection and `ResolveFunction`
  paths jump to the same normal entry.
- `stackpoint_reentry_contract=true`: guest frames push stackpoints and
  synchronize after calls.
- `host_debug_exception_boundaries=true`: host callbacks, debug breaks,
  exceptions, and FPCR restore points require normal visibility.

The missing source contracts are the important result:

- `alternate_entry_storage_present=false`
- `payload_abi_storage_present=false`
- `behavior_fast_entry_present=false`

The previous route target-row capture still supplies the runtime blockers:

- `unresolved_direct_targets=52`
- `normal_entry_fallback=67`
- `stackpoint_sensitive=67`
- `dirty_flush_points=268`
- `callee_first_use_known=0`

## Decision

Do not implement guest-call fast-entry behavior from the current source shape.
Specifically:

- do not replace `A64Function::machine_code`;
- do not rewrite the global indirection slot;
- do not materialize an `r3-r10/lr` payload without a dirty-flush ABI.

The behavior lane is blocked until a separate data model exists for:

- a per-function fast-entry pointer or offset;
- direct-callsite guard and normal fallback;
- payload ABI for `r3-r10/lr`;
- dirty flush before context barriers, helpers, host calls, debug/trap
  visibility, tail calls, returns, and exceptions;
- late-bound target resolution;
- stackpoint resume semantics.

## Next Slice

Do not make the next default slice another guest-call fast-entry behavior probe.
Move to another structural A64 lane, with guest-state/cache design as the
preferred default only if it avoids the closed `r1/r11/f1/fpscr/barrier` lanes.

Reopen fast-entry only if the slice is explicitly a source/data-model patch for
the missing alternate-entry and payload-flush contracts, with generated behavior
still unchanged until the contracts and counters are proven.
