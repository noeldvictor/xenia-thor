# Static Recompilation with Register Localization — Go/No-Go Eval (BD/Gears CPU)

Date: 2026-06-26
Trigger: user pointed to ExpansionPak/DolRecomp ("interesting ideas, must check").
Context: the per-block JIT register-residency lever crashes on device (back-edge
soundness, the cross-block design doc's hardest unit); this evaluates the
architecturally-clean alternative.

## The problem this solves
BD's common/light fields and Gears are CPU-bound on the guest JIT. The dominant
structural tax is that **guest PPC registers round-trip through PPCContext memory at
every basic-block boundary** — xenia's register allocator resets all host registers
per block (`register_allocation_pass.cc` PrepareBlockState → availability.set()).
The in-JIT fix (cross-block register-residency inheritance,
`arm64_register_cache_inherit`) is host-correct (320 assertions) but **crashes BD on
device** with a stale inherited register (SIGBUS 0x1fffffff8), localized this session
to a back-edge staleness hole in `guest_824694A0`. It is the design doc's
highest-risk unit and has eaten 12+ device fires across sessions.

## What DolRecomp is (and its key limitation)
A static recompiler for GameCube/Wii (PowerPC Gekko/Broadway) that **emits C**
(`src/backend/emitter.c`). Same family as XenonRecomp (Xbox 360, our exact arch) and
N64Recomp. Two relevant techniques:
- **PC-switch entry table:** one C function per guest function, `switch (ctx->pc) {
  case 0xADDR: goto label_ADDR; ... }` — every guest PC is a valid entry point, which
  cleanly handles indirect branches / dispatcher re-entry (the exact control-flow
  case the in-JIT residency lever mishandles).
- **BUT DolRecomp emits guest regs as context-struct fields** (`ctx->gpr[%u]`), so it
  does NOT get register residency — the host compiler round-trips them, same as our
  JIT. The residency win comes from **register LOCALIZATION** (regs → C locals), which
  XenonRecomp does and DolRecomp does not.

## Proven: localization IS the residency win (device-free kill-test)
`scratch/thor-debug/residency_killtest.c`, same guest hot loop (load-accumulate-
advance-count + an opaque `mem_read32(ctx,…)` call), aarch64-gcc -O2, disassembled:
- **struct fields (DolRecomp / ≈ our JIT):** loop body does **~4 ctx memory ops per
  iteration** (`ldp [x19,#32]` + `ldr [x19,#48]` reload gpr4/5/6, two `stp` store
  gpr3/4/5/6) — the opaque call defeats alias analysis.
- **localized (XenonRecomp):** loop body has **0 guest-reg memory ops** — regs live in
  x24/x21/x23 across the loop and the call; loaded once at entry, stored once at exit.

So localization removes the exact per-iteration round-trip tax the JIT pays and the
JIT-residency retrofit crashes trying to remove — soundly, for free, from the host
compiler. The mechanism is proven.

## Real hot-code characterization (guest_824694A0 — BD's hottest JIT'd fn)
Disassembled its 4 tight loops (from cached `scratch/blue-dragon/default.xex`) — all
the SAME pattern, a sorted linked-list / tree traversal (STL map/set-style):
```
loop: cmplw cr6, r10, r11 ; beq exit                 // end-of-list check
      lhz r8, -8(r11) ; cmplw cr6, r9, r8 ; ble exit // 16-bit key compare
      lwz r11, 0(r11)                                // r11 = node->next
      stw r11, 0x84(r31)                             // update iterator field (guest obj)
      b loop
```
Implications — this is the IDEAL case for static-recomp localization:
- **Pointer-chasing.** The node ptr `r11`, search key `r9`, bound `r10` round-trip
  through PPCContext **every block, every iteration** in the JIT (the loop is 3 blocks
  via its 2 conditional exits) = the tax. Localized → they stay in host registers
  across all 3 blocks; only the real guest mem ops (lhz/lwz/stw) remain.
- **Flag cost ≈ 0.** Every `cmplw` is immediately consumed by its branch → direct C
  `if` / lazy flags, no CR materialization. The expensive static-recomp case (flags
  read far from their compare) does NOT occur here.
- **Confirms the JIT crash:** the in-JIT residency inheritance binds the list
  node-pointer `r11` stale across this multi-block loop → `r11=0xfffffff8` →
  `lhz/lwz r11` → SIGBUS (the observed `fault addr 0x1fffffff8`).

Net: BD's real hot code is exactly the shape localization helps most (pointer-chasing
loops + trivial flags), and exactly the shape the in-JIT lever crashes on. Strengthens GO.

## The hard parts (the real cost / risk of the full system)
1. **Precise flags** — CR0-7, XER (CA/OV/SO), FPSCR must be bit-exact. The residency
   win COMPOUNDS with **lazy flags** (compute CR/XER on demand from the operands) —
   likely the single biggest sub-lever, and easier to express in C than in the JIT.
2. **Indirect control flow** — bctr/bcctr/blr + jump tables. Adopt DolRecomp's
   PC-switch for dynamic entry. Needs the scanner to enumerate targets (xenia's
   `ppc_scanner.cc` has the jump-table TODO) or a conservative "any PC entrable" table.
3. **Memory model** — guest loads/stores through membase; keep the mem helper opaque
   so locals stay resident (the kill-test relies on this).
4. **SMC** — DolRecomp punts (manual patch). Detect + fall back to the JIT for SMC
   pages; Xbox 360 titles rarely SMC gameplay code.
5. **Integration** — AOT functions must interop with xenia's lazy JIT, kernel HLE
   thunks, and the dispatcher. The realistic shape is **HYBRID: AOT-recompile the hot
   cluster, JIT everything else**, sharing the dispatcher/lookup table.

## Go/No-Go kill-test (device-free; do this BEFORE the multi-week build)
1. Recompile `guest_824694A0` (BD's confirmed hottest JIT'd fn this session) to C with
   register localization (regs → locals, lazy flags), aarch64 NDK -O2.
2. Compare emitted host-instruction + memory-op counts vs xenia's JIT output for the
   same function (dump via `disassemble_function_filter` on a fresh device, once).
3. **PASS bar (ThorPack gate):** localized AOT is >20-25% fewer host instructions /
   dramatically fewer memory ops than the JIT. <15% ⇒ DEAD (not worth the architecture).
4. **Correctness:** differential — run AOT vs JIT on identical inputs, require
   byte-identical guest state (reuse the qemu-a64 harness + the crossblock test
   template).

## Recommendation
- The residency mechanism is proven; the open question is full-system cost, gated by
  the kill-test above.
- If it passes: build a **targeted hybrid AOT** (hot cluster only), NOT whole-program.
  This sidesteps the crashing JIT-residency lever entirely and is the architecturally-
  clean BD/Gears CPU win.
- This is a multi-week bet and a major architecture decision — it needs an explicit go
  from the user, and a fresh device for the one codegen-comparison fire (step 2).

## Status of the alternative (the in-JIT lever)
`arm64_register_cache_inherit` stays default-off (crashes BD). If static recomp is a
no-go, the fallback is the back-edge soundness fix — but it needs the `guest_824694A0`
codegen to pin (device/qemu, currently blocked) and is the design doc's hardest unit.
See [[cross-block-register-caching-build]].
