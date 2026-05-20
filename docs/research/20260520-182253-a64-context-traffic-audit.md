# A64 Context Traffic Audit For Blue Dragon

Date: 2026-05-20 18:22 EDT
Branch: `master`
Device: AYN Thor Max / Snapdragon 8 Gen 2 / Adreno 740
Target: Blue Dragon Disc 1, legally owned local dump on SD card

## Question

The current Blue Dragon route still spends too much time in generated A64 code.
Instead of guessing at another isolated peephole, can we measure which PPC state
traffic remains inside the hottest generated function, `8272A3A4`?

## Changes

- Added default-off A64 context-traffic audit cvars:
  - `arm64_context_traffic_audit`
  - `arm64_context_traffic_audit_function`
  - `arm64_context_traffic_audit_budget`
- Exposed them in `tools/thor/thor_xenia_debug.ps1`:
  - `-Arm64ContextTrafficAudit`
  - `-Arm64ContextTrafficAuditFunction`
  - `-Arm64ContextTrafficAuditBudget`
- Added Android intent forwarding in `EmulatorActivity.java`. The first audit
  run showed metadata extras but no native enable log because the new cvars were
  not yet copied into launch arguments.
- Added a compile-time HIR scan in `A64Emitter::MaybeLogContextTrafficAudit`.
  It logs blocks, HIR instruction count, context loads/stores, CR/GPR/LR/CTR
  classes, memory ops, branches/calls, compare-derived context stores, and top
  PPCContext offsets.
- Added one generic A64 peephole for exact PPC `UpdateCR` triplets shaped as
  `COMPARE_LT + store CR[LT]`, `COMPARE_GT + store CR[GT]`,
  `COMPARE_EQ + store CR[EQ]`.

## Commands

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
powershell -ExecutionPolicy Bypass -File tools\thor\thor_xenia_debug.ps1 -Mode LaunchBlueDragonSpeedCapture -DeviceSerial c3ca0370 -LiveCaptureSeconds 50 -PerfSampleSeconds "40" -Arm64SpeedProfileIntervalMs 15000 -Arm64SpeedProfileTopFunctions 20 -Arm64SpeedProfileMinDelta 1 -Arm64SpeedProfileBodyTimeFilter "8272A3A4" -Arm64ContextTrafficAudit true -Arm64ContextTrafficAuditFunction 0x8272A3A4 -Arm64ContextTrafficAuditBudget 4
```

## Captures

- Bridge-missing proof: `scratch/thor-debug/20260520-181535-*`
- Audit proof before triplet peephole: `scratch/thor-debug/20260520-181744-*`
- Triplet peephole proof: `scratch/thor-debug/20260520-182128-*`

APK SHA-256 for the final proof:

`44C846620EF331CDDC58E54C1DA0DAF06EEE6730C1E583117DA3BC5F0F4CC91C`

## Audit Result

For `8272A3A4`, both audit proofs reported:

```text
blocks=54 instrs=2467
context_loads=255 context_stores=442
locals=3/3 mem=105/57
byte_swaps=12 cmp=186/0 branches=73 calls=11
ppc_loads lr/ctr/gpr/cr/xer/fpr/vmx/runtime=0/0/255/0/0/0/0/0
ppc_stores=6/0/252/183/1/0/0/0
store_src cmp/const/select=183/18/0
barriers ctx/mem=85/0
load_top=0x108:46,0x118:28,0x028:27,0x110:24,0x0F8:19,0x0D8:17,0x078:15,0x070:14
store_top=0xA3C:61,0xA3D:61,0xA3E:61,0x078:31,0x070:27,0x038:24,0x068:24,0x040:20
```

Offset interpretation:

- `0xA3C..0xA3E` are CR6 bytes.
- `0x108`, `0x118`, `0x110`, `0x0F8`, `0x0D8`, `0x078`, `0x070`, and
  `0x028` are hot GPR context slots.
- The function is not GPU-bound at this point; this evidence is still CPU/A64
  backend state traffic.

## Triplet Peephole Result

The PPC `UpdateCR` triplet peephole built for `arm64-v8a` and `x86_64`, deployed,
and stayed route-clean. The final screenshot reached the Blue Dragon Voice
Language menu:

`scratch/thor-debug/20260520-182128-screenshot.png`

However, the measured hot function size did not improve:

- Before triplet peephole: `8272A3A4 code_size=12544`
- After triplet peephole: `8272A3A4 code_size=12544`

This means the matcher either did not hit `8272A3A4` or matched outside the
currently measured hot region. Keep it as a small generic route-clean cleanup,
but do not count it as a Blue Dragon speed win.

## Performance Notes

Final proof `scratch/thor-debug/20260520-182128-*` stayed alive and showed real
CPU work at the Voice Language menu:

- `Main Thread` about `76.9%`
- `XMA Decoder` about `42.3%`
- `GPU Commands` about `15.3%`

The next wall is still generated CPU plus XMA/audio cost, not Adreno rendering.

## Next

- Add a hit audit for CR compare-store shapes so we know the exact remaining
  pattern behind the `0xA3C..0xA3E` cluster.
- Start a narrow register-cache experiment for the hottest GPR offsets in
  `8272A3A4`: `r29`, `r31`, `r1`, `r30`, `r27`, `r23`, `r11`, `r10`.
- Treat the 85 context barriers as the flush/aliasing problem to solve before
  broad PPC state caching.
- Use this audit lane before more donor copying or GPU speculation.
