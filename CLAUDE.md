# CLAUDE.md — xenia-thor (Xbox 360 emulator on the AYN Thor)

## Goal
Make Xbox 360 games fast + playable on the AYN Thor (Snapdragon 8 Gen 2 / Adreno 740) via this xenia fork.
**TARGET = FULL-SPEED Xbox 360 emulation. Blue Dragon → locked 30fps at 720p with FULL default foliage**
(the 360 ran it at 30; the Thor is 10-20× more powerful + the emulation needs only ~4× the 360 ⇒ full speed
has large margin — see the ~4× budget below).
**⭐⭐⭐ COMMITTED ARCHITECTURE (user 2026-07-02, FINAL — this is THE plan, not a lever):**
**The emulator, on GAME-SELECT, does UPFRONT LOAD-TIME ANALYSIS of the XEX (the RPCS3 model — RPCS3
recompiles SPU/PPU at boot) and produces a NATIVE, HLE build of the game:**
1. **CPU = AOT-LLVM recompile the PPC to native** (we HAVE the whole-fn LLVM backend — precompile everything
   upfront, residency, direct calls; NO JIT/dispatch at gameplay). This is RPCS3's SPU-recompile, applied to PPC.
2. **GPU = HLE the Xbox D3D9 → Vulkan directly.** Load-time ANALYSIS of the XEX signature-identifies the
   static-linked XDK D3D9 functions (Draw/SetState/BeginTiling/Resolve/shader-setup); HLE-REPLACE them with
   host Vulkan (vertex decls→native input, cbuffers→push/UBO, shaders via Xenos→SPIR-V). This BYPASSES the
   PM4 command stream + EDRAM + predicated tiling ENTIRELY — the source of ~all the overhead. (XenonRecomp/
   XenosRecomp/UnleashedRecomp prove the translation; we do it GENERICALLY at load via a signature DB, not a
   per-game offline port.)
**⛔ STOP deepening the LLE PM4/EDRAM emulation. The flatten / scissor / tiling / EDRAM-in-GMEM / bin-once /
render-target-cache work is LEGACY STOPGAP on the OLD architecture (faithfully re-emulating the 360 GPU's
inefficiencies). The win is not making the LLE emulation of tiling faster — it's NOT EMULATING IT AT ALL
(HLE renders the scene once, natively, no tiles/EDRAM). Keep shipped LLE wins running; build NEW work on the
HLE+AOT track only.** See the LOAD-TIME XEX D3D-HLE section below for the build order.** Then Burnout, Gears, Lost Odyssey, Banjo → 30-60. Ship every win as a cvar-gated, stacking `XeniaOptimizations` toggle. **Active focus (user, 2026-06-27):
improve BD steady-state perf — smoother, LOWER WATTAGE + HEAT (not boot time) — via (1) ⭐ AOT LLVM = THE CORE
DIRECTION (user 2026-06-29): the ReXGlue / XenonRecomp model — STATIC AHEAD-OF-TIME recompilation, precompile
the whole title to native, NO JIT/dispatch at gameplay (opcode coverage is already 100%; the work now is AOT +
residency + direct calls — see the AOT bullet below); (2) UMA zero-copy [FIXED + enabled default-on 2026-06-27,
present-hang gone]; (3) CPU/GPU NEON [VMX→NEON, FP32 dot=fmul+faddv]; (4) TURNIP — the Mesa/Adreno Vulkan driver
(the fast+correct path; the KGSL blocking-fence fix was the project's biggest win; lever = driver-level perf +
Adreno features). Fix LLVM bugs as found.**

### 🔑🔑 BD-30 APPROACH — read before any perf work (REWRITTEN 2026-07-01 from driver-measured data)
- **CURRENT STATE: BD SHIPPED at ~19.8fps** (GameProfile: `gpu_fp10_color_as_unorm10` + `gpu_vrs_foliage_rate=4`
  + `gpu_force_max_msaa_samples=2`; quality option = VRS 2x2 at 17.0fps). Baseline was 10.8. Frame floor
  ~49ms on the heaviest field scenes. Target remains locked-30 (33ms).
- **✅✅ THE FRAME ANATOMY IS DRIVER-MEASURED (2026-07-01, Turnip u_trace): GMEM tile loads+stores ≈ 1ms/frame
  TOTAL** (the historic "74ms tile-I/O" model was WRONG). Pass count, barriers, EDRAM structure ≈ ~6ms. **⚠️ The
  "~90% fragment execution" read of this trace was WRONG — 2026-07-02 device tests prove the "draw execution"
  bulk is VERTEX/GEOMETRY (foliage), NOT fragment (see THE REAL BOTTLENECK below).**
- **🚨 "RESOLUTION-INVARIANT" was RIGHT (for the wrong reason): resolution changes DON'T help BD not because
  pixels are structure-bound but because BD is NOT fill-bound at all — it's foliage-VERTEX-bound. Confirmed
  2026-07-02: `gpu_resolution_downscale_pct=71` (half pixel area, dims confirmed shrunk) = FLAT frame time.**
- **MEASURED LEVER VERDICTS (2026-07-01, all heavy-field, driver-trace era):** SHIPPED: fp10 32bpp color
  (−8ms), VRS foliage (2x2 clean / 4x4 perf). CLOSED: fp16-relaxed (+11ms regression), cap=1-on-stack (no win
  + ghost), vrs_all_draws (null => opaque shading insignificant), LRZ (upper-bound ZERO — co-planar foliage
  unoccludable; do NOT build the driver patch), buffer path (4.7x loss), barrier scoping, gmem/sysmem force,
  inpass=2 (safe 25% pass cut, frame flat), retro depth/color elision (classes ~2/~0), UBWC (timing-neutral).
- **🚨 THE REAL BOTTLENECK (device-proven 2026-07-02, two clean single-run tests): BD's field is VERTEX/GEOMETRY-bound, specifically FOLIAGE. NOT fill/EDRAM/overdraw.** (1) `gpu_resolution_downscale_pct=71` shrank all RT images to half pixel AREA (pass dims 720x768->511x545, confirmed) => frame time FLAT 49->50ms = NOT fill-bound. (2) `gpu_diag_skip_alpha_test_draws` (drop foliage) => 49->34ms (20->29fps) = foliage costs 15ms; and since downscale also shrank foliage fragments with no effect, that 15ms is foliage VERTEX processing (262k verts, ~600x native via Turnip SSBO vertex-fetch latency), not its alpha-test overdraw. **=> THE BD-30 LEVER = cut foliage vertex cost: hardware vertex-fetch (SSBO->native input assembly, [[bd-hw-vertex-fetch-lever]]) and/or foliage decimation/LOD.** This RETIRES the fill/EDRAM/MSAA/resolution premise (why VRS/cap/fp10/downscale/11-resolution-patches were all limited or inert). The flatten's 49->31ms was real but from dropping needed DRAWS (broke pixels) - same root (geometry cost), wrong method. Non-foliage scene = 34ms floor, so foliage reduction + a small extra cut reaches 33ms/30fps.
- **MEASUREMENT: use the driver-trace harness for every verdict** — `gpu_vulkan_driver_env
  'MESA_GPU_TRACES=print;MESA_GPU_TRACEFILE=...'` (per-pass render_pass/gmem_load/gmem_store GPU timestamps;
  parse + hole analysis = where untraced time sits), plus the pass-split log (vulkan_trace_pass_timestamps +
  gpu_trace_resolve_timing). TU_DEBUG via `gpu_vulkan_driver_debug` (setenv pre-dlopen; mesa.tu.debug property
  does NOT reach the driver). Scene tell for nav-desync: gpu_busy < 90% + tiny screenshot = menu, discard run.
