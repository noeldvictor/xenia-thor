# PowerPC → AYN Thor hardware-accel codegen rearchitecture (CPU track)

Date: 2026-06-17. Source: ultracode workflow (ppc-thor-hw-accel-rearch, 6 agents) +
adversarial precision/feasibility red-team, both grounded against the live a64 backend
this session. User direction: "rearch core xenia PowerPC translation to exploit Thor
hardware (NEON + ARMv9 accelerators) — hardware acceleration as the KEY for PowerPC
adaptation. Stop cutting content."

## Scope reality (honest framing)
This is the **CPU track**: it speeds up **CPU-bound** guest titles (Burnout
post-fence-fix is CPU-bound on the guest Main XThread; Lost Odyssey once it renders). It
does **NOT** speed GPU-bound BD (whose cost is GPU overdraw — addressed separately by the
foliage/transparency thinning toggles). The CPU is ~75-85% idle on GPU-bound titles, so
measure CPU-track wins only on CPU-bound, currently-rendering titles → **Burnout is the
only valid A/B vehicle right now** (LO spins at 0x827B6278, task #10).

## Hard precision constraint (load-bearing — violating it black-screened BD)
Guest-VISIBLE FP/vector math must reproduce PowerPC precision EXACTLY. The lossy int8/bf16/
FP16 matrix+dot units (asimddp/i8mm/bf16/fphp) must NEVER touch guest FP32 geometry
(arm64_vmx_dot_f32_fastpath black-screened BD). Safe hardware levers: exact NEON FP32
(fmla single-rounding), ARMv9 SCALAR features (flagm for carry/CR, LSE for locks), and
recompiler-structural wins.

## Ground-truth corrections the red-team verified in-tree (recon was partly stale)
- Denormal-flush-skip-on-FZ: ALREADY SHIPPED (8 guarded sites). Not a unit.
- Single-use integer compare+branch fusion: ALREADY SHIPPED (`arm64_single_compare_branch_fusion`, default-off, has a test).
- FLAGM ADD_CARRY: real, bit-exact, default-OFF (`a64_sequences.cc:2122-2209`, gated on `kA64EmitFlagM`).
- LSE kernel locks: shipped + default-on.
- **The `fmla` MUL_ADD_V128 fast path is fenced to ONE BD function** (`current_guest_function()==0x82282490` + 3 PCs), and it is a **BARE `fmla` with NO NaN fixup / NO output flush** (a64_sequences.cc:5441-5453). That is WHY it's title-scoped — it is only safe for 3 BD call sites whose operands never produce NaN/output-denormals. **Generalizing the bare path as-is would diverge on NaN/Inf and black-screen, exactly like arm64_vmx_dot_f32_fastpath.**

## Ranked plan

### ⭐ UNIT 1 (FIRST) — Generalize MUL_ADD_V128 `fmla`, precision-CORRECTED
PPC `vmaddfp`/`vnmsubfp` (FP32×4 fused MAdd) → Thor `fmla.s4` (single instr, single
rounding = exact PPC semantics). **CORRECTION (red-team):** the generalized path MUST keep
`FixupVmxNan_V128_Fma` + output-denormal flush — it is the **slow path minus the
FZ-covered software INPUT flush**, NOT the bare-`fmla` BD path. On Thor (FZ=true) the slow
path already skips the input flush; the remaining waste is the **unconditional 3 source
stack-stores + 1 load that exist only to feed the rare per-lane NaN path** (the NaN
front-gate at seq_util.h:574-578 `fcmeq/uminv/cbnz` skips everything when no result lane is
NaN). **Real win = make the source spills LAZY (only on NaN-detected)**, keeping sources
register-resident in the common case. Wrinkle: the NaN front-gate needs a scratch vreg
(the helper uses v3, which the lazy version wants for a source) → restructure the helper to
read sources from registers (umov) or free a 5th scratch vreg.
- Files: `a64_sequences.cc:5370-5604` (MUL_ADD/MUL_SUB V128), `a64_seq_util.h:560-637`
  (FixupVmxNan_V128_Fma), `a64_backend.cc:303-391` (new cvar `arm64_fma_v128_fastpath`).
- **HARD GATE: qemu-a64 differential test** (`fma_v128_test.cc`): fast-path bits == slow-path
  bits == PPC `vmaddfp` reference across denormal/NaN/Inf/signed-zero/wide-random. No
  default-on until clean. Then A/B on Burnout. XeniaOptimizations toggle "Fused vector
  multiply-add (FMA)".
- Honest magnitude: modest (saves the source spills + generalizes); Burnout's ONE profiled
  hot fn (0x82382798) is INTEGER (won't benefit), but Burnout's other vector/transform math
  will. Unproven until the Burnout A/B.

### UNIT 2 — Flip `arm64_flagm_fastpath` default-ON (no code, A/B-pending)
Bit-exact, built, host-tested; just never device-promoted. Bundle the A/B into Unit 1's
Burnout fire. Win = 1 instr/carry, only in carry-dense loops.

### UNIT 3 — SUB-with-carry (subfc/subfe) FLAGM lowering
Mirror of ADD_CARRY using ARM `SBCS`/`SBC` + `rmif`. **Trap: ARM borrow polarity is INVERTED
vs PPC (C=1 ⇒ no-borrow)** — the whole risk. Verify `sbcs`/`sbc` exposed in the emitter;
gate behind an exhaustive host `xenia-cpu-tests` borrow matrix. Burnout's hot fn HAS subfc/
subfe carry chains (recon agent 1 + disasm), so this is the unit most likely to touch
Burnout's measured hot path.

### UNIT 4 — Intra-block dead-CR-field elimination (INSTRUMENTATION FIRST)
Record-form ops (`cmpw`, `addi.`) write a CR field overwritten before any read within the
same block → skip its StoreContext (~2-4 instr each). **Strictly intra-block** (sidesteps
the cross-barrier-elision wall that crashed BD; do NOT revive
`arm64_cr_compare_branch_across_context_barrier`). **Build the `arm64_dead_cr_profile`
instrumentation sub-pass FIRST** (backend-independent → host-testable), run on Burnout, get
the real dead-CR-%; build the eliminator only if >~25%. **Trap (red-team): XER.SO is sticky/
cross-block + record-form CR.SO depends on it; a call/context_barrier reads CR** — the
eliminator must handle SO + intervening barriers. Highest tier-2 value (host-testable,
measurable-before-building).

### UNIT 5 — Block-linking / branch elision (low risk, modest)
No branch when target == next block; invert conditionals so fall-through is not-taken.
`a64_seq_control.cc`. Ship when convenient.

### UNIT 6 (DEFER) — Hot-region cross-block regalloc
Call-free/barrier-free loop bodies only. Multi-week, silent-corruption blast radius. After
Units 1-4 + a mature differential harness.

## Dropped (verified dead/done): denormal-flush-skip (shipped), CMP+branch fusion (shipped),
## MR-as-NEON-orr (regalloc already handles), FRECPE/FRSQRT (not hot + Newton ULP risk),
## FCMA/jscvt/LDAPR (inapplicable/unsafe), LSE locks (shipped).

## Build order: Unit 4 instrumentation (host, safe, measure) ∥ Unit 1 (careful, qemu-gated)
## → Unit 3 (host-gated) → Unit 2 flip → Unit 5. All cvar-gated default-off, each a
## XeniaOptimizations toggle. Validate device-free (host xenia-cpu-tests + qemu-a64) BEFORE
## any device fire; the only device A/B vehicle is Burnout.
