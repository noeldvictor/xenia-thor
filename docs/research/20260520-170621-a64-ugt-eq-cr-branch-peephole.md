# 2026-05-20 17:06 EDT - A64 UGT/EQ CR Branch Peephole

## Context

The post-peephole body-time run still put Blue Dragon's `8272A3A4` at the top
of generated-code time. The hot `8272A8B4` byte loop has the PPC `cmpli`
lowering shape:

- `COMPARE_ULT value, 0`, store CR LT
- `COMPARE_UGT value, 0`, store CR GT
- `COMPARE_EQ value, 0`, store CR EQ
- branch on the EQ result

The LT store is already folded by the unsigned-zero compare cleanup. The next
safe target is sharing flags between the adjacent GT and EQ rows.

## Change

Added `TrySelectUnsignedGtEqCompareStores` in
`src/xenia/cpu/backend/a64/a64_sequences.cc`.

The matcher only fires when all of these are true:

- first instruction is `COMPARE_UGT`;
- its only use is the immediately following `STORE_CONTEXT`;
- the next compare is `COMPARE_EQ` with identical integer operands;
- the two stores target PPC CR `GT` and adjacent `EQ` bytes,
  `offsetof(PPCContext, cr0) + 4*n + 1/+2`;
- the EQ result is used only by the EQ store and an optional immediately
  following branch.

Emission changes from:

```text
cmp
cset HI
strb GT
cmp
cset EQ
strb EQ
cbz/cbnz EQ-result
```

to:

```text
cmp
cset HI
strb GT
cset EQ
strb EQ
b.ne/b.eq
```

This is not the earlier rejected pair matcher. That attempt did not handle the
branch use of the EQ value, so it did not shrink the hot function. This version
explicitly allows and consumes the branch while preserving CR bytes.

## Validation

Build and deploy:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both succeeded.

Body-time proof:

- Capture: `scratch/thor-debug/20260520-170226-*`
- Branch/head at launch: `master` / `095f185c2`
- APK SHA-256:
  `A7ED0CE9547441992F0EAC297EDE425FA81D97BA6AFB5DDE5E44EB155C9F5E5F`
- Command included body-time filter:
  `8272A3A4,8272A8E8,826C5620,827294CC,826BF770`
- No searched fatal markers.
- Final screenshot showed the Blue Dragon loading spinner.
- Final body interval:
  - `8272A3A4 body_ticks_delta=19940185 entries_delta=1277068 ticks_per_entry=15 code_size=12544`
  - `826C5620 body_ticks_delta=1632137 entries_delta=2148263 code_size=576`
  - `827294CC body_ticks_delta=1332280 entries_delta=1761869 code_size=412`
  - `826BF770 body_ticks_delta=926431 entries_delta=1025678 code_size=296`
  - `8272A8E8 body_ticks_delta=656395 entries_delta=615959 code_size=5756`

Clean speed proof:

- Capture: `scratch/thor-debug/20260520-170433-*`
- APK SHA-256:
  `A7ED0CE9547441992F0EAC297EDE425FA81D97BA6AFB5DDE5E44EB155C9F5E5F`
- No body-time filter.
- No searched fatal markers.
- Final screenshot was black, but A64 counters stayed active through the final
  interval.
- Clean code-size deltas versus the previous clean proof:
  - `8272A3A4 12332 -> 12296`
  - `8272A8E8 5520 -> 5508`
- First active interval also showed additional small shrink in frequent
  functions:
  - `827294CC code_size=324`
  - `826BF770 code_size=248`
- Final clean interval:
  - `funcs=4695 entry_delta=5436916`
  - top entries: `827294CC delta=1365881`, `8272A3A4 delta=903231`,
    `8272A8E8 delta=564994`

## Result

Safe measured win, but still nowhere near 30 FPS. The hot loop is still
dominated by repeated context loads/stores and byte-at-a-time generated code.
The next meaningful target is either a register-cached CR/GPR region for
`8272A3A4` or a fully proven native fastpath for the full state machine around
the `8272A8B4` loop, not another isolated constant-lowering tweak.
