# Cross-Block Guest-Register Caching — Crash Root-Cause + Incremental Build Plan

Date: 2026-06-24
Scope: xenia-thor ARM64 JIT — keep guest PPC GPRs live in host ARM64 registers across HIR
basic-block boundaries instead of round-tripping through PPCContext memory.

This is DESIGN + a concrete incremental build plan. All file:line citations are against the
working tree at `src/xenia/cpu/`.

---

## TL;DR

1. **The crash is a CFG-soundness bug, not a codegen bug.** The carrier seeds a successor from its
   "single dominating predecessor", but `Edge::DOMINATES` in this codebase does **NOT** mean
   "dominates" — `ControlFlowAnalysisPass` sets it iff the destination has **exactly one incoming
   edge** at the moment the edge list is built (`control_flow_analysis_pass.cc:67-70`). With
   `_cond_branch_carry` ON, a block reachable by a conditional branch whose *other* path was an
   indirect branch (`bctr`/`blr` → `OPCODE_CALL_INDIRECT`, which **adds no HIR edge** —
   `control_flow_analysis_pass.cc:43-62`) has only ONE recorded incoming edge, is flagged
   `DOMINATES`, and is therefore seeded from a predecessor whose carrier deposit does **not run on
   the indirect entry path**. The promoted `LOAD_LOCAL` then reads an uninitialized/stale stack slot
   → SIGBUS. A second, independent instance of the same class: a **single-predecessor loop body**
   whose dominating predecessor (the latch) is emitted **later** in the block list, so
   `outgoing_states[pred]` is empty when the body is seeded — the carrier reads `nullptr` and the
   load stays a context load (benign), but a back-edge that *re-enters* through a one-incoming-edge
   block reads the previous iteration's stack slot before it is deposited.

2. **The proven-safe predicate (`isEnterable`) is cheap here** because xenia never builds in-function
   edges for indirect branches: **a block is externally-enterable iff it is the function entry block
   OR it has an incoming edge that is NOT `Edge::DOMINATES` (i.e. it is a CFG merge / has >1
   predecessor) OR it is a label block whose guest address can be an indirect-branch target.** The
   first two are free from the existing edge data. The third needs one bit from the scanner.

3. **The mechanism question has a surprising answer: the existing carrier wins ~0 because the
   per-block register allocator turns every cross-block `LOAD_LOCAL` back into a stack `ldr`** —
   identical cost to the `LOAD_CONTEXT` `ldr` it replaced (`a64_sequences.cc` LOAD_LOCAL vs
   LOAD_CONTEXT both emit one `ldr`; `register_allocation_pass.cc:388` resets all availability per
   block). To get a *real* win you must make the value stay in a **host register** across the edge,
   which requires teaching the **register allocator** to inherit the predecessor's exit assignment
   (Approach A), not just rewriting context loads to local loads in the HIR. The HIR-local carrier is
   necessary plumbing but insufficient alone.

4. **Plan:** ship the safe `isEnterable` analysis first as pure analysis (U0), then *fix the existing
   crossblock carrier's soundness* by replacing the `DOMINATES`-as-dominator assumption with the real
   predicate (U1–U2) — this alone makes `_cond_branch_carry` safe and is the cheapest correctness
   win — then build the register-inheritance allocator extension behind its own cvar (U3–U6) to
   convert the now-correct cross-block liveness into actual host-register residency (the speed win).

---

## 1. CRASH ROOT-CAUSE (pinned)

### 1.1 What the carrier does

`ContextPromotionPass::PromoteCrossBlockGprSlots` (`context_promotion_pass.cc:1532-1714`):

- Allocates one INT64 HIR local per promoted GPR slot (`:1550-1553`).
- For each block in **emission order** (`builder->first_block(); block = block->next`, `:1572`):
  - If the block has a **single dominating predecessor** (`GetSingleDominatingPredecessor`,
    `:652-658`), seed `current[]` from `outgoing_states[pred]` (`:1580-1592`).
  - Walk instrs: a `LOAD_CONTEXT` of a tracked slot whose carried value's SSA def is in another
    block is replaced by `LOAD_LOCAL` + `ASSIGN` (`:1622-1652`); a full-width `STORE_CONTEXT`
    updates the carrier (`:1654-1667`); any volatile (unless a carryable cond-branch) resets
    (`:1608-1620`).
  - Before the tail branch, deposit dirty carriers via `StoreLocal` (`:1683-1692`).
  - Record `outgoing_states[block] = current` (`:1694-1698`).