- **⭐⭐ AOT LLVM = THE CORE CPU DIRECTION (user mandate 2026-06-29). Model = ReXGlue / XenonRecomp + RPCS3:
  PRECOMPILE EVERYTHING AHEAD OF TIME, zero JIT/dispatch at gameplay.** ReXGlue (github.com/rexglue/rexglue-sdk)
  is a static PPC→C++→native `-O3` recompiler (Xenia/XenonRecomp lineage) — its whole speed comes from turning
  guest regs into C++ locals the host compiler keeps in registers across the whole function, and direct native
  calls with no dispatch. Adopt that here: **(a) precompile every guest function** (AOT object cache, started
  123a6095a — push to addObjectFile / skip lowering); **(b) full register RESIDENCY** (guest regs as allocas at
  entry → mem2reg → SSA → host regs, store back only at calls/exit — the #1 perf lever, currently partial);
  **(c) DIRECT native calls** (inline-cache the guest call: resolve→direct machine_code, kill the ~13% helper +
  thunk + virtual `Function::Call`); **(d) NEON for VMX.** Compile/boot time is IRRELEVANT — never optimize for
  it, report it as a cost, or gate features on it; only GAMEPLAY codegen quality matters. Payoff: CPU-bound
  titles (Burnout/Gears → 30) directly, and BD's HEAT/wattage → sustained GPU clock (BD field is GPU-bound, so
  AOT lifts its SUSTAINED fps + smoothness, not its peak). OPEN BLOCKERS to make AOT the default BD backend: the
  opt=2 residency crash (092eacdc3) + the LLVM-renders-BD-cyan codegen bug [[bd-llvm-postload-3d-cyan-bug]] —
  fix forward. [[llvm-jit-backend-build]]
- **✅ THE #1 INEFFICIENCY WAS THE BUILD ITSELF (-O0) — found+fixed 2026-06-28.** `githubDebug` mapped its
  native ndkBuild to PREMAKE config `Debug` = `optimize("Off")` = -O0, so the ENTIRE host emulator (CP, dispatch,
  kernel) ran UNOPTIMIZED — confounding EVERY prior BD perf number. Switched the debug variant's native config to
  Release/-O2 (cc1924d82): libxenia-app.so 301MB→99MB, ~15% trivial-accessor overhead (`__cxx_atomic_load`,
  `Function::address`, `unique_ptr::get`) inlined away. **⚠️ FPS CLAIM CORRECTED 2026-06-28: the "~4→19.9fps ≈5×" was OVERSTATED. (1) The VdSwap-window count is ~2× the TRUE rate — the in-app OSD (green "X FPS" box, screenshot it) is the truth (e.g. BD title screen OSD=29.7fps). USE THE OSD, NOT VdSwap-count. (2) The heavy scene I profiled rendered as GARBLED cyan-bars (screenshot-caught), NOT BD's verified 3D field — so its fps was of a wrong/corrupt scene. LLVM renders BD CORRECTLY at the title (~30fps OSD); a heavier scene corrupts (gpu_uma_direct_shared_memory suspect). The -O2 + dispatch CODE wins are still real (qemu-validated, .so 301→99MB); only the fps NUMBERS were wrong. Re-measure the CORRECT field via OSD before any fps claim.** [[thor-build-was-o0-now-o2]]
- **Real -O2 bottleneck = GUEST-CALL DISPATCH plumbing** (BD is call-heavy) — NOT rendering / register-residency /
  3-thread-serialization (those were -O0-confounded verdicts). SHIPPED (qemu byte-validated, device-confirmed at
  19.9fps, b34a0f69b): per-call `ThreadState::Get()` TLS → derive `ts` from ctx/x20 (TLS 11.7%→6.4%); RTTI
  `dynamic_cast<GuestFunction*>` → `is_guest()` (−4.7pp, gone from profile). **NEXT levers from the -O2 profile:**
  non-tail guest-call INLINE-CACHE (resolve→direct machine_code call, skip helper+thunk+virtual `Function::Call`,
  ~13%: `xe_llvm_guest_call` 8.3% + `GuestFunction::Call` 2.8% + `A64Function::CallImpl` 2.1%); `xe_llvm_call_extern`
  TLS (residual ~6%); PM4 `WriteRegister` ~6.6%; HLE RtlEnter/Leave critical-section trampolines ~5.4%; `cas2`/
  global-lock 2.3%. Residency (092eacdc3) is DEPRIORITIZED — it CRASHES at opt=2 on device + only touches the
  guest thread (~1.2×); the dispatch wins are bigger + lower-risk.
- **RELIABLE-FPS GOTCHA: clear logcat + 64M buffer + count VdSwap in a TIMED WINDOW** (`bd_hostprofile.ps1`) — the
  sw1−sw0 delta goes NEGATIVE at -O2 (higher log throughput rotates the buffer → sw1<sw0 → bogus negative fps).
- **DO NOT REBOOT THE DEVICE.** It does NOT degrade from repeated launches; never blame the device for a result.

### ✅ LLVM 100% EMITTABLE-OPCODE COVERAGE ACHIEVED (2026-06-27, device-validated)
Every opcode the PPC frontend EMITS is now LLVM-native (verified: `comm` of opcodes.h vs llvm_assembler.cc
cases leaves only ATOMIC_EXCHANGE, LVR/STVL/STVR, RESERVED_LOAD/STORE — all UNEMITTABLE dead opcodes:
ATOMIC_EXCHANGE has no a64 sequence; lvlx/lvrx/stvlx/stvrx are lowered by the frontend as
PERMUTE+LoadVectorShl+vector-LOAD/STORE+BYTE_SWAP; lwarx/stwcx use LoadContext(reserved_val)). BD renders
with full coverage (VdSwap 758, 0 faults). The final additions: PACK/UNPACK (all 18 formats via xe_llvm_pack/
unpack/pack2 C helpers), vector LOAD/STORE (4×32-bit decodable), MEMSET, DID_SATURATE (stub 0), LOAD_MMIO/
STORE_MMIO (baked MMIORange callback), LOAD_CLOCK, + the SVE-disable fix. Detail: [[llvm-jit-backend-build]].
**NEXT for LLVM = PERF (residency/codegen quality), not coverage.**

