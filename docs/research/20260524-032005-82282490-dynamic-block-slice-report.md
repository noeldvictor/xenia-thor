# 82282490 Dynamic Block Slice Report

Date: 2026-05-24 03:20 EDT

## Goal

Map dynamic block-body profiler guests such as `8228252C` back to exact
`82282490` OptHIR/PPC spans before writing another A64 codegen experiment.
The previous attribution capture proved the recursive `822825E0 -> 82282490`
wall is mostly callee body work, so the next local patch needs a body-backed
span rather than a coarse HIR label.

## Tool

Added:

- `tools/thor/thor_hir_dynamic_block_slice_report.ps1`

Command:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_dynamic_block_slice_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 12
```

Inputs:

- Filtered OptHIR dump:
  `scratch/thor-debug/20260521-170941-speed-logcat.txt`
- Same-run body/call-edge capture:
  `scratch/thor-debug/20260524-030450-speed-logcat.txt`

The report slices from each generated-code block guest PC to the next profiled
dynamic block guest or HIR label, then joins body ticks, call-edge ticks,
approximate exclusive ticks, PPC op counts, HIR op counts, context slots, and
memory/branch/call/barrier counts.

## Key Output

Top local candidates after subtracting same-run direct call-edge body time:

| Guest | Span | Body total | Edge body total | Approx exclusive | Exclusive share | Shape |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `8228252C` | `8228252C-822825C4` | 8530471 | 5653971 | 2876500 | 33.72% | vector/FPR/store-heavy |
| `822824F0` | `822824F0-82282528` | 3501617 | 1480443 | 2021174 | 57.72% | setup/call/vector-load-heavy |
| `822824B8` | `822824B8-822824E8` | 1099164 | 0 | 1099164 | 100% | branch/CR classification |
| `822825F4` | `822825F4-822825FC` | 1033964 | 0 | 1033964 | 100% | linked-list advance/branch |

The `822825E0` recursive block remains the biggest inclusive block, but its
local slice is not the patch target:

- `822825E0-822825F0`
- body total `73427065`
- edge to `82282490` `72194274`
- approximate exclusive `1232791`, only `1.68%`

## 8228252C Slice

The dynamic block guest `8228252C` is inside the older `_label0007`/`loc_822824F0`
HIR region, not a separate HIR label. The exact slice is now:

```text
8228252C addi r5, r0, 0x8
82282530 vsldoi vr0, vr0, vr0, (UNHANDLED SHB)
82282534 lvx128 vr11, r0, r9
82282538 addi r9, r1, 0x80
8228253C lvx128 vr9, r0, r8
82282540 lvx128 vr8, r0, r7
82282544 addi r8, r1, 0x50
82282548 addi r7, r0, 0x4
8228254C vor vr0, vr13, vr0
82282550 addi r3, r1, 0x50
82282554 lvx128 vr10, r0, r9
...
822825C4 stbx r9, r11, r10
```

Shape summary:

- PPC ops: `addi:7`, `vspltw:6`, `lvx128:4`, `lwz:4`, `stvewx:3`,
  `vmaddfp:3`, plus compare/load/store tail work.
- HIR ops: `store_context:27`, `load_context:14`, `add:11`, `and:11`,
  `extract:9`, `load.1:6`, `splat:6`, `truncate:5`.
- Context loads: `r[29]:2`, `r[1]:1`, `r[27]:1`, `r[30]:1`, `r[31]:1`,
  `fpscr:1`, `r[3]:1`, `r[6]:1`.
- Context stores: `r[11]:2`, `r[9]:2`, CR0/CR6 fields, vector state, and
  call argument GPRs.
- Dynamic edge: `8228252C -> 82287788` accounts for `5653971` ticks, so the
  local slice is still meaningful but not the whole block.

## Decision

- Do not patch from the broad `loc_822824F0` label anymore; use the dynamic
  slice address range.
- Do not optimize `822825E0` as generic recursive call overhead; its block is
  almost entirely child body time.
- The next codegen audit should focus on the `8228252C-822825C4` local vector
  and state-traffic shape, especially `stvewx`/dynamic extract/store lowering,
  repeated splat/extract churn, and context round-trips around the vector stack
  scratch path.
- Any code change should be default-off and function/block-gated until route
  proof exists.

## Next Action

Run an A64 generated-code audit for the `8228252C-822825C4` slice, then choose
one guarded experiment. The first candidate is likely a block-gated vector
store/extract lowering improvement, but it needs generated A64 evidence before
implementation.
