# Blue Dragon Edge f[1] Kill Taxonomy

## Summary

This slice extends the default-off `arm64_blue_dragon_edge_variant_audit` for the
exact hot edge `82282490:82282598 -> 82287788` with behavior-neutral `f[1]`
call-kill taxonomy counters.

The route-clean Thor capture proves that the active `f[1]` call-kill model has
no unknown kills on the opening sky / dragon-wing route:

- helper-preserved calls: `720290`
- child-preserved calls: `350636`
- return/exit calls: `710511`
- unknown call kills: `0`

This is not a speed win and is not a quiet FPS verdict. It is a planning proof
for the broader caller-local / side-table edge payload design.

## Code Changes

- Split the long `A64 Blue Dragon edge-variant audit` logcat row into base,
  taxonomy, `f1-sites`, and `kill-sites` rows to avoid Android logcat tail
  truncation.
- Added atomic counters for:
  - `active_f1_helper_preserved_calls`
  - `active_f1_child_preserved_calls`
  - `active_f1_return_exit_calls`
  - `active_f1_unknown_call_kills`
- Classified active call-kill PCs in `82287788`:
  - helper-preserved: `8228778C`, `82287EE4`
  - child-preserved: `82287854`, `82287ED4`
  - return/exit: `82287EDC`, `82288220`
- Updated `tools/thor/thor_a64_edge_variant_storage_plan.ps1` to merge the
  split rows while preserving compatibility with older single-row captures.

The audit remains behavior-neutral:

- no payload materialization
- no normal-entry replacement
- no global indirection-slot change
- no generated guest behavior change
- normal fallback remains the only behavior path

## Validation

Commands:

```powershell
$null = [System.Management.Automation.Language.Parser]::ParseFile('tools/thor/thor_a64_edge_variant_storage_plan.ps1',[ref]$null,[ref]$null)
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_edge_variant_storage_plan.ps1 -EdgeCounterLogPath scratch\thor-debug\20260525-175231-speed-logcat.txt
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_a64_edge_variant_storage_plan.ps1 -EdgeCounterLogPath scratch\thor-debug\20260525-183812-speed-logcat.txt
git diff --check
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 180 -PerfSampleSeconds "120,150,175" -Arm64BlueDragonEdgeVariantAudit true -Arm64SpeedProfileBodyTimeFilter "82282490,82287788,82281D28" -Arm64SpeedProfileBodyTimeAfterMs 120000 -StopAppAfterCapture true
```

Results:

- PowerShell parser check passed.
- Storage-plan report parses both older single-row logs and the new split-row
  capture.
- `git diff --check` passed with only expected CRLF warnings.
- `NativeCore` passed.
- `FullDeploy` passed and installed the APK on Thor.
- Thor route capture reached the visible opening sky / dragon-wing route.
- Fatal-marker search was clean.

## Capture

- Capture prefix: `scratch/thor-debug/20260525-184539-*`
- Meta: `scratch/thor-debug/20260525-184539-meta.txt`
- Logcat: `scratch/thor-debug/20260525-184539-speed-logcat.txt`
- Filtered log: `scratch/thor-debug/20260525-184539-speed-logcat-filtered.txt`
- Screenshot: `scratch/thor-debug/20260525-184539-screenshot.png`
- Final perf: `scratch/thor-debug/20260525-184539-perf-final.txt`
- APK SHA: `DEDD20333729C3E6B44CC1E423357147F0A9149D034C6D0A2DB0237AB8BF5C61`
- Meta head: `8d053566e`

The meta head is the pre-commit baseline for this uncommitted audit worktree.

## Final Edge Counters

From `tools/thor/thor_a64_edge_variant_storage_plan.ps1` on
`scratch/thor-debug/20260525-184539-speed-logcat.txt`:

- `eligible_calls=710511`
- `normal_fallbacks=710511`
- `normal_fallback_share=100.00%`
- `indirection_fallbacks=710511`
- `variant_misses=710511`
- `payload_materializations=0`
- `storage_missing=1`
- `marker_sets=710511`
- `marker_clears=710511`
- `active_f1_reads=2121019`
- `inactive_f1_reads=3436`
- `active_call_kills=1781437`
- `active_f1_reads_per_call=2.99`
- `active_call_kills_per_call=2.51`

Taxonomy:

- `helper_preserved_calls=720290`
- `child_preserved_calls=350636`
- `return_exit_calls=710511`
- `unknown_call_kills=0`
- `nonblocking_calls=1070926`
- `unknown_kills_per_call=0.00`

Active `f[1]` read sites:

- `82287798=710511`
- `82287828=350636`
- `82287CF8=350031`
- `82287D10=350031`
- `82287D8C=350031`
- `82287F1C=9779`
- `82287A1C=0`
- `82287A2C=0`
- `82287AA4=0`
- `82287EA8=0`

Active kill sites:

- `8228778C=710511`
- `82287854=350636`
- `82287ED4=0`
- `82287EDC=700732`
- `82287EE4=9779`
- `82288220=9779`

## Body-Time / Perf

Final delayed body-time rows:

- `82282490=39738917`, `ticks_per_entry=176`, `code_size=87348`
- `82281D28=8067997`, `ticks_per_entry=95`, `code_size=96908`
- `82287788=4878256`, `ticks_per_entry=7`, `code_size=37412`
- `82282388=2341735`
- `82282410=2253406`

Final thread sample remained CPU/JIT-heavy:

- Main Thread: `103%`
- XMA Decoder: `42.3%`
- GPU Commands: `7.6%`
- Draw Thread: `0.0%`

No sustained 30 FPS proof exists yet.

## Decision

Do not run a quiet speed A/B or materialize an `f[1]`-only payload from this
taxonomy patch. The previous standalone `f[1]` carrier was route-safe but did
not prove speed, and this route still shows `82282490` as the main body-time
wall.

The next useful slice is a broader edge payload/storage design for
`82282490:82282598 -> 82287788`, using the taxonomy as proof that `f[1]` has no
unknown call kills on this route while keeping `fpscr` in its separate
CFG-writeback lane.
