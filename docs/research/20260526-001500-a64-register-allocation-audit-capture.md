# A64 Register Allocation Audit Capture

## Summary

FullDeploy plus a 180-second Thor Blue Dragon route capture proved the
default-off `arm64_register_allocation_audit` path is route-clean for
`0x82282490`, but it did not expose material register-allocation spill
pressure. This closes the allocator-spill-pressure lane for the current
opening-route evidence.

This is audit proof only. It is not a speed win and not full-speed proof.

## Capture

- Capture prefix: `scratch/thor-debug/20260526-000910-*`
- Commit: `8fee7f3d2`
- APK SHA256:
  `4B3B48CD3DB69B093CF3D47FA84BBC211DD09D23063F08DDD0064496FF6AC356`
- Target:
  `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`
- Cvars of interest:
  `arm64_register_allocation_audit=true`,
  `arm64_register_allocation_audit_function=0x82282490`,
  `arm64_speed_profile_body_time_filter=82282490,82281D28,82287788`,
  `arm64_speed_profile_body_time_after_ms=120000`
- Screenshot: `scratch/thor-debug/20260526-000910-screenshot.png`

The screenshot reached the visible opening sky / dragon-wing route. The
fatal-marker scan found no Xenia, AndroidRuntime, tombstone, assert, or
backtrace failure. The only signal marker was an unrelated
`com.google.android.gms.unstable` start-timeout kill.

## Register-Allocation Counters

`0x82282490` compiled with:

- `blocks=164`
- `blocks_with_spills=0`
- `instructions=9016`
- `dest_values=4517`
- `locals_before=0`
- `locals_after=0`
- `local_slots_added=0`
- `behavior_changed=0`

Per allocation set:

- INT: `dest_values=2877`, `allocation_successes=2877`,
  `spill_requests=0`, `spill_successes=0`, `max_active_registers=6`,
  `max_upcoming_uses=6`
- FLOAT: `dest_values=350`, `allocation_successes=350`,
  `spill_requests=0`, `spill_successes=0`, `max_active_registers=16`,
  `max_upcoming_uses=16`
- VEC: `dest_values=1290`, `allocation_successes=1290`,
  `spill_requests=0`, `spill_successes=0`, `max_active_registers=16`,
  `max_upcoming_uses=16`

The INT side stays within the currently exposed allocatable set, and the
FLOAT/VEC sides report no allocator spill request despite reaching 16 active
registers. A behavior patch that only changes allocator spill handling is not
justified by this route.

## Body-Time / Perf

The delayed body-time profile activated after 120 seconds. Final observed
body-time rows:

- `82282490`: `body_ticks_total=29894735`, `entries_total=186283`,
  `ticks_per_entry=145`, `code_size=87168`
- `82281D28`: `body_ticks_total=8077356`, `entries_total` last observed
  `27612`, `ticks_per_entry=93`, `code_size=96908`
- `82287788`: `body_ticks_total=3611977`, `entries_total=723905`,
  `ticks_per_entry=5`, `code_size=35732`

Final perf still shows a CPU/JIT wall rather than a broad GPU wall:

- Main Thread: `92.3%`
- GPU Commands: `11.5%`
- Draw Thread: `3.8%`

No sustained 30 FPS proof exists.

## Decision

Close the narrow allocator-spill-pressure lane for `0x82282490` on the current
route. Do not patch allocator behavior or run a quiet speed A/B from this
capture.

Next structural candidates:

1. Helper ABI and block-linking audit, focused on repeated helper/call/dispatch
   cost in the `82282490`, `82281D28`, `82287788` wall cluster.
2. Current route-stabilized PERMUTE / `LOAD_VECTOR_SHL` /
   `LOAD_VECTOR_SHR` counters before any VMX128 behavior patch.
3. Guest-state cache design only if it avoids the already closed narrow
   `r1`/`r11`, f1/edge-payload, fpscr, and first-barrier lanes.