### LLVM coverage history (how it got to 100%)
DONE — ~28 opcode handlers, ALL qemu-a64 differential byte-identical (31 tests / 1984 assertions) and the
core set DEVICE-VALIDATED on BD (VdSwap 816, 0 faults, default-on): scalar core, 8 vector groups, calls,
control (DELAY_EXECUTION, SET_ROUNDING_MODE, SET_NJM, DEBUG_BREAK/_TRUE), atomic CAS, cache_control,
lvsl/lvsr, INSERT/EXTRACT, MUL_ADD/MUL_SUB V128 (full VMX denormal-flush + PPC NaN fixup), DOT_PRODUCT_3/4,
VECTOR_AVERAGE, VECTOR_DENORMFLUSH, SWIZZLE, PERMUTE (I32 word + V128 byte tbl2 + V128 halfword), TO_SINGLE,
ROUND, RECIP, RSQRT (f32 inline / f64+V128 via runtime helpers), LOG2/POW2, and V128-constant
materialization in `V()`. Reusable infra in llvm_assembler.cc: `VmxFlushDenorm` / `VmxNanFixup` (PPC FP
semantics) + `EmitVecLaneCall` (per-lane runtime helper). Runtime helpers in llvm_backend.cc
(absoluteSymbols): xe_llvm_vrsqrte_lane / xe_llvm_frsqrte (replicated 360 estimate tables, qemu-verified) +
xe_llvm_log2_lane / xe_llvm_exp2_lane (libm). ATOMIC_EXCHANGE has NO a64 seq (unemittable) → skip.
ALSO DONE + DEVICE-VALIDATED (2026-06-27, fixed FORWARD per user "FIX... FORWARD", not reverted): **vector
128-bit LOAD/STORE** (as 4 volatile 32-bit accesses = each decodable by the fault handler; a single q-access
that faults can't be decoded → BD hangs), **MEMSET** (llvm.memset), **VEC128 BYTE_SWAP** (llvm.bswap.v4i32),
and the **SVE-disable codegen fix** (`-sve,-sve2,-sme` in target-features — detectHost enabled SVE2 on the
X3 but it SIGILLs on the Thor; was the MEMSET storm root cause). LVL/LVR/STVL/STVR are UNEMITTABLE (frontend
uses the vector-LOAD+PERMUTE path). See [[llvm-jit-backend-build]] for the SVE + 4-scalar details.
REMAINING long-tail (a64 fallback works meanwhile): PACK/UNPACK (9 formats each — D3DCOLOR/FLOAT16/SHORT/
2101010/8_IN_16/16_IN_32, custom Xenos half-float + TBL + clamps, delicate; BD histogram = UNPACK),
RESERVED_LOAD/STORE (lwarx/stwcx — needs A64BackendContext reservation state; reserve x19 or a thread-local
helper), LOAD_MMIO/STORE_MMIO (MMIO range callbacks — rare), LOAD_CLOCK (non-deterministic → untestable),
DID_SATURATE (cross-op saturation-flag tracking — hard). Method: read the a64 sequence, match byte-for-byte,
add a differential test with adversarial inputs, `bash scratch/thor-debug/build_run_llvm.sh` (qemu), then a
device render check (qemu CANNOT catch SVE-SIGILL / q-decode hangs — always device-validate new mem/codegen).

## ⭐ PRIMARY BUILD: LLVM whole-function CPU backend — SHIPPED, default-on, now PERF-tuning
**LLVM is the way forward, no matter what (user, 2026-06-27).** The whole-function HIR→LLVM IR→ORCv2
recompiler RENDERS Blue Dragon gameplay on the AYN Thor (full LLVM, 3793 fns, 683 frames, 0 faults) and
is **default-on** via the `opt_llvm_backend` XeniaOptimizations toggle (cvar `cpu_backend_llvm`, opt
level `cpu_backend_llvm_opt`=2). Lives in `src/xenia/cpu/backend/llvm/`; `LLVMBackend:A64Backend`,
x20=ctx/x21=membase reserved ABI, hybrid (unsupported opcodes fall back to the a64 JIT). Full detail +
the build recipe: memory file [[llvm-jit-backend-build]].
- **What it cost to get rendering (all committed, qemu 896/896):** CALL_EXTERN recursion, 32-bit guest
  EA sign-ext, conditional-call self-loop, terminator-in-block, compile-race SIGBUS, POSSIBLE_RETURN/TAIL,
  and the FINAL one = the **return-address basis for tail-call-entered fns** (`my_ret_addr_=(x0!=0)?x0:
  context->lr`; __restgprlr restores context->lr so an entry-lr basis mis-detected its blr as a forward
  call → infinite recursion). Tooling built: `cpu_backend_llvm_max_fns` (monotonic count-gate bisection),
  `_range_lo/_hi`, `_dump_ir`, `_trace_addr`, `LLVMfallback` opcode histogram — all gated, 0 default cost.
- **PERF FOCUS NOW (user priority 2026-06-27): steady-state smoothness, LOWER WATTAGE + HEAT, more perf —
  NOT boot time** ("boot compile doesn't bother us if the game is smoother"). So the on-disk object cache
  is DEPRIORITIZED; the lever is making the JIT'd code FAST:
  1. **RESIDENCY (the core win) — currently PARTIAL.** Direct-context model: LOAD/STORE_CONTEXT →
     load/store on the opaque PPCContext ptr. At opt=2 GVN forwards LOADS across blocks (hot fn 0x824694A0:
     ~1770 ctx ops → 181 loads, a real win vs a64's per-block reloads) but 372 STORES remain (GVN can't
     prove dead-store elimination through the opaque ptr + call barriers). **FULL residency (RPCS3-class) =
     switch guest regs to ALLOCAs at entry → mem2reg promotes to SSA → host regs, store back only at
     calls/exit.** This is the #1 LLVM perf lever; verify each change with the `_dump_ir` ctx-access count.
  2. **OPCODE COVERAGE** (fewer a64 fallbacks = more fns get residency). Lowered so far: scalar core, 8
     vector groups, calls, YIELD, ADD_CARRY, IS_NAN, TRAP/TRAP_TRUE. Top remaining fallbacks (LLVMfallback
     histogram): atomic_compare_exchange, mul_add/mul_sub (FMA — keep PPC NaN propagation), load_vector_shl/shr,
     cache_control, unpack. NOTE: inline asm report_fatal_errors in the ORCv2 AsmPrinter — use intrinsics/IR, never inline asm.
  3. **NEON for vectors** (VMX→NEON; user "improve gpu/neon stuff"): DOT_PRODUCT=fmul+faddv (FP32, NEVER
     int8 sdot), PERMUTE/SWIZZLE=shufflevector/tbl, PACK/UNPACK, vector mem+byteswap.
- **⚠️ UMA MUST BE FIXED (user directive 2026-06-27):** `gpu_uma_direct_shared_memory` is default-off +
  was found SAFE but fps-neutral on BD's heavy field ([[uma-safe-but-not-bd-heavy-lever]]) — the user
  wants it working/enabled regardless (zero-copy guest↔GPU memory = lower bandwidth/heat on the Thor's
  UMA). Re-evaluate, fix any bug, and enable it (smart_sync or double-buffer path).
- **Validation:** qemu-a64 differential ([[a64-qemu-harness]]) 896/896 + device render/no-fault, every change.

## ⭐⭐⭐⭐ CORE DIRECTION (user 2026-07-02): LOAD-TIME XEX D3D-HLE — "reach into the game, drag shit out to optimize, like RPCS3 does with SPU"
**THE model: at LOAD time our emulator READS the game's XEX, IDENTIFIES the statically-linked XDK D3D9 code by
SIGNATURE (exactly how RPCS3 identifies + AOT-recompiles SPU code at boot), and HLE-REPLACES the perf-critical
D3D9 functions (BeginTiling/EndTiling, Resolve, DrawIndexedPrimitive, state setup) with OPTIMAL host Vulkan
implementations.** This is the resolution of the "why not D3D-HLE" question:
- Runtime D3D-hooking is IMPOSSIBLE (360 D3D9 is static-linked/inlined in the XEX, no runtime boundary like PC's
  d3d9.dll). BUT LOAD-TIME analysis sidesteps that — read the XEX, pattern-match the XDK D3D9 functions, install
  HLE trampolines (xenia ALREADY does exactly this for xboxkrnl imports; extend it to signature-identified
  static functions). GENERIC because every 360 game static-links the SAME XDK D3D9 (signatures stable per XDK
  version) -> a signature DB works across games = NOT a per-game port (which the user rejected).
- WHY it's the BD-30 win: HLE BeginTiling/Resolve = bypass the predicated tiling + EDRAM round-trip AT THE
  SOURCE (render once, resolve once) = the clean bin-once by construction, intercepting the high-level INTENT
  instead of reconstructing it from the PM4 stream (the RT-cache base/offset coalescing I was pinning is the
  harder, lossy way to the same end). Payoff compounds: any HLE'd D3D function skips ALL its guest-CPU cost too.
- HONEST catches: signature coverage across XDK versions (2005-2013; start with BD's), small INLINED calls
  (SetRenderState) can't be matched (but the big tiling/resolve/draw fns aren't inlined = the ones that matter),
  host implementations are real work (a perf-critical SUBSET suffices, per UnleashedRecomp). Build order:
  (1) pin BD's BeginTiling/EndTiling/Resolve addrs in Ghidra (bd.gpr ALREADY imported; tile-walker ~0x826bf...
  already RE'd), (2) HLE-trampoline them via xenia's function-HLE path, (3) host BeginTiling=noop + Resolve=one
  full-surface resolve = bin-once, (4) generalize into a signature DB. Track 2 REFRAMED: "HLE the identified
  D3D tiling/resolve functions at load time", not "reconstruct bin-once from PM4". ir3 measurement (built) +
  intrinsically-huge foliage shaders (~4000 instr, ~1.5x bloat only) stand: HLE gives the STRUCTURAL ~1.4x +
  EDRAM/CPU bypass, shaders stay the game's (VRS/quality options for those).

### 🚀 SESSION PROGRESS 2026-07-03 — the RE wall broke; the D3D tiling fn is HLE-REPLACED with a RUNNING host body
Build order steps 1-2 DONE, step 3 IN PROGRESS (host body plants + runs on-device). Full detail: memory
[[bd-d3d-hle-re-state]] + skill `xbox360-d3d-hle-recomp`. The concrete wins (all committed):
- **PAGE-WATCH tool (`cpu_watch_guest_write_page`, default-off) — THE unblock.** Host-mprotect a guest page +
  EMULATE-ON-FAULT (a64_backend.cc ExceptionCallback: decode store size[31:30]+Rt[4:0], do the write to
  ex->fault_address() yourself, keep protected = catch EVERY write) + code_cache->LookupFunction(ex->pc()) +
  ctx->lr → the guest fn + caller. NO per-store JIT bloat → BD runs FULL SPEED → reaches the field (the store-
  watch `arm64_guest_store_watch` could NOT — ~10x slowdown desyncs the timed nav). This pinned what 20+ turns
  of store-watch/stack-walk/save-state could not.
- **BD's D3D tiling fn PINNED + FULLY DECODED = FUN @ 0x82487CC0** (xenia's declared entry; grep logcat
  `shared-fn-harvest 82487CC0-82487F50`; 0x82487cc8 is 8 bytes IN = wrong for the intercept). It's the deferred-
  D3D-command REPLAY: all 13 opcodes decoded (0x80=memcpy tile-table→0x40011330; 0x82=emit PM4 via ring writer
  0x8246E100 incl RB_SURFACE_INFO pitch=360; 0x88=tile-loop on ctx[0x78]=count; 0x84-0x8c=predication/bin).
