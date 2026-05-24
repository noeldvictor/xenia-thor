# Blue Dragon 8228252C Call-Boundary State Audit

Date: 2026-05-24 08:36 EDT

## Goal

Move past the narrow `stvewx` lane-fold and three-PC `MUL_ADD_V128` fastpath
probes by auditing the broader `8228252C-822825C4` span at its hottest child
edge: `8228252C -> 82287788`.

The question is not yet "which store can we delete?" It is: which parent
`store_context` writes before the direct call are actually live into the child
callee, and which writes look like call-boundary state traffic that a future
default-off direct-call/state-handoff experiment might avoid?

## Tool

Added:

- `tools/thor/thor_hir_call_boundary_state_audit.ps1`

The tool compares:

- parent pre-call `store_context` rows in a body-backed span;
- the callee function's first `load_context`/`store_context` per context slot;
- the parent function's first post-call context access per slot.

It classifies each parent pre-call store as:

- `callee_live_in`: child loads that context slot before writing it;
- `parent_reads_after_call`: child does not need it, but later parent code
  linearly reads it before overwriting it;
- `callee_dead_parent_dead_linear`: child writes or never touches it before
  read, and later parent code linearly overwrites it or does not touch it.

This is a linear HIR dependency audit. It is not proof that stores can be
removed. Branches, exceptions, debug stops, helper side effects, and exact
guest-state visibility still need an experiment gate.

## Command

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_call_boundary_state_audit.ps1 -ParentLogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -ParentFunction 82282490 -ParentStartGuest 8228252C -ParentEndGuest 822825C4 -CalleeLogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -CalleeFunction 82287788 -Phase OptHIR -Top 40
```

## Key Output

The parent span is still the body-backed `8228252C-822825C4` local target:

- parent edge: `8228252C -> 82287788`
- prior body/call-edge evidence:
  - `body_ticks_total=8530471`
  - child edge body `5653971`
  - approximate exclusive parent work `2876500`

Pre-call parent state stores before the direct call:

```text
parent_pre_call_store_context=17
approx_state_bytes=188
by_class=vmx:7,gpr:5,fpr:3,control:1,fpscr:1
bytes_by_class=vmx:112,gpr:40,fpr:24,control:8,fpscr:4
classification_counts=callee_dead_parent_dead_linear:13,callee_live_in:4
classification_approx_bytes=callee_dead_parent_dead_linear:160,callee_live_in:28
```

The four stores that are live into `82287788` are expected call inputs:

| Parent Store | Field | Callee First Use |
| --- | --- | --- |
| `82282550:addi` | `r[3]` | `load@8228779C:or` |
| `82282594:fmuls` | `fpscr` | `load@82287A1C:fsubs` |
| `82282594:fmuls` | `f[1]` | `load@82287798:stfs` |
| `82282598:bl` | `lr` | `load@82287788:mfspr` |

The 13 linear-dead candidates are the broader traffic we should study next:

| Parent Store | Field | Class | Callee First | Parent After First |
| --- | --- | --- | --- | --- |
| `82282534:lvx128` | `v[11]` | VMX | store | store |
| `8228253C:lvx128` | `v[9]` | VMX | store | store |
| `82282540:lvx128` | `v[8]` | VMX | store | store |
| `82282554:lvx128` | `v[10]` | VMX | store | store |
| `82282574:vspltw` | `v[13]` | VMX | store | store |
| `82282578:vspltw` | `v[12]` | VMX | store | store |
| `8228257C:vspltw` | `v[0]` | VMX | store | store |
| `8228252C:addi` | `r[5]` | GPR | store | store |
| `82282544:addi` | `r[8]` | GPR | store | store |
| `82282548:addi` | `r[7]` | GPR | store | store |
| `82282558:addi` | `r[9]` | GPR | store | store |
| `8228258C:lfs` | `f[13]` | FPR | store | store |
| `82282590:lfs` | `f[0]` | FPR | store | store |

## Interpretation

The small narrow-site experiments were probably too small because they left the
call-boundary state model intact. In this span, the bigger local cost appears
to be broad architectural state traffic around a direct child call:

- 188 approximate state bytes are stored before the call.
- 160 approximate bytes are not live into the child in the linear HIR audit.
- Most of that apparent dead traffic is VMX state from vector loads/splats.

That does not mean we can simply skip the stores. The compiled child currently
reads guest architectural state from the PPC context, and the parent resumes
after a host call boundary where normal allocated temporaries cannot be assumed
alive. A real speed experiment needs either:

1. a default-off store-suppression audit/counter proving skipped fields are not
   read dynamically by the callee or parent tail on the opening route, or
2. a larger direct-call state-handoff design that can pass selected live state
   without forcing every candidate through `PPCContext`.

## Decision

Do not make another narrow `EXTRACT_I32`, `stvewx`, or three-PC FMA shortcut
immediately. The next useful slice is a default-off, function/span/call-gated
state-store suppression audit for `82282490` span `8228252C-822825C4` at the
`82282598 -> 82287788` direct call.

The first audit should count candidate pre-call store sites only; it should not
change behavior. If counters match the static classification on the Thor route,
then a second default-off behavior probe can skip only the 13
`callee_dead_parent_dead_linear` sites and require route-clean proof before any
quiet A/B.
