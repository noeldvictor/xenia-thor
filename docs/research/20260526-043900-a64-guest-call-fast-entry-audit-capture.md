# A64 Guest-Call Fast-Entry Audit Capture

Date: 2026-05-26

## Summary

FullDeployed commit `45d43abe8` and ran a route-clean Blue Dragon capture with
the default-off `arm64_guest_call_fast_entry_audit` enabled for `0x82281D28`.
The capture proves direct-call argument traffic exists, but it does not justify
fast-entry behavior yet.

## Capture

- Capture prefix: `scratch/thor-debug/20260526-043446-*`
- Log: `scratch/thor-debug/20260526-043446-speed-logcat.txt`
- Screenshot: `scratch/thor-debug/20260526-043446-screenshot.png`
- Meta: `scratch/thor-debug/20260526-043446-meta.txt`
- Commit: `45d43abe8`
- APK SHA256:
  `86B1C890FB6C63775F5D20B99001F36E62BD088F308AD9C21D9F963F37B68F83`
- Cvars:
  `arm64_guest_call_fast_entry_audit=true`,
  `arm64_guest_call_fast_entry_audit_function=0x82281D28`,
  `arm64_guest_call_fast_entry_audit_budget=16`,
  delayed body-time filter `82282490,82281D28,82287788`.
- Route evidence: screenshot reached the visible opening sky / dragon-wing
  frame.
- Focused fatal-marker search:
  no `AndroidRuntime`, tombstone, assert, backtrace, `FATAL EXCEPTION`,
  `SIGSEGV`, `SIGABRT`, crash, or ANR rows.

## Audit Row

`0x82281D28` produced one compile-time audit row:

- `blocks=188`
- `instrs=10508`
- `direct_calls=84`
- `conditional_direct=0`
- `eligible_regular=67`
- `tail_blockers=8`
- `indirect_blockers=0`
- `extern_host_blockers=0`
- `unresolved_direct_targets=52`
- `helper_blockers=17`
- `normal_entry_fallback=67`
- `stackpoint_sensitive=67`
- `arg_store_calls=78`
- `arg_store_fields=421`
- `arg_store_bytes=3368`
- `callee_first_use_known=0`
- `callee_first_use_missing=421`
- `dirty_flush_points=268`
- `flush_context_barrier=260`
- `flush_return=0`
- `debug_trap_blockers=0`
- `already_compiled_targets=32`
- `payload_materializations_allowed=0`
- `behavior_changed=0`
- `alternate_codegen=0`
- `normal_entry=unchanged`
- `global_indirection=unchanged`
- `arg_store_top=r3=67 r4=58 r5=46 r6=27 r7=28 r8=37 r9=35 r10=45 lr=78`

## Speed Context

This was not a quiet speed A/B and should not be treated as speed proof. The
final perf sample remained CPU/JIT-heavy:

- Main Thread: `103%`
- XMA Decoder: `50.0%`
- GPU Commands: `3.8%`
- Draw Thread: `0.0%`

Final delayed body-time rows:

- `82282490=31015975`, entries `57876`, ticks/entry `141`
- `82281D28=7992965`, entries `27612`, ticks/entry `91`
- `82287788=3534990`, entries `229939`, ticks/entry `5`

## Decision

The lane has material caller-side argument store traffic, but the current
compile-time audit cannot prove callee entry safety:

- all `421` argument stores are still `callee_first_use_missing`;
- all `67` eligible regular calls still require normal-entry fallback;
- dirty/flush pressure is high, especially `260` context-barrier flush points;
- `52` direct targets are unresolved from this row.

Do not implement a fast-entry behavior patch from this capture, and do not rerun
the same `0x82281D28` audit unchanged.

## Next Useful Slice

Make the fast-entry audit callee-aware before any runtime design:

- join direct-call targets to available callee HIR or file-backed HIR coverage;
- report top target/blocker rows, not only function totals;
- distinguish already-compiled targets with known first-use reloads from
  unresolved or helper-blocked targets;
- keep normal entry and global indirection unchanged;
- keep `payload_materializations_allowed=0` and `behavior_changed=0`.

If that cannot produce broad body-weighted known first-use traffic with
manageable flush pressure, close this fast-entry lane and move to a different
structural A64 lane.