- **HLE HOST BODY WRITTEN + RUNS (`cpu_hle_tiling_replay_addr=82487CC0`, default-off):** reimplements the token
  loop in host C++, calling guest helpers (ring writer + tiling fns) REENTRANTLY via `Processor::Execute` (viable
  = only ~dozens of setup PM4/frame; draws go via the driver/walker). DEVICE: **planted=1, faults=0** = the host
  body IS BD's D3D tiling fn + executes without crashing (step-3 mechanism PROVEN). WIP: VdSwap=3 = first-cut
  replay stalls BD (debugging: iters/final-token diagnostic added). Then: force count=1 + COHERENT surface
  rewrite (pitch+base+resolve TOGETHER — piecemeal downstream = 0fps black EDRAM desync, device-proven).
- **GOTCHAS:** downstream RT-pitch override (render_target_cache) = 0fps black (EDRAM desync) → must HLE at
  SOURCE. Fns reached by tail-call/indirect (driver 0x82487988) NEVER LookupFunction'd = un-plantable; only
  harvested entries plant. VdGlobalDevice ptr = *(0x820005f4). Helpers: memcpy 0x826bf770, calloc 0x8246B3B8.
- **LOAD-TIME PIPELINE (RPCS3 model):** at game-load, AOT-LLVM the PPC (have the .o cache) + signature-scan the
  XEX → install HLE trampolines for the D3D fns (the mechanism above), cache keyed by XEX hash. Page-watch RE's
  each game's BeginTiling/Resolve ONCE → their byte-signatures seed a DB → loader matches + trampolines (generic,
  not per-game). = build-order step 4.

## ⭐⭐⭐ LONG-TERM DIRECTION (user-decided 2026-07-02): GENERAL EMULATOR + generic HLE/AOT techniques
**DECISION: keep xenia as a GENERAL-PURPOSE runtime emulator (runs many games) and bring in the SPEED
TECHNIQUES from the recompilation world GENERICALLY - NOT per-game native ports.** User verbatim: "we dont
want to port EVERY game native... we want the general purpose emulator which all the tricks to get working
for many games." So UnleashedRecomp/XenonRecomp are REFERENCE for TECHNIQUES, not a template to copy per-game:
- Borrow: native vertex-input submission (vs SSBO vfetch), HLE-style optimal rendering that BYPASSES the
  guest GPU inefficiencies (predicated tiling / EDRAM round-trips) GENERICALLY in xenia's Vulkan path, AOT
  LLVM (already have the whole-fn backend). XenosRecomp's "vertex decl -> native input layout" is the model.
