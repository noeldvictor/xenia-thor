# A64 UGT/EQ Context Pair No-Hit

Date: 2026-05-20 16:41 America/New_York

## Question

Can the A64 backend fuse the common CR flag pair `COMPARE_UGT x, 0` followed by
`COMPARE_EQ x, 0`, when both results are immediately stored to PPC context?

## Attempt

An uncommitted local experiment added a `SelectSequence` peephole that tried to
match:

- `COMPARE_UGT value, 0`
- single-use `store_context`
- `COMPARE_EQ same_value, 0`
- single-use `store_context`

The intended generated shape was one shared `cmp value, 0`, then `cset NE`,
store, `cset EQ`, store. This would remove one redundant compare when the HIR
uses the same value for both CR greater/equal flags.

## Build

Commands:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both commands completed successfully.

## Validation

Capture:

- `scratch/thor-debug/20260520-163945-*`
- APK SHA-256:
  `618BC8C0F5B55000C2C2ED557B0A6401F2CC4980A3B3A403CEDAC3000DE14FFB`
- Metadata head: `6fb53f1d2` plus uncommitted local code.
- No searched native fatal markers.
- Process stayed in `EmulatorActivity`.
- Screenshot reached the Blue Dragon `Voice Language` menu.
- First active interval: `funcs=3528`, `entry_delta=3802266`.
- Final interval: `funcs=4390`, `entry_delta=2036864`.
- Final `top -H`: Main Thread about 80.7%, XMA Decoder about 38.4%, GPU
  Commands about 19.2%.

Measured hot function sizes stayed identical to the pushed
compare-to-context-store peephole proof:

- `8272A3A4`: stayed `12332`.
- `8272A8E8`: stayed `5520`.

## Result

Do not keep this matcher as written. It was route-safe but produced no clean
hot-function shrink, which means the exact HIR adjacency/value identity it
expected either did not occur in the emitted hot functions or did not affect the
measured path.

The local code was reverted before commit. The pushed backend remains at
`6fb53f1d2`, with only the proven false-compare-to-context-store peephole.

## Next

- If revisiting this family, first add an audit/log-only matcher to count exact
  `UGT/EQ` context-store adjacency hits in `8272A3A4`.
- Higher-value work is still likely register-cached PPC GPR/CR lowering or a
  fully proven native region fastpath for the `8272A3A4` state machine.
