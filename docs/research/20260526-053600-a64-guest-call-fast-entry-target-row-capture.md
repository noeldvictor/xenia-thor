# A64 Guest-Call Fast-Entry Target-Row Capture

## Scope

Capture the default-off `arm64_guest_call_fast_entry_audit` target rows added in
commit `bc00a8eaa` for Blue Dragon hot function `0x82281D28`, then join the
runtime rows with the existing callee-aware HIR audit. This is counter-only
evidence, not a speed patch.

## Capture

- FullDeploy passed on Thor `c3ca0370`.
- Capture prefix: `scratch/thor-debug/20260526-052805-*`.
- Commit: `bc00a8eaa`.
- APK SHA256:
  `555838EB4F235D1FF1AA1B63B1D9E1A7F115B74DB541DD6479F52C6C5FFBBBA2`.
- Cvars:
  - `arm64_guest_call_fast_entry_audit=true`
  - `arm64_guest_call_fast_entry_audit_function=0x82281D28`
  - `arm64_guest_call_fast_entry_audit_budget=16`
  - `arm64_speed_profile_body_time_filter=82282490,82281D28,82287788`
  - `arm64_speed_profile_body_time_after_ms=120000`
- Screenshot reached the expected visible opening sky / dragon-wing route.
- Focused fatal-marker search found no AndroidRuntime, tombstone, assert,
  backtrace, fatal exception, SIGSEGV, SIGABRT, crash, or ANR rows.

## Runtime Rows

Function summary:

- `target_rows=32`
- `direct_calls=84`
- `eligible_regular=67`
- `tail_blockers=8`
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
- `parent_pre_call_flush_points=195`
- `already_compiled_targets=32`
- `payload_materializations_allowed=0`
- `behavior_changed=0`
- `alternate_codegen=0`
- `normal_entry=unchanged`
- `global_indirection=unchanged`

Top runtime target rows by static call count / store volume:

- `0x826BF770`: `calls=11`, `already_compiled=11`, `arg_store_fields=57`,
  `parent_pre_call_flush_points=34`.
- `0x82168610`: `calls=9`, `already_compiled=9`, `arg_store_fields=54`,
  `parent_pre_call_flush_points=21`.
- `0x822750F8`: `calls=5`, `unresolved=5`, `arg_store_fields=45`,
  `parent_pre_call_flush_points=23`.
- `0x82281D28`: `calls=2`, `unresolved=2`, `arg_store_fields=18`,
  `parent_pre_call_flush_points=5`.

The target rows are compile-time/static call-site summaries. Dynamic relevance
still comes from the block body-time join.

## Joined HIR Result

Join command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_guest_call_arg_handoff_audit.ps1 -LogPath scratch\thor-debug\20260526-025401-speed-logcat.txt -ExtraLogPath scratch\thor-debug\20260526-034119-speed-logcat.txt -FastEntryAuditLogPath scratch\thor-debug\20260526-052805-speed-logcat.txt -Function 82281D28 -Phase OptHIR -Top 16
```

Output:
`scratch/thor-debug/20260526-052805-guest-call-fast-entry-target-row-join.txt`.

Joined totals:

- `argument_store_fields=439`
- `callee_first_load_stores=247`
- `callee_first_store_stores=51`
- `callee_absent_stores=15`
- `callee_hir_missing_stores=126`
- `body_weighted_argument_fields=748407618`
- `body_weighted_live_in_fields=690421033`
- `body_weighted_missing_fields=30011199`

Top body-weighted known live-in targets:

- `0x82281D28`: `body_weighted_live_in=401150187`.
- `0x826BF770`: `body_weighted_live_in=206739640`.
- `0x822750F8`: `body_weighted_live_in=42703776`.

Top body-weighted missing target:

- `0x82276E70`: `body_weighted_missing=20915802`.

## Perf Status

Final perf remains CPU/JIT-heavy:

- Main Thread: `96.1%`
- XMA Decoder: `7.6%`
- GPU Commands: `7.6%`
- Draw Thread: `3.8%`

This is not a sustained 30 FPS proof and does not move the wall to Vulkan/GPU.

## Decision

The target rows prove broad known callee-first-load traffic in the offline join,
but the runtime path still fails the behavior threshold:

- runtime compile summary still reports `callee_first_use_known=0`;
- `52/84` direct targets are unresolved at compile time;
- all `67` eligible regular calls require normal-entry fallback;
- all `67` eligible regular calls are stackpoint-sensitive;
- flush pressure is high: `dirty_flush_points=268`,
  `flush_context_barrier=260`, and `parent_pre_call_flush_points=195`.

Do not implement guest-call fast-entry behavior, do not run a quiet speed A/B,
and do not rerun the same `0x82281D28` target-row capture unchanged.

Close the current target-row capture lane. Reopen guest-call fast-entry only with
a source-level guarded-stub / late-bound-entry design that explicitly preserves
normal entry, global indirection, stackpoint/debug/exception visibility, and
dirty flush points for the top body-weighted targets. Otherwise move to the next
structural A64 lane.

Preferred next structural slice: a hot-block A64 codegen-floor/disassembly audit
for the mapped body-dominant blocks, starting with
`82281D28:8228233C-82282370`, using the deterministic block-profile mapping and
`tools/thor/thor_hir_a64_codegen_audit.ps1`. The audit should identify a
reusable backend rule before any behavior patch, with fastmem/addressing or
context-traffic counters only if the codegen floor points there.
