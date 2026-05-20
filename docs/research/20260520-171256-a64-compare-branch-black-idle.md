# 2026-05-20 17:12 EDT - Rejected A64 Compare-Branch Fusion

## Question

Can a generic integer `COMPARE_*` immediately followed by a single-use
`BRANCH_TRUE` / `BRANCH_FALSE` be lowered as `cmp` plus `b.cond`, avoiding the
temporary boolean register and `cbz` / `cbnz`?

## Attempt

An uncommitted local patch added a `TrySelectIntegerCompareBranch` matcher for:

- `COMPARE_EQ`
- `COMPARE_NE`
- `COMPARE_ULT`
- `COMPARE_ULE`
- `COMPARE_UGT`
- `COMPARE_UGE`

The matcher required the compare result to be used only by the immediately
following branch, emitted integer compare flags, then emitted the corresponding
conditional branch.

## Validation

Build and deploy both succeeded:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Thor proof:

- Capture: `scratch/thor-debug/20260520-171056-*`
- Base head before the local experiment: `c41ce6ec4`
- APK SHA-256:
  `8E63DC6A0D634A8E0F88820A557CD9C922F512D890F172E07F2DF3E1DF28D88C`
- No searched fatal markers.
- First active interval: `funcs=3653 entry_delta=3872644`.
- Second interval fell to `entry_delta=94899`.
- Later intervals at `17:11:45`, `17:12:00`, and `17:12:15` all had
  `entry_delta=0`.
- Final screenshot was black.

The first interval code sizes were identical to the previous committed
UGT/EQ CR branch peephole proof:

- `8272A3A4 code_size=12296`
- `8272A8E8 code_size=5508`
- `827294CC code_size=324`
- `826BF770 code_size=248`

## Decision

Reverted the local compare-branch fusion before commit.

Do not re-add this broad matcher without:

- a per-function hit audit;
- a small HIR-level semantics test for every condition and branch polarity;
- an idle snapshot or thread-state explanation for the Blue Dragon black idle;
- a route-clean Thor proof that reaches the same visible milestone as
  `c41ce6ec4`.

This one is not worth shipping on the current evidence.