- DON'T: recompile each game to a native exe (UnleashedRecomp is a per-game PORT = explicitly rejected).
- BD-30 within this: (a) GENERIC predicated-tiling bin-once (the flatten done RIGHT: 2 tiles share one
  full-surface host RT + coherent resolves - benefits ANY tiled game, not a BD patch); (b) generic native
  geometry submission (cuts the ~56% geometry cost that resolution can't touch); (c) resolution scaling as a
  user QUALITY option (cuts the ~44% fill cost - gpu_resolution_downscale_pct, shipped). Keep every win a
  cvar-gated, multi-game-validated XeniaOptimizations toggle.
- Refs cloned ../reference/ (XenonRecomp/XenosRecomp) - study for TECHNIQUE, apply generically. Skill:
  xbox360-d3d-hle-recomp (reframe as "generic HLE techniques in xenia", not per-game port).

## 🎯 EXECUTION PLAN (user-decided 2026-07-02, all Qs answered) — build order + specs
**DIRECTION: general-purpose xenia + generic recompilation-world techniques (NOT per-game ports). Build
LLVM/AOT FIRST, then generic HLE. FULL QUALITY (no quality-reduced Performance preset - downscale/decimation
are user QUALITY OPTIONS only, not the 30fps path). Validate every technique MULTI-GAME from the start.**

### TRACK 1 (NOW): RPCS3-style AOT LLVM (user: "select a game -> it compiles stuff", user-selectable dir)
- UX: **UPFRONT compile-on-select** (RPCS3 PPU-compile style) - precompile ALL guest functions to native with
  a PROGRESS BAR before gameplay; every later launch is instant + fast.
- Cache dir: **default app-storage (files/aot_cache) + a Settings PATH-PICKER override** (SD/USB/custom).
- Artifact: **FULL NATIVE OBJECTS (max speed)** - cache final linked .o, re-linked per run; the non-portable
  functions (MMIO callbacks / baked extern Function* ptrs) stay JIT via the `nocache_` prefix; weak-typeinfo
  shim handles the -fno-rtti libLLVM boundary. FOUNDATION EXISTS: src/xenia/cpu/backend/llvm/llvm_object_cache
  .{h,cc} (ORCv2 ObjectCache, per-fn .o, getObject skips MCAssembler, notifyObjectCompiled persists,
  kLlvmObjectCacheVersion dir-gate, nocache_ handling). REMAINING: (1) upfront compile-ALL pass (currently
  lazy/JIT-as-hit + cache) with progress reporting; (2) user cache-dir cvar + Settings picker + Java wiring;
  (3) flip full-native-object relink default-on after multi-title validation. Existing LLVM backend: whole-fn
  HIR->LLVM->ORCv2, renders BD; open bugs to fix forward = opt=2 residency crash + BD-cyan codegen
  [[bd-llvm-postload-3d-cyan-bug]]. Payoff: CPU-bound titles (Burnout/Gears) + heat/wattage everywhere.

### TRACK 2 (AFTER AOT): generic HLE-style techniques in xenia (multi-game)
- Generic PREDICATED-TILING BIN-ONCE (the flatten done RIGHT: the 2 tile passes share ONE full-surface host
  RT + coherent per-region resolves - a general xenia render-path feature, benefits ANY tiled game). Cuts the
  binning-doubling. [[task 39]] has the RE + the honest complexity (separate RTs/resolves = EDRAM-addressing).
- Generic NATIVE GEOMETRY submission (XenosRecomp model: guest vertex decl -> native Vulkan input layout,
  replacing SSBO vfetch). Attacks BD's ~56% GEOMETRY cost that resolution can't touch = the full-foliage-30
  piece. (gpu_hw_vertex_fetch was a partial attempt, flat - the general native-input path is the real build.)

### THE BD-30 PHYSICS (rigorous, 2026-07-02) - why this plan
BD's field is BOTH fill-bound (~44%, rigorous gpu_diag_raster_ab: full-raster 94ms vs quarter-area 53ms on
ONE scene) AND geometry-bound (~56%). Resolution scaling cuts fill (keeps foliage, softer); native geometry
cuts geometry (keeps foliage). Full-foliage 30 = BOTH, at full quality => the generic HLE geometry path is
mandatory (not a lever). The "not fill-bound" and pure-geometry earlier verdicts were CROSS-RUN CONFOUNDS.

## (superseded framing) D3D-HLE + AOT recompile

## (superseded framing) D3D-HLE + AOT recompile
**The strategic ceiling = STOP emulating the Xbox 360 GPU at the PM4 command level (xenia's LLE, which
faithfully reproduces inefficiencies like BD's predicated tiling) and instead TRANSLATE the D3D API to
Vulkan directly (HLE) + AOT-recompile the PPC to native.** This is PROVEN, not theoretical:
- **hedge-dev/XenonRecomp** = Xbox360 PPC XEX -> C++ (static/AOT recompilation; x86-only today, uses x86
  intrinsics). **hedge-dev/XenosRecomp** = Xenos shader bytecode -> HLSL -> DXIL/SPIR-V. **UnleashedRecomp**
  (Sonic Unleashed) = the full proven port: *"implements a translation layer for the renderer rather than
  emulating the Xbox 360 GPU"* -> vertex decls -> native input, cbuffers -> root/push. Runs at high FPS with
  NONE of the tiling/EDRAM overhead. Inspired by N64Recomp/Zelda64Recompiled. **ReXGlue** (goal-named) = the
  same static-recompile lineage for 360.
- **WHY it kills BD's bottleneck:** BD's field is GPU-bound on foliage triangle-binning DOUBLED by predicated
  tiling. HLE renders the scene ONCE, optimally on Turnip (no tiling, no EDRAM round-trip) -> the bin-once win
  by construction. AOT is orthogonal (helps CPU-bound Burnout/Gears + heat; BD field is GPU-bound so AOT is
  thermal-only there).
- **THE HARD PARTS (honest):** (1) XenonRecomp is x86-only -> needs an ARM64 backend for the Thor (we already
  have an LLVM whole-fn backend in xenia to draw from). (2) It's a per-GAME offline PORT (recompile BD ->
  native ARM64 Android app + HLE renderer), NOT a general runtime emulator like xenia. (3) The D3D-HLE renderer
  is real work (dxvk-scale in general; but UnleashedRecomp shows a per-title subset is tractable). (4) Xbox
  D3D is statically linked/inlined in the XEX (no clean boundary) - the recompile approach SIDESTEPS this by
  recompiling the game's D3D-call sites and providing host implementations, which is why it works.
- **THE FORK (user deciding):** (A) incremental HLE inside xenia (keep the general runtime base, add targeted
  HLE like the one-tile patch - works now, lower ceiling); (B) pivot to a XenonRecomp-style "Blue Dragon
  Recompiled" (per-game native ARM64 port + HLE renderer - proven-super-fast ceiling, huge effort, needs the
  ARM64 recomp backend); (C) hybrid (xenia now, build the HLE renderer incrementally, share the LLVM backend).
- **RAG/KNOWLEDGE:** a retrieval DB over Xbox360 XDK D3D9 docs + Xenos/PM4 reference + XenonRecomp/UnleashedRecomp
  source would accelerate BOTH the tactical RE (finding tiling/BeginTiling) and the HLE build. Proposed as a skill.

## Autonomous mode (standing user directive)
Pick the highest-value unit yourself and execute end-to-end (implement → build-verify → device-test →
commit → next). Don't ask which task / re-confirm direction / analysis-paralyze. A big effort is a reason
to start, not to ask. Surface only genuine external blockers. The thermal + no-fabrication rules below are
safety, not "asking" — they always hold.



