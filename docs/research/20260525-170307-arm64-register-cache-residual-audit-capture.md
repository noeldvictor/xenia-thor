# A64 Guest-State Register Cache Residual Audit Capture

Date: 2026-05-25 17:03 EDT

## Goal

Validate the post-`PromoteBlock` residual guest-state register-cache audit for
Blue Dragon `0x82282490`. This was the source-gated follow-up to the
pre-promotion audit: if normal `ContextPromotionPass::PromoteBlock` already
removes same-block clean hits, the residual row should say whether any useful
`r1` / `r11` traffic remains after promotion and DSE.

## Build And Capture

- Commit: `85c422d2b`
- APK SHA-256:
  `18097BA09F0C50596DED67D4251E4401FC7D458AA5EF7AAC7365B362D14CD19A`
- FullDeploy:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370`
- Capture:
  `powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64GuestStateRegisterCacheResidualAudit true -Arm64GuestStateRegisterCacheResidualAuditFunction 0x82282490 -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true`

Capture paths:

- `scratch/thor-debug/20260525-165936-speed-logcat.txt`
- `scratch/thor-debug/20260525-165936-speed-logcat-filtered.txt`
- `scratch/thor-debug/20260525-165936-meta.txt`
- `scratch/thor-debug/20260525-165936-screenshot.png`
- `scratch/thor-debug/20260525-165936-perf-final.txt`

The screenshot reached the visible opening sky / dragon-wing route. Fatal-marker
search for fatal/crash/assert/signal markers was clean.

## Residual Audit Result

Final residual row:

```text
candidate_loads=174
candidate_stores=139
clean_hits_possible=0
dirty_hits_possible=0
normal_fallback=313
estimated_spill_pressure=2
payload_materializations_allowed=0
behavior_changed=0
```

Miss and flush reasons:

```text
miss_multi_pred=62
miss_after_call=31
miss_after_branch=63
miss_after_label=18
flush_branch=108
flush_label=163
flush_external_visibility=177
```

Per-slot totals:

```text
r1  loads/stores/clean_hits/dirty_hits/fallback = 107/11/0/0/118
r11 loads/stores/clean_hits/dirty_hits/fallback = 67/128/0/0/195
```

## Body-Time And Thread Wall

Final body-time rows stayed CPU/JIT-heavy:

```text
82282490 body_ticks_total=29927708 ticks_per_entry=145 code_size=87168
82281D28 body_ticks_total=7155788  ticks_per_entry=92  code_size=96908
82287788 body_ticks_total=3162326  ticks_per_entry=4   code_size=35732
82282388 body_ticks_total=2071249
82282410 body_ticks_total=1994466
```

Final perf sample:

```text
Main Thread  = 96.1%
GPU Commands = 11.5%
XMA Decoder  = 7.6%
Draw Thread  = 3.8%
```

## Decision

The residual audit closes the immediate `r1` / `r11` guest-state register-cache
behavior lane. After normal promotion and DSE, there are still 174 tracked
loads and 139 tracked stores in `82282490`, but there are zero residual clean
or dirty hits. The remaining traffic is blocked by multi-predecessor CFG,
calls, branches, labels, and external-visibility flushes.

Do not implement the stale same-block `r1` clean-load replacement, `r11` dirty
caching, store elision, or any quiet speed A/B from this lane.

Next useful slice: return to caller-local or side-table edge-variant payload
storage for the hot `82282490:82282598 -> 82287788` edge, with normal-entry
fallback and counter-only payload/materialization diagnostics first. Keep GPU
work as a measured parallel lane only; this capture still does not show a GPU
wall.
