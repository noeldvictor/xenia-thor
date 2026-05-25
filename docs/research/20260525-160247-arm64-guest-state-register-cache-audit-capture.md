# A64 Guest-State Register-Cache Audit Capture

## Summary

Commit-under-test: `3b02ff758`

APK SHA-256:
`4ED7B6FE876F5ADCB7D11F70BDC42AC4D7A1BF916D680E4B22BEA861069440D6`

This slice FullDeployed the default-off, counter-only
`arm64_guest_state_register_cache_audit` APK and captured Blue Dragon on the
AYN Thor. The first capture used the bare function filter `82282490` and
reached the visible route, but emitted no register-cache audit rows. The rerun
changed only the function filter to `0x82282490`; the audit rows landed. Future
guest-PC filters for this launcher path should use the `0x` form.

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370

powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64GuestStateRegisterCacheAudit true -Arm64GuestStateRegisterCacheAuditFunction 0x82282490 -Arm64SpeedProfileBodyTimeFilter "82282490,82281D28,82287788" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

## Capture

- Log: `scratch/thor-debug/20260525-155824-speed-logcat.txt`
- Filtered log: `scratch/thor-debug/20260525-155824-speed-logcat-filtered.txt`
- Metadata: `scratch/thor-debug/20260525-155824-meta.txt`
- Screenshot: `scratch/thor-debug/20260525-155824-screenshot.png`
- Final perf: `scratch/thor-debug/20260525-155824-perf-final.txt`
- Visible route: opening sky / dragon-wing
- Fatal-marker search: clean; no matches for fatal signal, AndroidRuntime,
  device lost, GPU hung, ANR, abort, segfault, crash, exception, or assert.

The metadata records:

```text
arm64_guest_state_register_cache_audit=true
arm64_guest_state_register_cache_audit_function=0x82282490
arm64_speed_profile_body_time_filter=82282490,82281D28,82287788
arm64_speed_profile_body_time_after_ms=120000
```

## Audit Rows

The audit is behavior-neutral:

```text
blocks=164 labeled_blocks=163 multi_pred_blocks=116
candidate_loads=778 candidate_stores=285
clean_hits_possible=357 dirty_hits_possible=247
normal_fallback=1063 estimated_spill_pressure=2
payload_materializations_allowed=0 behavior_changed=0
```

Miss and flush detail:

```text
miss_no_entry=0 miss_multi_pred=62 miss_volatile=0 miss_overlap=0
miss_after_call=31 miss_after_helper=0 miss_after_branch=63
miss_after_label=18 miss_after_return=0 miss_after_trap=0
miss_external_visibility=0

flush_call=0 flush_helper=0 flush_branch=108 flush_label=163
flush_return=0 flush_trap=0 flush_external_visibility=177
```

Per-slot totals:

```text
r1 loads/stores/clean_hits/dirty_hits/fallback=433/11/322/4/444
r11 loads/stores/clean_hits/dirty_hits/fallback=345/274/35/243/619
```

## Body-Time And Thread State

The delayed body-time filter activated after `120001ms`. Final rows:

```text
82282490 body_ticks_total=31080764 entries_delta=62328 ticks_per_entry=152 code_size=87168
82281D28 body_ticks_total=7471709 entries_delta=29736 ticks_per_entry=91 code_size=96908
82287788 body_ticks_total=3379246 entries_delta=246906 ticks_per_entry=5 code_size=35732
82282388 body_ticks_total=2165366
82282410 body_ticks_total=2084225
```

Final thread sample:

```text
Main Thread 100%
XMA Decoder 46.1%
GPU Commands 7.6%
```

This keeps the route CPU/JIT-heavy. It does not justify a broad Vulkan pivot.

## Decision

The audit is route-clean and useful enough to open a default-off behavior
prototype, but not broad enough for an unconstrained register-cache rewrite.
The safest next behavior slice is:

- function-filtered to `0x82282490`;
- start with `r1` clean load replacement only;
- no store elision;
- no `r11` dirty-store caching yet;
- flush on the same call/helper/branch/label/return/trap/external-visibility
  boundaries used by the audit;
- keep `payload_materializations_allowed=0` and normal fallback available;
- first proof should be route-clean Thor correctness/counter capture, not a
  quiet speed A/B.

If the `r1` behavior prototype is route-clean, then run a same-APK quiet
control sandwich. If it is not route-clean or hit volume collapses, fall back
to caller-local/side-table edge-variant storage for
`82282490:82282598 -> 82287788`.