### ⚠️ PERFORMANCE-MODE SHADING STACK FAILS 30 (measured 2026-07-02, OSD): downscale 45% + decimation 55% + VRS 4x4 + fp10 + cap2 = **8.9 FPS OSD** (heavy bloom field, gpu_busy 99%) - WORSE than the 19.8 baseline. Render is CORRECT (coherent, thinner foliage, softer) but downscale REGRESSES fps (confirms the earlier rigorous 'downscale makes BD slower' via RT-scaling overhead). => built shading levers do NOT reach 30 (cap ~20 or regress). BD-30 needs the HLE bin-once (multi-session, target 82487F58) + working levers, NOT the shading stack. Do NOT re-chase aggressive downscale for fps.

## 🔬🔬 GREP THE EXPERIMENT LEDGER BEFORE RUNNING ANYTHING (anti-repetition RAG)
**`docs/research/experiment-ledger.md` is the grep-able index of EVERY experiment ever run + its DEAD/FLAT/WIN
verdict. BEFORE running ANY device experiment or building any lever: `grep` the ledger for the lever name +
keywords. If it's there with DEAD/FLAT, DO NOT re-run it — build on the note instead. ADD every new result to
the ledger the moment you get it.** This exists because we REPEATEDLY re-ran already-settled experiments
(native-vertex-fetch=flat, EDRAM=black, draw-merge=regresses) — the large prose memory files aren't grep-
friendly, so retrieval failed and we burned device runs re-deriving dead ends. The ledger is the fix: retrieve,
don't re-run. (Practical RAG = a structured grep-able ledger, since this CLI env has no vector DB.)

## ⚠️ PLAN FROM FIRST PRINCIPLES — device A/Bs here LOOP on confounds
**Derive the structural cause from CODE + the known 360/Xenon/Adreno ARCHITECTURE + the DBT/emulation
literature FIRST. Form a falsifiable hypothesis. ONLY THEN fire the device — to CONFIRM that one
hypothesis, never to "explore."** Device experiments here have repeatedly produced a wrong number → a
wrong verdict → weeks chasing a non-lever (GPU-bound⇄CPU-bound flip-flops, "pixel-independent", "FDM is
THE lever" → FDM was device-DEAD). The real structural taxes are DERIVABLE without the device and don't
need firing to find: per-block register round-trips through PPCContext memory, CPU↔GPU fence
serialization, the global guest-atomic lock, the single-threaded guest, the EDRAM RAM-round-trip — all
code-facts + arch-mismatches. **Build the model from code/arch/literature; the device only validates the
FINAL hypothesis. The emulation gap (~5× beyond typical) is a STACK of compounding ~2× structural taxes,
not one bug — attack the stack, derived not measured.**

### Measurement is the #1 trap (for when you must measure)
**BD's GPU scene complexity changes ~4× in 500ms of guest time** (267 draws @ guest_ms 14007 → 1126 @
14516, device-measured 2026-06-25). Consequences, learned the hard way:
- **Cross-fire / relaunch A/Bs are WORTHLESS.** Two separate launches reach different guest_ms = different
  scenes = invalid comparison. EVERY "config X = N fps vs baseline M" from separate runs is confounded —
  including the historical "BD GPU-bound on binning", "pixel-independent / 480p inert", "thinning 1.7×",
  "VRS −22%", "fence-fix +46%". **Treat all such numbers as UNVALIDATED until re-proven the rigorous way.**
- **`gpu_frame_us` is unreliable** — a SPAN that includes GPU-idle time (RT-dump readbacks also poison it).
  Never reason from its absolute value. KGSL `gpu_busy_percentage` (busy-vs-idle) is the trustworthy signal.
- **The ONLY rigorous perf A/B = single-run, IN-PLACE ALTERNATING:** freeze ONE frame, flip the cvar in
  ~16-frame windows within that one run, compare the per-phase delta. Infra: `gpu_freeze_ab_alternate_vrs`
  (vulkan_command_processor.cc ~2280). Mirror it to test any new lever.
- **Confirm the frozen frame is GPU-BUSY** (KGSL busy% high) before trusting a delta — a frozen LIGHT scene
  is GPU-idle (busy ~2%) and its numbers are meaningless. Freezing a precise guest_ms is unreliable (boot
  speed varies) — prefer the alternator's "free-running past guest_ms" trigger.
- **RULE 0: never measure movies/intros** (full-speed video decode inflates fps). Confirm via screenshot
  it's live gameplay, not a cutscene/menu (a movie keeps changing with no input; many geometry draws = real).
- **VdSwap/s** = real user-visible fps, but only meaningful at a MATCHED scene (cross-fire can't give that).
- **Until a lever is confirmed by a single-run alternating A/B on a busy frame, it is UNVALIDATED — say so;
  do not claim a fps number.** Reliable evidence: code facts (file:line), host cpu-tests (byte-identical),
  qemu-a64, single-run alternating deltas, pixel-correctness (screenshot), commit hashes / what shipped.

### ⚠️⚠️ TEST WITH ALL OPTIMIZATIONS ON — the silent-default-off confound (learned the hard way 2026-06-25)
The single source of truth for shipped optimizations is **`XeniaOptimizations.java`** (global, auto-applied by
the app UI launch) + **`GameProfiles.java`** (per-game overrides). **`am start --ez` test launches BYPASS BOTH**
— they set ONLY the cvars you explicitly pass. So any device test that does not pass the FULL validated stack
is CONFOUNDED: it silently runs WITHOUT flat_membase, the rlwinm/CR/vector fast-paths, `vulkan_gate_rt_update`
(+34% Burnout), `opt_prime_core_router` (+25%), `vulkan_lazy_completion_polls` (the Turnip fence fix, +46-78%),
constants-arena, etc. This invalidated a whole session of "Burnout 8.3 / BD 9.9 / GPU-paced" measurements; with
the real stack on, the Burnout RACE hits **46.2 fps** (device-validated 2026-06-25, VdSwap 462/10s, 0 faults) —
it was reported 8.3, then a STILL-partial 14.83 that was itself missing `gate_rt_update` + the GPU stack. The lesson
compounded: even a "stack" measurement is confounded unless it is the COMPLETE validated set. BD heavy field 7.9→11.67.
- **Effective-value layering (highest wins):** app launch Bundle [`XeniaOptimizations` global → `GameProfiles`
  per-game → user `--ez`] **beats** the persisted `files/xenia.config.toml` **beats** the compiled `DEFINE_bool`
  default. ONLY the launch Bundle (intent extras / `--ez`) beats the device config — the "phantom config" gotcha.
- **A cvar's compiled default can silently disagree with the registry's intended-on** (the fence fix was
  `DEFINE_bool(...,false,...)` AND pinned `=false` in the device config). When a validated win seems absent,
  check BOTH the `DEFINE_bool` default AND `run-as <pkg> grep <cvar> files/xenia.config.toml`.
- **THE TEST STACK:** every device A/B must `--ez` the full validated set — reuse the `$opts` array in
  `scratch/thor-debug/measure_const_promo.ps1`. Bisect off that, don't build up from nothing.
- **VALIDATED-SETTING PROPAGATION — do ALL of these EVERY time a game's best settings are found.** This is the
  only way the device actually gets the win; skipping step 3 is exactly what shipped a 10fps Burnout to the UI
  while `--ez` tests read 46:
  1. **Per-game win → add the cvar to that title's `GameProfiles` profile** (persists + beats the config, with the
     device-validated "why"). **Cross-game win → flip `XeniaOptimizations` `defaultEnabled=true`.**
  2. **Frame cap per game = the title's NATIVE rate** via `gpu_frame_limit_fps` in its profile (BD / Gears / Lost
     Odyssey / Banjo = 30, Burnout Revenge = 60). **Resolution per game** via `kernel_display_resolution` (BD = 720p
     — its field is GPU-bound but RESOLUTION-INVARIANT/structure-bound, so 720p costs ~nothing over 480p; sharper).
  3. **REBUILD the APK and REINSTALL it.** The registry + profiles are Java — they DO NOT reach the device until the
     APK is repackaged and installed. A stale APK silently runs the OLD defaults = the silent-default-off confound,
     ON the device. (Root cause of the 10fps Burnout: the installed APK predated the default-on + fence-fix commits.)
  4. **VERIFY from the in-app UI launch (NOT `--ez`):** pick the game in-app and confirm it reproduces the measured
     fps. If menus run uncapped (hundreds of fps) the registry/profile is NOT being applied → the install is stale.
