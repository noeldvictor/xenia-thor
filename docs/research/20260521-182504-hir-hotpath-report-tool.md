# HIR Hotpath Report Tool

Date: 2026-05-21 18:25 EDT

Branch: `master`

## Goal

Speed up the Blue Dragon OODA loop by turning large filtered HIR logcat dumps
into a small hotpath summary without another Thor run.

## Tool

Added:

```powershell
tools\thor\thor_hir_hotpath_report.ps1
```

Example:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_hotpath_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Top 20
```

The script parses filtered function dump lines for one guest function and
reports:

- top HIR opcodes,
- top context load/store offsets,
- `PERMUTE_I32` controls,
- dot-product, permute, byte-swap, branch, call, and context-barrier counts,
- memory load/store opcode families.

## `82282490` Baseline Read

Running the tool on `scratch/thor-debug/20260521-170941-speed-logcat.txt` for
`82282490` OptHIR produced:

- `hir_lines=6799`
- `store_context=1415`
- `load_context=763`
- `dot_product_4=136`
- `permutes=381`
- `permute.2=236`
- `context_barriers=213`
- `branches=203`
- `calls=60`
- `byte_swaps=32`, including `v128_byte_swaps=8`

Top context load offsets:

- `+40`: 107
- `+120`: 67
- `+112`: 36
- `+264`: 34
- `+280`: 34
- `+256`: 31
- `+272`: 31
- `+104`: 24
- `+2576`: 22
- `+544`: 21

Top context store offsets:

- `+120`: 128
- `+2620`: 78
- `+2621`: 78
- `+2622`: 78
- `+112`: 75
- `+16`: 58
- `+56`: 53
- `+104`: 50
- `+544`: 50
- `+64`: 49

Top `PERMUTE_I32` controls:

- `0x5010400`: 123
- `0x7030602`: 32
- `0x7060104`: 19
- `0x3020504`: 13
- `0x7020100`: 11
- `0x3060504`: 10

## Decision

Use this as the first-pass offline triage tool whenever a filtered HIR dump
exists. It does not replace route-clean Thor proof, but it prevents guessing
which family of codegen waste is dominant before the next patch.

Current read: `82282490` still wants bigger state-traffic work more than
another broad vector peephole. The safest next speed branch is a measured
GPR/CR context-state reduction or a title-safe exact-mask vector probe with a
correctness harness.
