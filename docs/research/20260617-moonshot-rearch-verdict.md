# Moonshot rearchitecture verdict (ultracode design + adversarial code-reading, 2026-06-17)

Two workflows ran: grounded codegen units + bold capability moonshots. Both had adversarial
red-teams that READ THE ACTUAL TREE. This doc = the honest moonshot verdict (the codegen verdict
is in 20260617-ppc-thor-hw-accel-rearch-plan.md; net there: only the FMA-V128 generalization
survived review + shipped default-off, 4b0d47cc4; HW LL/SC atomics was a double-byte-swap
black-screen landmine, caught by review).

## The moonshots, after the red-team read the code

**#1/#4 — Exact Xenos ROP / compute-ROP back-end (paraLLEl-GS for the 360): DO NOT START.**
The design synthesis claimed "xenia already has an exact ROP path, just finish it via ROAA, and it
fixes BD's overdraw." The red-team REFUTED this from the code:
- The exact-Xenos-ROP shader machinery (alpha-test kill, sample-mask, blend, in-shader depth/stencil
  in spirv_shader_translator_rb.cc:520-626) is gated EXCLUSIVELY on `edram_fragment_shader_interlock_`
  = `VK_EXT_fragment_shader_interlock`, which the Adreno 740 LACKS (falls back to kHostRenderTargets,
  vulkan_render_target_cache.cc:341-363).
- **`gpu_vulkan_edram_roaa` (VK_EXT_rasterization_order_attachment_access) is an UNCONSUMED STUB**:
  `edram_roaa_` appears exactly TWICE in the whole GPU tree (the constructor assignment :377 + the
  log line :382), read NOWHERE downstream. The cvar self-labels "EXPERIMENTAL, in progress" scoped to
  transfer-COPY elision (~9ms BTTF), NOT ordered blend/alpha-test ROP. **TRAP: do not mistake the ROAA
  stub for a working ordered-ROP path** - "finishing it" for overdraw = writing a non-FSI ordered-ROP
  shader variant FROM SCRATCH = most of moonshot #4.
- On a 99%-busy mobile TBDR, a software/compute ROP serializes BD's per-fragment overdraw through
  per-pixel atomics = STRICTLY SLOWER. paraLLEl-GS itself needs a DISCRETE GPU + isn't bit-exact.
  Accuracy-only at best, and BD overdraw is already addressed by the shipped thinning toggles.

**#2 — NEON SIMT re-vectorization of guest loops: the one survivor, but likely TARGET-LESS.**
Bit-exact (VMX128->NEON .s4 1:1, each lane independent - no cross-lane precision share = sidesteps the
arm64_vmx_dot_f32_fastpath black-screen scar). Device-free de-riskable under qemu-a64. BUT it needs a
HOT, DIVERGENCE-FREE, FIXED-STRIDE, SIMD-SHAPED guest loop in a CPU-bound title - and the evidence is
NEGATIVE: Burnout's #1 hot fn 0x82382798 (~16% CPU, disassembled this session) is BRANCHY integer/CR
game-logic (cmplw/beq/bne/subf, zero vector ops), exactly the not-vectorizable case; LO's hot code is
the idle multicore barrier + a game-logic stall. **GATE before any build (device-free): Ghidra a hot
LO/Burnout loop; if branchy game-logic / data-dependent addressing -> KILL (no target).** The prior is
that the gate FAILS on these titles (their hot code is control-flow, not data-parallel math, which is on
the GPU vertex shader anyway).

**#3 — GPU-driven command preprocessing: DEFER.** The cheap 80% already ships (multiDrawIndirect:
mdi_supported_ + indirect emission vulkan_command_processor.cc:5642-5715; the per-draw cpu_* buckets
already exist). Residual = the fragile pipeline-cache-on-GPU, gated by the 740's SINGLE queue (no
compute/graphics overlap - the win is pure CPU-offload, not parallelism). 1-fire MEASUREMENT (read
post-MDI cpu_* buckets) before any build; likely a wash.

## Honest aggregate
The moonshots, deeply designed + adversarially code-checked, LARGELY DO NOT PAN OUT on the Thor's
specific hardware (no FSI, single queue, no GMEM-map, mobile-ROP-slower) + the games' actual code shape
(branchy CPU-bound hot loops, not SIMD-shaped). The red-teams' VALUE = preventing months wasted on (a)
the ROAA-stub misread, (b) a mobile-hostile compute ROP, (c) a NEON-SIMT build with no target. The real
shipped wins remain the driver fence fix + the overdraw thinning toggles + (CPU-track) the modest exact
codegen units (FMA shipped; FlagM in-block + the rest are single-% careful units). NOT a silver bullet -
this is the honest ceiling of the current architecture on this hardware.

## Cheapest remaining de-risks (opportunistic, low cost)
1. DEVICE-FREE: confirm the #2 NEON-SIMT gate FAILS (Ghidra: are LO/Burnout hot loops branchy not
   SIMD-shaped?) - mostly already answered NO-TARGET by the Burnout 0x82382798 disasm.
2. 1 FIRE each (fill a cooldown): is VK_EXT_rasterization_order_attachment_access present on Turnip/740
   (read the edram_roaa= log) - only decides whether a transfer-elision BUILD is worth starting (modest,
   ~172ms resolve, NOT overdraw); and read post-MDI cpu_* buckets for #3.
Full agent output: scratch/moonshot_roadmap.txt + the task json.