- **`--ez`/`--es` CVAR ALLOWLIST — a NEW cvar will NOT reach C++ via an intent extra until you add it to the
  `copyBooleanExtra`/`copyStringExtra`/`copyIntExtra` allowlist in `EmulatorActivity.java onCreate` (~line 129+).**
  An un-allowlisted `--ez my_new_cvar true` is silently dropped → the cvar stays default → you measure the WRONG
  thing (cost me a full validation cycle: the LLVM object cache looked inert until I saw no "enabled" log line).
  Add the line, rebuild+reinstall the APK (Java change), THEN test. Grep your enable/init log to confirm it took.
- **BUILD GOTCHA — the repo path has a space ("New project 8")** so ndk-build's `$(wildcard)` fails the native
  configure ("unknown file"). The documented `subst X:` is NOT visible to background gradle tasks; use a directory
  JUNCTION instead: `cmd /c mklink /J C:\xt "<repo>"`, then build from `C:\xt\android\android_studio_project`.
  `mergeResources` is flaky with `!directory.isDirectory` on a fresh model — run `:app:mergeGithubDebugResources`
  once in isolation to prime it, then `assembleGithubDebug`. (You do NOT need to recompile the native `.so` for a
  Java-only registry/profile change — but a stale `.cc` makes gradle try, so the junction path is required anyway.)
- **ALWAYS MEASURE temp before assuming thermal saturation** — the Thor cools to ~38°C within ~90s of idle; never
  say "thermally blocked" without reading `/sys/class/kgsl/kgsl-3d0/temp`.
- **`screencap` can grab the SECONDARY display** (post-reboot the Thor shows the launcher on a 2nd display, so a
  screenshot looks like the home screen while the game renders fine on the primary). Confirm rendering via the
  `GPU draw outcomes/frame` logcat line + `dumpsys activity activities | grep topResumedActivity`, not the shot alone.

## ⚠️ Never thrash the Thor (hard safety rule — heavy firing crashed it once)
Before ANY launch read `/sys/class/kgsl/kgsl-3d0/temp` (milli-°C) + `gpu_busy_percentage`; launch only if
temp < 50-55°C and busy low. ONE launch per cooldown; force-stop `jp.xenia.emulator.github.debug` when temp
climbs past ~70°C. The device degrades under heavy firing (boot stalls after ~6 launches) — batch fixes,
build once, fire once, fill cooldowns with device-free work. The BUILD is the primary verification.

## Device
AYN Thor, ADB `c3ca0370`. Snapdragon 8 Gen 2 (QCS8550), Android 13, ~16GB UMA. ADB at
`C:\Users\leanerdesigner\AppData\Local\Android\Sdk\platform-tools\adb.exe`. Package `jp.xenia.emulator.github.debug`.
- **CPU:** 8× ARMv9.0-A heterogeneous — 1× Cortex-X3 @3.19GHz (prime, cpu7), 4× A715/A710 @2.8GHz, 3× A510
  @2.0GHz. NEON is the ONLY SIMD (no SVE). asimddp/i8mm/bf16 = int8/bf16 matrix (heuristics only, NEVER
  guest FP32 geometry — it black-screens, [[approx-math-guest-visible-vs-heuristics]]); fphp/asimdhp = FP16
  (pixel-shader only); flagm (NZCV flags), atomics (LSE locks), rcpc.
- **GPU:** Adreno 740v2, TBDR, Vulkan 1.3, **Turnip (Mesa) = the fast+correct driver path.** On-chip GMEM =
  the EDRAM-emulation target. SINGLE graphics+compute queue. LRZ early-Z (defeated by alpha-test/discard).
  EDRAM resolves are per-pixel COMPUTE dispatches.
- **🔌 DRIVER = custom Mesa Turnip 26.0 (K11MCH1 "v26.0.0 - R8" = `v26.0.0-rc08`, Vulkan 1.4.335), now BUNDLED
  in-APK** (`assets/drivers/turnip.zip`, auto-installs first run via GpuDriverManager; commit daccaa604). The
  whole stack depends on ITS extension set (the device-enumerated **156-extension** Turnip list, NOT the ~80/113
  the old notes claimed). **⚠️⚠️ DRIVER TRAP — do NOT "update" to the newer-looking `Qualcomm Driver v840/v837`
  in K11MCH1's repo: those are the PROPRIETARY QUALCOMM BLOB (113 ext), NOT Turnip — they are MISSING ROAA,
  `dynamic_rendering_local_read`, `multisampled_render_to_single_sampled`, the whole EDRAM toolkit the reimagine
  depends on, so "the newest release" is a DOWNGRADE for us.** R8/rc08 IS the latest + validated *Turnip*; treat
  any driver swap as a deliberate, re-benchmarked change (a newer Mesa can regress/shift behavior). [[bd-edram-reimagine-and-recompiler]]
- **Vulkan extensions (Turnip 26.0, from the 2026-06-30 EDRAM research) — PRESENT:** ROAA
  (rasterization_order_attachment_access → ordered in-place programmable blend), dynamic_rendering_local_read,
  attachment_feedback_loop_layout, multisampled_render_to_single_sampled ("free MSAA"), custom_resolve,
  load_store_op_none + LAZILY_ALLOCATED/TRANSIENT_ATTACHMENT, external_memory_dma_buf, descriptor_buffer.
  **ABSENT (the real walls):** fragment_shader_interlock (→ no unified EDRAM-as-one-buffer model), shader_tile_image,
  QCOM_tile_shading/tile_memory_heap, external_memory_host, mesh shaders; + the TBDR LAW: cannot read an RT from
  an EARLIER SEPARATE render pass (GMEM evicted) — the real "subpass-merge dead for BD" cause. Audit:
  docs/research/20260620-adreno-turnip-feature-gap-audit.md.

