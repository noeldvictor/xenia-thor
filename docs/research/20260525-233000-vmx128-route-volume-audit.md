# Blue Dragon VMX128 Route-Volume Audit

## Summary

After completing PACK/UNPACK unit coverage, the next gate was route-volume
evidence before any VMX128 behavior patch. This pass extends the deterministic
HIR vector-shape report to count `pack` and `unpack` and to emit block-profile
weighted vector totals.

Result: do not patch PACK/UNPACK behavior yet. PACK is absent in the audited
hot functions, and UNPACK appears statically in `82282490` / `82281D28` but has
zero weighted hot-block volume in the available block-profile captures. PERMUTE
and LOAD_VECTOR_SHL/SHR have stronger dynamic evidence, especially in
`82287788`.

This is an offline route-volume report, not a Thor speed proof and not a
behavior change.

## Inputs

- Latest quiet body-time reference:
  `scratch/thor-debug/20260525-195142-speed-logcat.txt`
- `82282490` HIR:
  `scratch/thor-debug/20260521-170941-speed-logcat.txt`
- `82282490` block profile:
  `scratch/thor-debug/20260524-014437-speed-logcat.txt`
- `82281D28` HIR and block profile:
  `scratch/thor-debug/20260524-050427-speed-logcat.txt`
- `82287788` HIR and block profile:
  `scratch/thor-debug/20260524-041413-speed-logcat.txt`

The HIR and block-profile logs are not from a single all-in-one current capture,
so treat the weighted totals as route-shape evidence, not exact current FPS
accounting.

## Latest Body-Time Wall

From `20260525-195142` final delayed body rows:

- `82282490`: `body_ticks_total=39940278`, `ticks_per_entry=209`
- `82281D28`: `body_ticks_total=8099644`, `ticks_per_entry=96`
- `82287788`: `body_ticks_total=6069873`, `ticks_per_entry=8`

The sprint remains Main Thread / generated-code bound. This does not justify a
broad Vulkan pivot.

## Route-Volume Results

### `82282490`

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_vector_shape_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -BlockProfileLog scratch\thor-debug\20260524-014437-speed-logcat.txt -Top 24
```

Result:

- static `permute=381`
- static `pack=0`
- static `unpack=20`
- static `load_vector_shl=73`
- static `load_vector_shr=64`
- weighted `permute=2566935`
- weighted `pack=0`
- weighted `unpack=0`
- weighted `load_vector_shl=1711290`
- weighted `load_vector_shr=0`

### `82281D28`

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_vector_shape_report.ps1 -LogPath scratch\thor-debug\20260524-050427-speed-logcat.txt -Function 82281D28 -Phase OptHIR -BlockProfileLog scratch\thor-debug\20260524-050427-speed-logcat.txt -Top 24
```

Result:

- static `permute=381`
- static `pack=0`
- static `unpack=20`
- static `load_vector_shl=73`
- static `load_vector_shr=64`
- weighted `permute=1320723`
- weighted `pack=0`
- weighted `unpack=0`
- weighted `load_vector_shl=880482`
- weighted `load_vector_shr=0`

### `82287788`

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_vector_shape_report.ps1 -LogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -Function 82287788 -Phase OptHIR -BlockProfileLog scratch\thor-debug\20260524-041413-speed-logcat.txt -Top 24
```

Result:

- static `permute=132`
- static `pack=0`
- static `unpack=0`
- static `load_vector_shl=60`
- static `load_vector_shr=36`
- weighted `permute=14885978`
- weighted `pack=0`
- weighted `unpack=0`
- weighted `load_vector_shl=6837296`
- weighted `load_vector_shr=4038444`

## Decision

PACK/UNPACK is coverage-complete but not currently a behavior-patch target.
The available route-volume evidence says the next VMX128 behavior candidate
should be PERMUTE / LOAD_VECTOR_SHL / LOAD_VECTOR_SHR, or the sprint should
return to the broader structural register allocation / guest-state-cache lane.

Do not run a quiet speed A/B from this report. No behavior changed.

## Next

If staying in VMX128, collect a current route-stabilized Thor capture scoped to
PERMUTE and LOAD_VECTOR_SHL/SHR with matching body/block profile rows, then
choose one opcode-family lowering with tests and a route-clean fallback. If not
doing that immediately, prefer the structural A64 register allocation /
guest-state-cache lane because the body-time wall is still generated-code
overhead, not renderer work.
