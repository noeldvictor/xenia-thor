# 20260524-115538 - 8228252C Lane Closure And Next Target

## Goal

Re-check the broader `82282490:8228252C-822825C4` local span after the
`fpscr` dirty-cache audit closed the latest tempting sub-lane, then decide
whether the next autonomous slice should patch this span or pivot to a fresher
body-backed local target.

## Starting Point

Starting HEAD: `d409a4c26`

Relevant latest evidence:

- `docs/research/20260524-114614-82287788-fpscr-dirty-cache-audit.md`
- `docs/research/20260524-110545-blue-dragon-f1-carrier-fastpath-ab.md`
- `docs/research/20260524-094213-hir-dead-state-store-dce-audit.md`
- `docs/research/20260524-082324-blue-dragon-mul-add-v128-fastpath-ab.md`
- `docs/research/20260524-071045-blue-dragon-all-three-stvewx-quiet-ab.md`
- `docs/research/20260524-063109-82282490-span-instruction-estimate.md`
- `docs/research/20260524-054204-82282490-exclusive-candidate-ranking.md`

## Commands

```powershell
git status --short --branch
Test-Path scratch\thor-debug\latest-blue-dragon-full-speed-proof.json
Get-Content scratch\thor-debug\codex-goal-loop.json -Raw
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_call_boundary_state_audit.ps1 -ParentLogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -ParentFunction 82282490 -ParentStartGuest 8228252C -ParentEndGuest 822825C4 -CalleeLogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -CalleeFunction 82287788 -Phase OptHIR -Top 40
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_dead_state_store_dce_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 8228252C -EndGuest 822825C4 -TargetMode BlueDragonCallBoundaryDead -Top 32
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_a64_codegen_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 8228252C -EndGuest 822825C4 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 24
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_dynamic_block_slice_report.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -BlockProfileLog scratch\thor-debug\20260524-030450-speed-logcat.txt -Top 20 -ContextTop 12
```

One command was tried with a stale `-EdgeProfileLog` parameter for
`thor_hir_dynamic_block_slice_report.ps1`; that script does not support the
parameter. The command was rerun with supported parameters, and the corrected
output below is the evidence used for this decision.

## Rechecked 8228252C Evidence

`tools/thor/thor_hir_call_boundary_state_audit.ps1` still reports meaningful
state traffic before the hot `82282598 -> 82287788` call:

- `parent_pre_call_store_context=17`, approximately `188` bytes of context
  state traffic.
- By class: `vmx=7`, `gpr=5`, `fpr=3`, `control=1`, `fpscr=1`.
- Candidate-dead before the call:
  `r[5]`, `r[7]`, `r[8]`, `r[9]`, `v[0]`, `v[8]`, `v[9]`, `v[10]`,
  `v[11]`, `v[12]`, `v[13]`, `f[13]`, and `f[0]`.
- Live-in / externally visible state remains `r[3]`, `f[1]`, `fpscr`, and
  `lr`.

The follow-up HIR DCE audit keeps the store-suppression lane closed:

- `span_store_context=27`
- `target_store_context=13`
- `dead_assignments=0`
- decision: `no_hir_dce_patch`

The source-reviewed A64 audit keeps the span ranked high but not solved:

- body span: `8228252C-822825C4`
- `body_ticks_total=8530471`
- child edge `8228252C -> 82287788`: `5653971`
- approximate exclusive local work: `2876500` (`33.72%`)
- source-reviewed estimated A64 floor: `192`
- approximate exclusive ticks per estimated instruction: `14981.77`
- dominant shape: `store_context=27`, `load_context=14`, `extract=9`,
  `splat=6`, `mul_add=3`, `stvewx=3`

## Closed Local Lanes

The current narrow patch candidates inside `8228252C-822825C4` are closed for
immediate sprint purposes:

- The 13 candidate-dead pre-call stores are store-only. A backend suppression
  probe was route-clean but not speed-proven, and the HIR DCE audit reports no
  recursive dead-assignment payoff.
- `f[1]` has a plausible function-pair story, but the stack-slot carrier A/B
  missed speed proof and the pair-entry ROI is only the seed context load.
- `fpscr` is too cross-boundary for a Blue Dragon-only shortcut. The dirty
  cache audit requires CFG/PHI or carrier state plus exact external writebacks.
- `r[3]` is mutable and high-risk, and `lr` is call-link state.
- The all-three `stvewx` lane fold was route-clean and reduced code size
  slightly, but the quiet A/B did not prove speed.
- The three-PC `MUL_ADD_V128` fastpath was route-clean and reduced code size
  materially, but the quiet A/B was mixed and not a route-wide speed proof.

This does not mean `8228252C-822825C4` is unimportant. It means the next useful
patch there is likely a broader CFG-aware/interprocedural state carrier or
callee-variant design, not another narrow PC-fold.

## Fresh Candidate

The corrected dynamic block slice report keeps `8228252C-822825C4` first by
absolute approximate exclusive ticks, but it also exposes a fresher local target
that is not call-heavy:

- `822824B8-822824E8`
- `approx_exclusive=1099164`
- `exclusive_pct=100`
- shape: `beq`, `addis`, `cmpl`, `lwz`, `rlwinm`, `rlwinm.`, and branch
  control
- HIR surface: `store_context=16`, `truncate=5`, `zero_extend=5`,
  `context_barrier=4`, `load_context=4`, `and=3`, `branch_true=3`,
  `compare_eq=3`, `or=3`, `shl=3`, `compare_ugt=2`, and `compare_ult=2`
- load traffic: `r[11]:2`, `r[31]:2`
- store traffic: `cr6.1:2`, `cr6.all_equal:2`, `cr6.none_equal:2`,
  `r[10]:2`, `r[11]:2`, `r[9]:2`, `cr0.eq:1`, `cr0.gt:1`, `cr0.lt:1`,
  `r[30]:1`

## Decision

Do not patch `8228252C-822825C4` behavior next unless the slice is explicitly
building a broader CFG-aware or interprocedural state-carrier design. The
isolated state/vector/FPR sub-lanes have now had enough evidence to avoid more
same-shape A/B loops.

The next low-risk local lane should be an offline audit for
`822824B8-822824E8`, focusing on the CR/GPR compare-and-branch context traffic
and whether any stores can be suppressed, delayed, or kept local under exact
branch, helper, exit, exception, and alias rules. Only after that audit should
we consider a default-off function/span-gated codegen experiment.
