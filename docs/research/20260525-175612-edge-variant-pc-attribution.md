# Blue Dragon Edge-Variant Per-PC Attribution

## Summary

This slice extends the default-off `arm64_blue_dragon_edge_variant_audit` with
per-PC active `f[1]` read counters and per-PC active call-kill counters for the
exact hot edge `82282490:82282598 -> 82287788`.

The patch is still behavior-neutral: no payload materialization, no normal-entry
replacement, no global indirection-slot change, no generated guest-state
shortcut, and normal fallback remains the only behavior.

## Patch Shape

- Added active `f[1]` read-site counters for:
  `82287798`, `82287828`, `82287A1C`, `82287A2C`, `82287AA4`, `82287CF8`,
  `82287D10`, `82287D8C`, `82287EA8`, and `82287F1C`.
- Added active call-kill-site counters for:
  `8228778C`, `82287854`, `82287ED4`, `82287EDC`, `82287EE4`, and `82288220`.
- Extended `tools/thor/thor_a64_edge_variant_storage_plan.ps1` to parse the
  new optional per-PC fields while preserving compatibility with older logs.

## Validation

- `NativeCore` passed.
- `FullDeploy` passed.
- `git diff --check` passed before the Thor run.
- PowerShell parser validation passed for
  `tools/thor/thor_a64_edge_variant_storage_plan.ps1`.
- The updated storage-plan report parsed the previous
  `scratch/thor-debug/20260525-172734-*` log with missing per-PC fields as
  zero, so older captures remain readable.

## Thor Captures

First run:

- Prefix: `scratch/thor-debug/20260525-174834-*`.
- APK SHA:
  `FC9CE27AA7B7040CC741417B8B225B351A9624F43BCA444B4CA02480D57068E8`.
- Fatal-marker search was clean.
- Final screenshot was black and the edge counters stayed zero.
- Treat this as an invalid route capture, not evidence for the counters.

Retry:

- Prefix: `scratch/thor-debug/20260525-175231-*`.
- APK SHA:
  `FC9CE27AA7B7040CC741417B8B225B351A9624F43BCA444B4CA02480D57068E8`.
- Cvars: `arm64_blue_dragon_edge_variant_audit=true`,
  `arm64_speed_profile_body_time_filter=82282490,82287788,82281D28`,
  `arm64_speed_profile_body_time_after_ms=120000`,
  `arm64_speed_profile_interval_ms=15000`.
- Reached the visible opening sky / dragon-wing route.
- Fatal-marker search was clean.

## Final Dynamic Counters

Final storage-plan summary:

```text
edge parent=82282490 call_pc=82282598 callee=82287788 eligible_calls=675279 delta_calls=246624 normal_fallbacks=675279 normal_fallback_share=100.00% indirection_fallbacks=675279 variant_misses=675279 payload_materializations=0 storage_missing=1 call_kills=1697224 call_kills_per_call=2.51
payload_scope marker_sets=675279 marker_clears=675279 active_f1_reads=2015839 inactive_f1_reads=3054 active_call_kills=1693099 active_f1_reads_per_call=2.99 active_call_kills_per_call=2.51
payload_pc active_f1_sites=active_f1_82287798=675279,active_f1_82287828=333248,active_f1_82287a1c=0,active_f1_82287a2c=0,active_f1_82287aa4=0,active_f1_82287cf8=332673,active_f1_82287d10=332673,active_f1_82287d8c=332673,active_f1_82287ea8=0,active_f1_82287f1c=9293
payload_pc active_kill_sites=active_kill_8228778c=675279,active_kill_82287854=333248,active_kill_82287ed4=0,active_kill_82287edc=665986,active_kill_82287ee4=9293,active_kill_82288220=9293
```

Read distribution:

- `82287798` fires once per eligible edge call.
- `82287828` fires for about half the edge calls.
- `82287CF8`, `82287D10`, and `82287D8C` each fire for about half the edge
  calls.
- `82287F1C` is rare.
- `82287A1C`, `82287A2C`, `82287AA4`, and `82287EA8` did not fire on this
  route segment.

Kill distribution:

- `8228778C` fires once per eligible edge call under the current broad kill
  model.
- `82287854` fires for about half the edge calls.
- `82287EDC` fires for nearly every active call that reaches the later path.
- `82287ED4` did not fire on this segment.
- `82287EE4` and `82288220` are rare.

## Body-Time And Perf

Final body rows stayed CPU/JIT-heavy:

- `82282490 body_ticks_total=34275577`, `ticks_per_entry=172`,
  `code_size=87348`.
- `82281D28 body_ticks_total=7553858`, `ticks_per_entry=93`,
  `code_size=96908`.
- `82287788 body_ticks_total=4474801`, `ticks_per_entry=6`,
  `code_size=37244`.
- `82282388 body_ticks_total=2191723`.
- `82282410 body_ticks_total=2109329`.

Final perf stayed Main Thread-bound:

- Main Thread `100%`.
- GPU Commands `11.5%`.
- Draw Thread `7.6%`.
- XMA Decoder `7.6%`.
- Audio Worker `3.8%`.

## Decision

The per-PC attribution proves the active scope is real and stable, but it also
shows that a generic "payload dies on every call" rule is too coarse for a
read-only `f[1]` payload. `8228778C` is currently counted as a kill every time,
yet prior helper review says the nearby GPR/LR helper path should not clobber
FPR state. Conversely, the half-route `82287854` and late `82287EDC` kill sites
still need source/HIR classification before a behavior patch.

Do not materialize an edge payload and do not run a quiet speed A/B from this
audit-only patch. The next useful slice is a source-backed `f[1]` kill-taxonomy
split for the same exact edge:

- classify helper-preserved vs child/unknown-call kills separately;
- keep `fpscr` out of the `f[1]` read-only lane;
- preserve normal entry and indirection fallback;
- only consider a payload prototype if the route-clean counters prove enough
  active `f[1]` reads survive the refined kill rules.
