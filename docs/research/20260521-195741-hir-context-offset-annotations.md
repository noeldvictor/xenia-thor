# HIR Context Offset Annotations

Date: 2026-05-21 19:57 EDT

## Goal

Continue the Blue Dragon full-speed sprint without making another blind A64
codegen guess. The current opening-scene wall is still `82282490`, and prior
runtime context-audit logging perturbed the route. Improve the offline HIR tool
so it maps hot `LOAD_CONTEXT` / `STORE_CONTEXT` offsets back to PPC state
fields.

## Change

Updated:

```text
tools/thor/thor_hir_hotpath_report.ps1
```

The report now:

- annotates context offsets as `r[N]`, `f[N]`, `v[N]`, `crN.field`, `lr`,
  `ctr`, or runtime state;
- totals context loads and stores by class (`GPR`, `VMX`, `CR`, `LR/CTR`,
  etc.);
- preserves the existing opcode, permute, byte-swap, branch, call, and memory
  operation summaries.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_hotpath_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -Phase OptHIR -Top 20
```

## Findings

`82282490` OptHIR still reports:

- `hir_lines=6799`
- `store_context=1415`
- `load_context=763`
- `dot_product_4=136`
- `permutes=381`
- `context_barriers=213`

New state-class totals:

- Loads: `GPR=546`, `VMX=155`, `FPR=38`, `FP/VMX_STATUS=12`, `LR/CTR=10`,
  `CR=2`.
- Stores: `GPR=562`, `VMX=373`, `CR=343`, `LR/CTR=68`, `FPR=52`,
  `FP/VMX_STATUS=12`, `XER=5`.

Top concrete slots:

- Load-heavy GPRs: `r[1]` 107, `r[11]` 67, `r[10]` 36, `r[29]` 34,
  `r[31]` 34, `r[28]` 31, `r[30]` 31.
- Store-heavy GPRs: `r[11]` 128, `r[10]` 75, `r[3]` 53, `r[9]` 50,
  `r[4]` 49.
- Store-heavy CR fields: `cr6.all_equal` 78, `cr6.1` 78,
  `cr6.none_equal` 78, plus `cr0.lt/gt/eq` 35 each.
- Vector traffic: `v[0]`, `v[11]`, `v[12]`, `v[13]`, and high scratch-like
  vectors `v[125]..v[127]` remain prominent.

## Decision

The next full-speed sprint should prioritize real PPC state-traffic reduction,
not another narrow vector peephole. A useful first design target is keeping hot
GPR slots (`r[1]`, `r[10]`, `r[11]`, `r[28]..r[31]`) and repeated CR6 fields
live across safe HIR spans with explicit flushes at helpers, exits, exceptions,
and aliasing context barriers.

## Status

Analysis/tooling only. No runtime behavior changed and no APK rebuild is
required.