## Build / deploy / run
- Path has spaces → `subst X: "C:\Users\leanerdesigner\Documents\New project 8\xenia-thor"` for builds.
- APK (foreground; bg tasks don't see X:): `cmd /c "X:\android\android_studio_project\gradlew.bat -p
  X:\android\android_studio_project :app:assembleGithubDebug"` (native ~1-2.5min). APK:
  `android/android_studio_project/app/build/outputs/apk/github/debug/app-github-debug.apk`. Install:
  `adb -s c3ca0370 install -r <APK>`.
- BD launch: ISO `/storage/2664-21DE/Roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`,
  turnip driver flags, `--es hid nop --es hid_nop_button_sequence '<start/a sequence>'`; heavy field
  ~120-135s after launch. Reusable scripts in `scratch/thor-debug/*.ps1`.
- Trace cvar: `--ez vulkan_trace_draw_outcomes_per_frame true` → the `GPU draw outcomes/frame` logcat line
  (rendered, gpu_frame_us [unreliable], guest_ms, cpu_* buckets, fopen[], draw composition).

## Optimization framework
Every win = a cvar-gated engine flag + a `XeniaOptimizations` registry entry (auto-appears in the in-app
Settings UI, auto-wires the cvar) + the cvar **allowlisted in `EmulatorActivity.java`** (copyBooleanExtra/
copyIntExtra/copyStringExtra — REQUIRED or `--ez/--ei/--es` silently no-op). Default-off until validated;
per-game defaults via the GameProfile system. Two tracks: CPU (codegen/JIT/locks — CPU-bound titles) + GPU
(structure/EDRAM — GPU-bound scenes). **BD's bottleneck (driver-measured 2026-07-01): ~90% fragment/draw execution; EDRAM structure ~6ms; loads/stores ~1ms. Shipped: fp10+VRS (19.8fps). Road to 30 = internal-resolution guest patches + host scale-down. The old 'structure-bound/resolution-invariant' model is VOID.** Still re-measure any NEW lever with a
single-run / same-resolution single-cvar A/B (cross-run fps is noise) before building on it.

## Game patches
`.patch.toml` (src/xenia/patcher/, in xenia-core): be8/16/32/64 writes into guest memory, matched by title_id
+ build hash ("Module Hash" in logcat). Applied in `KernelState::LoadUserModule` after Dump() and BEFORE
execution → patches take effect (JIT compiles lazily, post-patch). Author via the `xenia-thor-ghidra-game-patch`
skill. Banjo dirty-disc = a guest-side false verification (deep RE; current patch sites are wrong-target).

## Device-free testing (prefer over firing)
- **Host x64 cpu-tests** (backend-independent HIR/kernel/VFS): `MSBuild build\xenia-cpu-tests.vcxproj
  /p:Configuration="Debug Windows" /p:Platform=x64`; run `build/bin/Windows/Debug/xenia-cpu-tests.exe "FILTER_*"`
  (Catch2). Add a test = `NAME_test.cc` + a ClCompile line in the gitignored vcxproj; commit only the .cc.
- **qemu-a64** (real ARM64 backend, no device): WSL + aarch64-linux-gnu-g++ + qemu-aarch64; `make -C build
  config=debug_linux xenia-cpu-tests CXX=aarch64-linux-gnu-g++`. Recipe: [[a64-qemu-harness]].
- Mine existing captures (log-grep) instead of re-firing where possible.

## Guest RE + CPU hot-spot tooling (the BD CPU is guest-game-logic-bound)
Confound-free simpleperf at the BD field (2026-06-25, 480p): guest-JIT **48%** (the guest main
XThread running game-logic = **67%** of samples), CP worker 16.8%, kernel 7.4%, libc 5.4%. The
guest-JIT is a **hot CLUSTER, not flat** — top: `guest_8273EF74`+`8273EF84` (7.6% of ALL cpu),
`826EE7C0`+`826EE728` (4.0%), the `8270B1F8/2D8/298` cluster (3.9%), `826FF288`+`298` (2.2%). So
the BD CPU lever = RE/accelerate BD's hottest guest code (codegen quality on a 48% chunk), not the CP.
- **Find hot guest fns (confound-free, the method that works):** `simpleperf record --app <pkg> -g
  --call-graph fp -f 1000` at a confirmed field; pull `jit.map` (`cpu_perf_map_path` →
  `files/jit.map`); bin the `unknown[+host]` samples by guest fn via the map
  (`scratch/thor-debug/_bin.py`). Symbolize host C++ offline w/ the unstripped `.so` +
  `llvm-symbolizer`. %-of-samples is reliable; `gpu_frame_us` is NOT.
- **Dump a hot fn's codegen (PPC + RawHIR + OptHIR + a64 machine code) straight from the JIT:** launch
  with `--es disassemble_function_filter "8273EF74,826EE7C0-826EE800"` (allowlisted; single addrs or
  `start-end` ranges) → grep logcat `Filtered function dump`. Shows EXACTLY how the JIT lowers it
  (register round-trips, flag stores) — the codegen-inefficiency, directly. `scratch/thor-debug/dump_hot_disasm.ps1`.
- **Semantic RE (device-free):** Ghidra **12.0.4_PUBLIC** at
  `C:\Users\leanerdesigner\Documents\SteamPortableTools\toolchains\ghidra_12.0.4_PUBLIC`
  (`support\analyzeHeadless.bat`; JDK `…\toolchains\jdk-21.0.11+10`). Scripts `scratch/ghidra/scripts/*.java`;
  projects `scratch/ghidra/proj/` (banjo, sylpheed — **BD NOT imported yet**, import `scratch/blue-dragon/default.xex`
  via XEXLoaderWV). Run headless in the MAIN loop w/ LONG timeout (600000), never in a workflow subagent —
  see the `xenia-ghidra-ooda-loop` + `xenia-thor-ghidra-game-patch` skills.

## Research finding history (compressed)
Mobile BD-30 is frontier territory (no documented mobile 360 emulator runs BD's field at 30; desktop "fable
@60" = immediate-mode GPU). The 2026-07-01 measurement campaign (driver u_trace) ended the EDRAM-fusion era:
the frame is ~90% fragment execution, EDRAM structure ~6ms — see the BD-30 APPROACH section above for the
current model, shipped stack, and road to 30. Historical detail: docs/research/20260701-bd30-edram-lever-
synthesis.md + memory [[bd-edram-atomic-rop-solve]].

## Config + git rules
- Device persists `files/xenia.config.toml` which OVERRIDES compiled cvar defaults (only `--ez/--ei/--es`
  beat it). When a default seems ignored, clear it. New cvars MUST be allowlisted in EmulatorActivity.java.
- **Never fabricate** — every number from device output read THIS turn; read the screenshot before asserting
  a visual; if unmeasured say "not measured"; if confounded say "unvalidated".
- **Targeted `git add` only — never `-A`.** Never commit ISOs/keys/dumps/screenshots/config backups. Work on
  `master`; commit + push durable results. End commit messages: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` (user 2026-07-01: Fable 5 attribution).
- **Forward-only** — never `git revert`; fix forward. Skills in `.agents/skills/`; knowledge in the memory
  files (MEMORY.md index); consult Codex/Gemini for hard rearch ([[consult-hard]]).
