# A64 Guest-Call Fast-Entry Feasibility

Date: 2026-05-26
Branch: `master`
Latest prerequisite evidence:

- `docs/research/20260526-033000-guest-call-arg-handoff-audit.md`
- `docs/research/20260526-034500-guest-call-hir-coverage-capture.md`

## Question

Can the current A64 backend reduce the direct guest-call argument round trip
where the parent stores `r3-r10/lr` to `PPCContext` and the callee reloads the
same values, without hiding guest-visible state or breaking normal entry?

## Tooling

Added:

```powershell
tools/thor/thor_a64_guest_call_fast_entry_feasibility.ps1
```

Validation:

```powershell
$tokens=$null; $errors=$null; $null=[System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path 'tools\thor\thor_a64_guest_call_fast_entry_feasibility.ps1'), [ref]$tokens, [ref]$errors)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_guest_call_fast_entry_feasibility.ps1
```

## Source Findings

The audit reports:

```text
check normal_entry_singleton=true
check host_to_guest_entry_passes_only_return_x0=true
check guest_entry_abi_documented_return_x0=true
check direct_guest_call_passes_only_return_x0=true
check tail_indirect_unresolved_need_normal_fallback=true
check stackpoint_longjmp_constraints_active=true
check guest_to_host_boundary_clobbers_payload=true
check debug_exception_visibility_requires_normal_mapping=true
check fixed_register_pressure_is_real=true
check existing_fast_entry_skeleton_absent=true
verdict=feasible_only_as_separate_fast_entry_path
```

Relevant source anchors:

- `src/xenia/cpu/backend/a64/a64_function.h`: one `machine_code_` pointer backs
  normal guest function entry.
- `src/xenia/cpu/backend/code_cache_base.h`: one guest-address indirection slot
  is published for normal entry.
- `src/xenia/cpu/backend/a64/a64_backend.cc`: host-to-guest entry sets fixed
  registers and passes only the guest PPC return address in `x0`.
- `src/xenia/cpu/backend/a64/a64_stack_layout.h`: generated guest function
  entry documents `x0` as the guest PPC return address.
- `src/xenia/cpu/backend/a64/a64_emitter.cc`: direct guest calls load
  `fn->machine_code()`, load `GUEST_CALL_RET_ADDR` into `x0`, and `blr` to the
  callee. Tail, indirect, unresolved, and extern paths have separate fallback
  requirements.
- `src/xenia/cpu/backend/a64/a64_emitter.cc` and
  `src/xenia/cpu/backend/a64/a64_backend.cc`: stackpoint and longjmp recovery
  are active, and host callbacks save/restore broad guest state plus FPCR.

## Decision

Do not implement a behavior patch that replaces `A64Function::machine_code()`
or rewrites the global indirection slot. That would break the normal-entry
contract, unresolved/indirect calls, host entries, debugging, exception
mapping, and stackpoint/longjmp assumptions.

The lane remains plausible only as a separate fast-entry model:

- normal entry unchanged;
- direct callsite guard only;
- separate fast-entry stub or per-function fast-entry offset;
- explicit payload for `r3-r10/lr`;
- explicit dirty flush back to `PPCContext` before barriers, helpers, host
  calls, debug/trap visibility, tail calls, returns, exceptions, and fallback;
- indirect, unresolved, extern, and tail calls stay on normal entry.

## Next Slice

Next useful work is a default-off counter-only
`arm64_guest_call_fast_entry_audit` skeleton. It should compile no alternate
code and change no generated behavior. It should count:

- eligible direct calls;
- tail, indirect, unresolved, extern, debug/trap, helper, host-call, and
  stackpoint-sync blockers;
- callee first-use coverage for `r3-r10/lr`;
- normal-entry fallback pressure;
- estimated avoidable context store/load traffic;
- required dirty flush points.

No quiet speed A/B should run until the audit proves enough route-wide volume
and manageable flush pressure for a separate fast-entry implementation.