`GetSingleDominatingPredecessor` (`:652-658`):
```cpp
Block* GetSingleDominatingPredecessor(Block* block) {
  auto edge = block->incoming_edge_head;
  if (!edge || edge->incoming_next || !(edge->flags & Edge::DOMINATES)) {
    return nullptr;
  }
  return edge->src;
}
```
It returns `pred` iff `block` has exactly one incoming edge AND that edge is flagged `DOMINATES`.

### 1.2 What `Edge::DOMINATES` actually means here (the bug's foundation)

`ControlFlowAnalysisPass::Run` (`control_flow_analysis_pass.cc:32-75`) is the ONLY producer of edges
and of the `DOMINATES` flag. It:

1. Adds edges **only for direct branches** (`:43-62`):
   - `OPCODE_BRANCH` → `AddEdge(block, label->block, UNCONDITIONAL)`
   - `OPCODE_BRANCH_TRUE/FALSE` → `AddEdge(block, label->block, 0)`
   - **`OPCODE_CALL`, `OPCODE_CALL_INDIRECT`, `OPCODE_RETURN` get NO edge** (they are
     `OPCODE_FLAG_BRANCH` but have no label operand; the loop ignores them).

2. Marks `DOMINATES` (`:64-72`):
```cpp
while (block) {
  if (block->incoming_edge_head && !block->incoming_edge_head->incoming_next) {
    block->incoming_edge_head->flags |= Edge::DOMINATES;
  }
  block = block->next;
}
```
This sets `DOMINATES` **iff the block has exactly one incoming edge** — i.e. it is
**single-predecessor**, which is a strictly weaker property than single-*dominator*. `AddEdge` clears
`DOMINATES` from all incoming edges the moment a *second* edge is added to a dest
(`hir_builder.cc:589-596`), so the flag is correctly "this is my only recorded predecessor". But
"only recorded predecessor" ≠ "dominator", because **edges are only recorded for direct branches**.

### 1.3 Failure mode (a): the indirect-branch hole — the SIGBUS

PPC indirect control flow (`bctr`, `bctrl`, `blr`, `bclr`, jump tables) lowers to
`OPCODE_CALL_INDIRECT` with a runtime target Value, **never** to an in-function edge
(`ppc_emit_control.cc` `InstrEmit_branch` indirect path; the scanner marks `bctr` block-ending but
does not decode targets — `ppc_scanner.cc` "TODO(benvanik): decode jump tables"). The dispatcher
resolves the target and **re-enters the function at that block via the function entry / lookup path**.

Consider a real pattern (common in compiled `switch` + guard code):

```
B0 (entry):     ... ; bctr  via CTR  →  OPCODE_CALL_INDIRECT (NO edge added)
B1 (label L1):  cond ; bc L3          →  BRANCH_TRUE L3 ; (fallthrough handled as branch by builder)
B2 (label L2):  ...
B3 (label L3):  r31 = load_context ...  ← promotion target
```

