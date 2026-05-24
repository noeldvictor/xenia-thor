# HIR Dead State Store DCE Audit

Date: 2026-05-24 09:42 EDT

## Question

After the backend-only `arm64_blue_dragon_call_boundary_state_suppress_dead_stores`
probe missed speed proof, should we move that same suppression earlier into HIR
so `DeadCodeEliminationPass` can remove upstream `8228252C-822825C4` vector/FPR
work?

## Commands

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_dead_state_store_dce_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 8228252C -EndGuest 822825C4 -TargetMode BlueDragonCallBoundaryDead -Top 32
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_dead_state_store_dce_audit.ps1 -LogPath scratch\thor-debug\20260521-170941-speed-logcat.txt -Function 82282490 -StartGuest 8228252C -EndGuest 822825C4 -TargetMode AllSpanStoreContext -Top 32
Select-String -Path scratch\thor-debug\20260524-041413-speed-logcat.txt -Pattern 'Filtered function dump 82287788 OptHIR:.*(load_context \+296|load_context \+2628|store_context \+296|store_context \+2628|f\[1\]|fpscr)' -Context 4,8
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_hotpath_report.ps1 -LogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -Function 82287788 -Phase OptHIR -Top 30
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_hir_state_span_report.ps1 -LogPath scratch\thor-debug\20260524-041413-speed-logcat.txt -Function 82287788 -Phase OptHIR -Top 20
```

## New Tool

Added `tools/thor/thor_hir_dead_state_store_dce_audit.ps1`.

The tool parses a filtered HIR dump, marks target `store_context` rows as if a
HIR pass removed them before `DeadCodeEliminationPass`, then recursively counts
assignment-producing HIR rows that would become unused. It is an offline
planning tool only; it does not change emulator behavior.

Supported modes:

- `BlueDragonCallBoundaryDead`: the exact `13` candidate-dead stores before
  `82282598 -> 82287788`.
- `AllSpanStoreContext`: unsafe upper bound that removes every `store_context`
  row in the requested span.

## Results

For the exact `13` route-proven candidate-dead stores:

```text
function_hir_instr=6798 span_store_context=27 target_store_context=13
dead_assignments=0 dead_in_span=0 dead_outside_span=0
decision=no_hir_dce_patch
```

The target fields were:

```text
f[0], f[13], r[5], r[7], r[8], r[9],
v[0], v[10], v[11], v[12], v[13], v[8], v[9]
```

This means moving the same `13`-store suppression up into HIR would still only
remove the `store_context` side effects. It would not delete the upstream
`lvx128`, `vspltw`, `vmaddfp`, `lfs`, or `fmuls` work because those values have
other local uses in the span.

For the unsafe all-span upper bound:

```text
target_store_context=27
dead_assignments=22 dead_in_span=22 dead_outside_span=0
dead_ops=convert:4, add:3, load_context:3, cast:2, compare_sgt:2,
         compare_slt:2, load.1:2, and:1, mul:1, or:1, zero_extend:1
```

Most of the larger removable-looking work is the live call argument path:

- `8228258C` / `82282590`: loads `f[13]` and `f[0]`.
- `82282594`: computes and stores `f[1]` plus updates `fpscr`.
- `82282598`: calls `82287788`.

That path is not safe to remove: the filtered `82287788` HIR proves the callee
loads `f[1]` and `fpscr`. Examples include:

- `82287798 stfs fr1, 0x16C(r1)` immediately loads `f[1]`.
- later `stfs fr1` rows reload `f[1]`.
- later floating-point compares and arithmetic load/update `fpscr`.

The focused `82287788` reports confirm this is real state traffic:

```text
82287788 load_context +296 f[1] = 10
82287788 load_context +2628 fpscr = 26
82287788 store_context +2628 fpscr = 26
```

## Decision

Do not implement a HIR-level version of the exact `13`-store dead-state
suppression. It has no recursive DCE payoff beyond the small backend store
skip already tested in
`docs/research/20260524-093001-blue-dragon-call-boundary-store-suppression-probe.md`.

Do not remove the `f[1]` / `fpscr` live-in path. It is a real callee argument
path into `82287788`, not dead state.

## Next Experiment

The next useful broader state/vector/FPR lane is an interprocedural argument /
state-roundtrip audit for `82282598 -> 82287788`, focused on live `f[1]` and
`fpscr`:

- quantify how often `82287788` reloads `f[1]` / `fpscr` from context after
  the parent just stored them;
- determine whether a default-off function-pair carrier, callee-local promotion,
  or direct-call argument lowering could avoid context round-trips without
  changing guest-visible PPC state;
- keep backend store suppression, all `stvewx` lane folds, `MUL_ADD_V128`
  shortcuts, broad CR elision, and broad GPR caches default-off until fresh
  route evidence justifies them.