If at runtime the `bctr` in `B0` lands on `L3` (B3) — i.e. B3 is an indirect-branch target — then B3
is entered **without** going through B1. But the **only HIR edge into B3** is `B1 → B3` (the direct
`bc`). So B3 has exactly one incoming edge → `DOMINATES` is set → `GetSingleDominatingPredecessor(B3)
== B1`. The carrier seeds B3's `current[r31]` from `outgoing_states[B1]` and **replaces B3's
`load_context r31` with `load_local`**. But the local carrier was only deposited on the `B1 → B3`
path (the `StoreLocal` before B1's tail branch). On the **indirect `bctr → L3` entry path, B1 never
executed**, the carrier slot is whatever the host stack frame held (garbage / a previous unrelated
spill), and the promoted `load_local` reads it. r31 is then used as a base pointer → wild load →
**SIGBUS ~2s into BD boot**. This is exactly "a promoted register read a stale/zero carrier on a
real-code/call pattern the synthetic tests don't reproduce" — the synthetic tests
(`crossblock_gpr_promotion_test.cc`) only build **direct** `BranchTrue/Branch` CFGs, so every block's
recorded predecessor IS its true dominator there, and the hole never appears.

Why `_cond_branch_carry` specifically triggers it: with cond-branch carry OFF, the carrier resets at
every `BRANCH_TRUE/FALSE` (they are `OPCODE_FLAG_VOLATILE`), so `current[]` is empty entering the
deposit and **nothing is carried across the conditional edge into B3** — the load stays a real
`load_context`. With it ON, the conditional edge becomes carryable and the unsound seed fires.

### 1.4 Failure mode (b): back-edge / emission-order staleness

`outgoing_states` is filled in **block emission order** (`:1572`, `:1698`). A loop body `Bbody` that
is single-predecessor-dominated by its header is seeded from `outgoing_states[header]`. That is fine
forward. But a **latch** block that is the single recorded predecessor of a block emitted *earlier*
than the latch (a back-edge target that happens to have one incoming edge) is read before it is
written: `outgoing_states.find(pred) == end()` → seeds nothing (benign, load stays context). The
*dangerous* variant is when a one-incoming-edge block sits on a back-edge and its predecessor's
deposit semantically corresponds to the *previous iteration*; the host stack slot is then read across
the loop back-edge with a value that is correct only if every path that re-enters deposited it — which
the indirect-entry hole (a) breaks. `GenLoopCarry` in the test exercises a back-edge but with
all-direct edges, so (b) is masked there too.

### 1.5 Why host + qemu tests pass but the device crashes

- The differential tests build **only direct-branch CFGs** (`b.BranchTrue` / `b.Branch` /
  `b.MarkLabel`), so `DOMINATES` == true-single-dominator for every block, and there is no
  indirect-entry path. Both failure modes are structurally absent.
- qemu-a64 runs the same HIR fixtures → same absence.
- The device runs **real guest functions with `bctr`/`blr`/jump tables**, which is the only place an
  in-function block has an indirect (edge-less) entry that aliases a one-incoming-edge direct entry.

**Conclusion: the carrier's safety rests on `DOMINATES` meaning "dominates", but it means
"single recorded predecessor", and recorded predecessors omit all indirect entries. The fix is to
either (i) restrict promotion to blocks proven not externally-enterable, or (ii) compute real
dominance. (i) is the Cemu-proven, cheaper path.**

---

## 2. THE `isEnterable` ANALYSIS FOR XENIA'S HIR

### 2.1 What can enter a block other than a direct in-function edge

From the CFG construction facts:
- Direct `b`/`bc` → recorded edge (`UNCONDITIONAL` or conditional).
- `bl`/`bcl` to in-function target → also a `BRANCH`/`BRANCH_TRUE` edge (link handled separately).
- **Indirect `bctr`/`bctrl`/`blr`/`bclr` and jump tables → `OPCODE_CALL_INDIRECT`, NO edge.** Target
  blocks are re-entered through the dispatcher / function lookup.
- Function entry block (`first_block`) is entered from the caller / dispatcher.
- xenia has **no explicit exception/interrupt landing-pad blocks** in HIR (guest exceptions go through
  kernel HLE), so those are not a separate case here.

Therefore a block is **externally-enterable** (NOT safe to seed a hot carrier into) iff ANY of:

**(E1) It is the function entry block.** `block == builder->first_block()`. (Cold start — registers
must be loaded from context.)

**(E2) It is a CFG merge.** It has `>1` incoming edge, equivalently its incoming head is not
`DOMINATES`: `block->incoming_edge_head && block->incoming_edge_head->incoming_next` OR
`!(block->incoming_edge_head->flags & Edge::DOMINATES)`. (Multiple direct preds → no single carrier
state is valid.) The existing carrier already handles this by only seeding single-`DOMINATES`-pred
blocks; keep that.

**(E3) Its guest address is (or may be) an indirect-branch target.** This is the missing case that
caused the crash. Because xenia does not decode jump tables, the **safe conservative predicate is:
any label block that is reachable by something other than its recorded direct predecessor**. We
cannot enumerate indirect targets, so we must treat **every block whose guest start address is a
`Label` that could be branched-to indirectly as enterable**. The cheapest sound over-approximation
that still allows the win:

> A block is indirect-enterable UNLESS its single recorded `DOMINATES` predecessor is also its only
> *possible* runtime predecessor. We can prove the latter only when the predecessor ends in a
> **direct** branch to this block AND this block's entry is NOT independently reachable.

Since we cannot prove non-reachability without a jump-table decode, U0 uses the **strongest cheap
sound rule**: a block is safe-internal iff (not E1) AND (not E2) AND **its dominating predecessor is
the block immediately preceding it in emission order and the edge is a fallthrough/`UNCONDITIONAL`
direct branch** — i.e. straight-line and conditional fallthrough chains, which a `bctr` cannot land
in the middle of *unless the guest address is a label*. The residual risk (a `bctr` targeting a
label that is mid-chain) is removed by E4.

**(E4) Refinement (removes residual indirect risk, needs 1 scanner bit):** mark a block enterable if
its head guest address appears in the scanner's set of **address-taken / indirect-target-candidate**
addresses. The scanner already walks every instruction; add a pass that flags any block whose address
is loaded as a constant into a register that feeds CTR/LR, or — simplest and fully sound — flag every
block that is a `Label` created for a `bctr`/`bcctr` fallout. Until E4 lands, U0's E1∧E2∧E3 rule is
sound because it only promotes straight-line dominated chains, and the carrier additionally resets at
every `OPCODE_CALL_INDIRECT` (it is volatile) so a carrier cannot survive *past* an indirect branch
inside a block — the only hole is an indirect *entry into* a promoted block, which E3 closes by
refusing to promote any block that is not a pure fallthrough successor.

### 2.2 The concrete predicate (U0 deliverable)

```cpp
// True if `block` may be entered at runtime by any path other than the single
// recorded DOMINATES edge from its predecessor. Conservative: when unsure, true.
bool IsExternallyEnterable(HIRBuilder* builder, Block* block) {
  // E1: function entry.
  if (block == builder->first_block()) return true;
  Edge* in = block->incoming_edge_head;
  // No recorded predecessor at all => only reachable indirectly (or dead). Enterable.
  if (!in) return true;
  // E2: merge point (more than one recorded predecessor, or non-dominating edge).
  if (in->incoming_next) return true;
  if (!(in->flags & Edge::DOMINATES)) return true;
  // E3: only treat as internal if the dominating predecessor reaches us by a
  // DIRECT branch/fallthrough. (Indirect targets have NO recorded edge, so a
  // block reached only indirectly already returned true at !in above; this guards
  // the case where a direct edge exists AND an indirect entry also exists.)
  // Until the scanner address-taken bit (E4) lands, require the predecessor to be
  // the immediately-preceding emission block (straight-line/conditional fallthrough),
  // which a jump table cannot split without creating a Label (handled by E4 later).
  if (in->src != block->prev) return true;   // not a fallthrough chain → be safe
  return false;
}
```

This is **pure analysis**: it reads only existing edge data + `block->prev`. It changes no codegen.
U0 ships it + a host test asserting it matches hand-computed expectations on the CFG fixtures, and an
audit counter (how many blocks are internal vs enterable per function). Behavior byte-identical.

`block->prev` is the emission-order predecessor (`Block::prev`, `block.h:51`). For a fallthrough/`b`
chain the dominating direct predecessor is exactly `block->prev`; for a back-edge or a forward jump
over intervening blocks it is not, so those are conservatively enterable. This is intentionally
strict for U0; E4 relaxes it later with a soundness proof from the scanner.

---

## 3. THE MECHANISM: A (allocator inheritance) vs B (whole-function linear scan)

### 3.1 The decisive measurement

A cross-block HIR local does **not** keep its value in a host register. The register allocator resets
`availability.set()` (all free) and clears `upcoming_uses` at **every** block
(`register_allocation_pass.cc:388-397`, called from `:217`). A `STORE_LOCAL` in block A and a
`LOAD_LOCAL` of the same slot in block B are independent: B emits a real `ldr` from the frame slot
(`a64_sequences.cc` LOAD_LOCAL_I64 → `e.ldr(dest, ptr(base, off))`; slot offset baked once at
`a64_emitter.cc` ~`:3761`). `LOAD_CONTEXT` is also one `ldr` from `[x20, off]`. **So replacing
`LOAD_CONTEXT` with `LOAD_LOCAL` trades one `ldr` for one `ldr` = ~0 win**, exactly as observed. The
existing HIR-local carrier cannot produce a speedup by construction; it only sets up *correct
cross-block dataflow*. The win must come from the allocator keeping the value in a register.

### 3.2 Approach A — augment the per-block allocator to inherit predecessor exit state

Idea: when a block has a single `DOMINATES`, non-enterable predecessor whose exit register-assignment
is known, **seed the successor's initial register state** from it: the carried guest value is already
in host register `Xk` at the end of the predecessor; record `value → Xk` at pred exit, and at the
successor entry pre-bind the carrier's `LOAD_LOCAL`/value to `Xk` (and mark `Xk` reserved) so the
allocator reuses it without a reload. The `LOAD_LOCAL` then becomes a no-op `ASSIGN` that the
allocator coalesces (dest == the inherited reg), and the deposit `STORE_LOCAL` in the predecessor
becomes dead when the only consumers are register-inheriting successors.

Data-structure changes (all in `register_allocation_pass.{h,cc}`):
- Add `struct BlockExitAssignment { std::unordered_map<hir::Value* /*carrier local*/, hir::RegAssignment> regs; };`
  and `std::unordered_map<Block*, BlockExitAssignment> block_exit_;` (cleared per function).
- After finishing a block, snapshot the host registers currently holding tracked carrier locals into
  `block_exit_[block]`.
- In `PrepareBlockState()`, instead of unconditionally `availability.set()`, if the block is
  non-enterable (predicate from §2) and has a recorded `block_exit_[pred]`, pre-mark those host
  registers as **occupied by the inherited values** and seed `upcoming_uses` so they are not
  immediately evicted; bind the carrier value's `reg` to the inherited register.
- The carrier (ContextPromotionPass side) still inserts the local + `LOAD_LOCAL` so SSA is valid on
  paths where inheritance does not apply (enterable re-entries reload from the local, which is
  correct because the deposit ran on the direct path — but for enterable blocks we DO NOT promote at
  all, so the local is only read where deposited).

Pros: incremental, reuses the existing per-block scaffold and the existing HIR-local carrier as the
fallback/correctness layer; touches one pass; default-off cvar; differentially testable. Risk is
localized to the seed step and is **fail-safe**: if inheritance is wrong, drop to `availability.set()`
(reload) — never miscompile, just slower.

Cons: it is a peephole on a fundamentally block-local allocator; spill decisions are still per-block,
so a register inherited into a high-pressure successor may be spilled immediately (the local carrier
then covers it — correct, no win that time). It cannot do global live-range splitting.

### 3.3 Approach B — whole-function linear-scan (Cemu IMLRegisterAllocator-style)

Replace the per-block allocator with a function-global linear scan over a flattened instruction order
with real live intervals, `isEnterable` blocks starting cold, internal edges keeping registers hot.
This is the "right" solution and the source of Cemu's "50–100% faster CPU emulation".

Pros: the actual large win; subsumes A; removes per-block reload entirely for hot loop variables.

Cons: a from-scratch allocator is the single highest-risk change in the JIT; it must reproduce every
guard the current allocator and the a64 backend rely on (preferred-reg reuse `:289-307`,
`SpillOneRegister` semantics `:541`, the x20/x21 pin, type-set partitioning, `dest==src1` reuse). A
bug is a silent miscompile across the whole library, not a localized crash. Validating it needs the
full differential suite + qemu + device A/B on many titles. It is a multi-week unit and should NOT be
the first thing built.

### 3.4 Verdict

**Build A first** (lower-risk incremental path; fail-safe; reuses existing carrier as the correctness
floor). Keep **B as the eventual tier** once A proves the win is real and the `isEnterable` predicate
is device-validated. A and B share the §2 predicate and the differential test harness, so A is not
throwaway — it de-risks the predicate and the test methodology that B also needs.

---

## 4. THE INCREMENTAL PLAN (U0..U6, each default-off, host/qemu-validatable)

Each unit: **what changes**, **how to validate**, **gate cvar**. Differential = compile+run the same
HIR fixture OFF vs ON and require byte-identical guest state, reusing the
`crossblock_gpr_promotion_test.cc` / `cross_block_flag_dse_test.cc` `RequireTransparent` template.

### U0 — `isEnterable` as pure analysis (FOUNDATION, behavior-unchanged)

- **Change:** Add `IsExternallyEnterable(builder, block)` (§2.2) as a free function in
  `context_promotion_pass.cc` (or a small shared `cfg_enterability.h`). Add an audit cvar
  `arm64_cfg_enterability_audit` that logs, per function, `blocks`, `enterable_blocks`,
  `internal_blocks`, and the breakdown by reason (E1/E2/E3). **No codegen change.**
- **Validate:** new host test `cfg_enterability_test.cc`: build the existing fixtures
  (`GenCarryUnmodified`, `GenNestedDominatedChain`, `GenLoopCarry`) PLUS one fixture with an indirect
  branch (`b.CallIndirect(...)` style — emit `OPCODE_CALL_INDIRECT` then a labeled block) and assert
  the predicate flags the right blocks. Assert the entry block and every merge/back-edge target is
  enterable; assert straight-line dominated successors are internal. Pure function → no Run needed,
  but also add a `RequireTransparent` no-op check (cvar default-off changes nothing) for hygiene.
- **Cvar:** `arm64_cfg_enterability_audit` (default-off, audit only).

### U1 — Gate the EXISTING crossblock carrier on `isEnterable` (correctness fix)

- **Change:** In `PromoteCrossBlockGprSlots`, replace the seed condition: only seed `current[]` from
  `outgoing_states[pred]` when `GetSingleDominatingPredecessor(block)` returns non-null **AND
  `!IsExternallyEnterable(builder, block)`**. Additionally, refuse to *promote a load* (the
  `loads_replaced` path `:1631-1643`) in any block that is enterable — such a block must keep its
  real `LOAD_CONTEXT`. This makes the carrier sound even with `_cond_branch_carry` ON, because a
  block reachable by an indirect entry is now `IsExternallyEnterable` (E3: its dominating pred is not
  `block->prev`, or it is the entry) and is never promoted.
- **Validate:** `RequireTransparent` on all existing crossblock fixtures (must stay green) + a NEW
  fixture that interleaves an `OPCODE_CALL_INDIRECT` whose absence-of-edge makes a downstream labeled
  block falsely single-predecessor; assert OFF==ON byte-identical AND (audit) that `loads_replaced`
  for that block is 0. This is the regression that reproduces the device crash *in the host harness*
  for the first time.
- **Cvar:** reuse `arm64_context_promotion_gpr_crossblock` (+ keep `_cond_branch_carry`), now sound.
  Still default-off pending device A/B, but the known crash is fixed.

### U2 — Device-validate U1 on Blue Dragon (the crash repro)

- **Change:** none (validation unit). Enable `arm64_context_promotion_gpr_crossblock=true` +
  `_cond_branch_carry=true` on-device via the gated capture harness; confirm BD boots past the ~2s
  SIGBUS point and renders the heavy field pixel-identically to OFF.
- **Validate:** device boot + frame png compare (per the thermal-gated `thor_gpu_capture.ps1`). If
  green, this proves the §2 predicate closed the hole. (Still ~0 fps win — that is expected; the win
  arrives in U3+.)
- **Cvar:** as U1.

### U3 — Allocator exit-assignment snapshot (analysis, behavior-unchanged)

- **Change:** In `RegisterAllocationPass`, after each block completes, record into
  `block_exit_[block]` the host register currently assigned to each *carrier local value* (the
  values the U1 carrier tracks). **Do not consume it yet** — just record + audit
  (`arm64_register_inheritance_audit`: how many (value,reg) pairs survive to each block exit, how many
  successors are internal and could inherit). No codegen change.
- **Validate:** host audit run on fixtures + one real-ish multi-block function; assert the snapshot is
  populated and that internal successors have a recorded inheritable predecessor. `RequireTransparent`
  no-op (recording changes nothing).
- **Cvar:** `arm64_register_inheritance_audit` (default-off).

### U4 — Allocator seed from inherited registers (the speed mechanism), default-off

- **Change:** Behind `arm64_register_cache_inherit` (default-off): in `PrepareBlockState` / at block
  entry, for an internal (`!IsExternallyEnterable`) single-`DOMINATES`-pred block with a recorded
  `block_exit_[pred]`, pre-bind each inherited carrier value to its predecessor-exit register, mark
  that register occupied, and seed `upcoming_uses` so it survives to first use. The carrier's
  `LOAD_LOCAL` for that value becomes an `ASSIGN` from the inherited reg (coalesced away). **Fail-safe:
  if a register cannot be honored (pressure/conflict), fall back to the normal
  `availability.set()` + the local `LOAD_LOCAL` reload — never miscompile.**
- **Validate:** `RequireTransparent` OFF vs ON on ALL fixtures (byte-identical) + non-vacuity audit
  showing `inherited_regs > 0` and a drop in emitted `LOAD_LOCAL`/`LOAD_CONTEXT` count on the loop
  fixture. qemu-a64 differential on the same fixtures (real ARM64 codegen). This is the first unit
  that should actually reduce emitted loads.
- **Cvar:** `arm64_register_cache_inherit` (default-off).

### U5 — Dead-deposit elimination + device A/B

- **Change:** When every consumer of a carrier deposit `STORE_LOCAL` is an inheriting internal
  successor (no enterable reader), DCE the deposit (it is covered by register inheritance). Then
  device A/B on Burnout's hot entity-traversal loop (`0x82382798`, the r1/loop-counter carrier site
  from memory) and a CPU-bound BD gameplay scene at matched `guest_ms`: measure emitted-instruction
  delta + fps.
- **Validate:** host differential (must stay byte-identical — removing a now-redundant store) +
  device A/B fps at matched scene. This is where the win is proven or bounded.
- **Cvar:** `arm64_register_cache_inherit` (+ an audit sub-flag for deposit elimination).

### U6 — (optional, larger) widen the carried set / promote to whole-function linear scan (Approach B)

- Only if U4/U5 show the win is real but bounded by per-block spill churn. Build Approach B as a
  separate default-off allocator selectable by cvar, reusing the §2 predicate + the entire
  differential+qemu+device harness from U0–U5. Treated as its own multi-unit effort.

---

## 5. KEY FILE:LINE INDEX (for the implementer)

- Carrier (the thing to gate): `context_promotion_pass.cc:1532-1714`
  - single-dom-pred helper: `:652-658`
  - seed from pred: `:1580-1592`; load promote: `:1622-1652`; deposit: `:1683-1692`
  - cond-branch carry flag handling: `:1604-1620`
- Edge + DOMINATES construction (the bug's root): `control_flow_analysis_pass.cc:43-72`
  - DOMINATES = single incoming edge: `:67-70`
  - AddEdge clears DOMINATES on 2nd pred: `hir_builder.cc:589-596`
- Edge/Block model: `hir/block.h` (Edge flags `:30-33`; `incoming_edge_head` `:54`; `Block::prev` `:51`)
- Indirect branch → no edge: `control_flow_analysis_pass.cc:48-58`; `ppc_emit_control.cc` indirect
  path (`CallIndirect`, `CALL_POSSIBLE_RETURN`); scanner jump-table TODO in `ppc_scanner.cc`
- Per-block allocator reset (why local carrier ≈0 win): `register_allocation_pass.cc:217`, `:388-397`
- LOAD_LOCAL vs LOAD_CONTEXT both one `ldr`: `a64_sequences.cc` (LOAD_LOCAL_*, LOAD_CONTEXT_*); slot
  offset bake: `a64_emitter.cc` (~`:3753-3763`)
- Spill mechanism: `register_allocation_pass.cc:541` (`SpillOneRegister`)
- Pass pipeline order (production): `ppc_translator.cc:198-249`
  (ControlFlowAnalysis → ControlFlowSimplification → ContextPromotion → … → RegisterAllocation →
  Finalization). NOTE: edges are computed ONCE before ContextPromotion and are NOT recomputed before
  RegisterAllocation, so both the carrier and the allocator see the same (direct-only) edge set.
- Test templates: `crossblock_gpr_promotion_test.cc` (`RequireTransparent`, fixtures),
  `cross_block_flag_dse_test.cc` (`RemovedDelta` non-vacuity pattern)

---

## 6. SAFETY SUMMARY

- The crash class is **CFG-soundness** (treating single-predecessor as dominator while indirect
  entries are edgeless), fixed by the §2 `isEnterable` predicate (U1). This is the Cemu-proven rule
  specialized to xenia's "indirect = no edge" model: **only the entry block, merge points, and
  any block not reached purely by a direct fallthrough from its single recorded predecessor are
  cold**.
- Every unit is default-off, fail-safe (wrong inheritance → reload, never miscompile), and
  differentially host-validated OFF==ON byte-identical before any device fire.
- The win is **not** in the HIR-local carrier (one `ldr` → one `ldr`); it is in the allocator
  inheritance (U4). Do not expect fps from U1/U2 — they are the correctness foundation that makes the
  aggressive carry safe so U4 can convert it to host-register residency.
