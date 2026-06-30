# CLAUDE.md — xenia-thor (Xbox 360 emulator on the AYN Thor)

## Goal
Make Xbox 360 games fast + playable on the AYN Thor (Snapdragon 8 Gen 2 / Adreno 740) via this xenia fork.
**TARGET = FULL-SPEED Xbox 360 emulation. Blue Dragon → locked 30fps at 720p with FULL default foliage**
(the 360 ran it at 30; the Thor is 10-20× more powerful + the emulation needs only ~4× the 360 ⇒ full speed
has large margin — see the ~4× budget below). Then Burnout, Gears, Lost Odyssey, Banjo → 30-60. Ship every win as a cvar-gated, stacking `XeniaOptimizations` toggle. **Active focus (user, 2026-06-27):
improve BD steady-state perf — smoother, LOWER WATTAGE + HEAT (not boot time) — via (1) ⭐ AOT LLVM = THE CORE
DIRECTION (user 2026-06-29): the ReXGlue / XenonRecomp model — STATIC AHEAD-OF-TIME recompilation, precompile
the whole title to native, NO JIT/dispatch at gameplay (opcode coverage is already 100%; the work now is AOT +
residency + direct calls — see the AOT bullet below); (2) UMA zero-copy [FIXED + enabled default-on 2026-06-27,
present-hang gone]; (3) CPU/GPU NEON [VMX→NEON, FP32 dot=fmul+faddv]; (4) TURNIP — the Mesa/Adreno Vulkan driver
(the fast+correct path; the KGSL blocking-fence fix was the project's biggest win; lever = driver-level perf +
Adreno features). Fix LLVM bugs as found.**

### 🔑🔑 BD-30 APPROACH — read before any perf work (user, 2026-06-28)
- **THE THOR IS 10-20× MORE POWERFUL THAN THE XBOX 360 → full-speed emulation IS the target + achievable.**
  BD ran ~30fps on the 360. **The emulation realistically needs only ~4× the 360's work (EDRAM handling +
  CPU recompilation/LLVM) — well within the 10-20× headroom (≈4×/15× ⇒ ~100fps-class).** So 30fps at full
  foliage has LARGE margin. BD at ~5-8fps = a ~15-58× EMULATION INEFFICIENCY to CLOSE, NOT a hardware/foliage
  limit. NEVER conclude "can't hit 30 / GPU-capped / content too heavy / needs foliage thinning" — find the
  emulation inefficiency. The job = drive emulation overhead DOWN toward the ~4× budget.
- **✅ BD FIELD = STRUCTURE-BOUND, not pixel-bound (CLEAN same-resolution isolation 2026-06-29 — supersedes the
  earlier CONFOUNDED cross-run "24ms real + 79ms tile-I/O" decomposition).** The field is GPU-bound (CPU does
  ~3ms then waits ~127ms on the GPU fence; 99% busy) AND **resolution-INVARIANT: 640px (480p) and 1280px (720p)
  give IDENTICAL gpu_frame_us (~122ms)** ⇒ fragments / fill / overdraw / resolution / foliage-COVERAGE are all
  FREE. Same-res single-cvar isolation: **`gpu_edram_passes_dont_care` (skip per-pass tile load/store) → 122→49ms,
  so the per-pass GMEM tile-RESOLVE ≈ 74ms (60%); residual ≈49ms = binning (260k verts) + 1083 draws + 46
  transfers.** The 74ms is **per-pass LATENCY, not bandwidth** (resolution-invariant; image+framebuffer clamp =
  ~10% only) = ~42 render-to-texture passes × ~1.7ms FIXED resolve each. **Those 42 are BD's FIXED post-process
  pipeline (bloom/blur/shadow/tonemap) that runs EVERY frame regardless of scene** — a sparse village is still
  99% GPU because it pays the full pipeline. On the 360 these resolves were free (on-die EDRAM); the Adreno pays.
- **BD-30 LEVERS (revised 2026-06-29):** (1) ⛔ the input-attachment / subpass MERGE is DEAD — proven 4 ways
  (25/26 composites read a producer rendered several passes earlier = non-adjacent, can't stay GMEM-resident on a
  TBDR; the 1 adjacent is in-place needing FSI/ROAV, ABSENT on Adreno 740). DON'T rebuild it. (2) ✅ image-alloc
  clamp SHIPPED (`gpu_clamp_rt_image_height=768`, ~10%, f1836c559 — Turnip's storeOp follows the IMAGE not the
  renderArea, so the old framebuffer clamp missed it). (3) ⭐ **USER-APPROVED 2026-06-29 ("lower bloom/blur is
  acceptable") → THE big lever = REDUCE THE 42 POST-PROCESS PASSES.** Even gutting all post-process → ~15-20fps
  (the DONT_CARE floor); locked-30 needs post-process reduction AND residual (binning/draw) cuts = a STACK.
  FOLIAGE STAYS FULL (coverage is free — thinning is pointless AND forbidden). (4) residual = binning/draws.
- **⛔ REFUTED dead-ends (device-proven CLEAN this session, DON'T re-chase): LRZ restore** (spike flat WITH the
  now-functional opaque prepass — BD foliage is co-planar + 34% blended, nothing to occlude); **VRS** (fragments
  free ⇒ its "−22%" was a scene confound; likely inert — re-verify or drop from BD's profile); resolution / SGSR /
  render-low (pixels free); hw-vertex-fetch, FDM, UBWC, sysmem, renderArea-clamp, dont_care_safe, depth-store-skip,
  input-attachment merge, foliage thinning. Full evidence: [[bd-resolution-invariant-structure-bound]] [[bd-edram-tile-io-bottleneck]].
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

## Autonomous mode (standing user directive)
Pick the highest-value unit yourself and execute end-to-end (implement → build-verify → device-test →
commit → next). Don't ask which task / re-confirm direction / analysis-paralyze. A big effort is a reason
to start, not to ask. Surface only genuine external blockers. The thermal + no-fabrication rules below are
safety, not "asking" — they always hold.

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
- **GPU:** Adreno 740v2, TBDR, Vulkan 1.3, **Turnip (Mesa)** = the fast+correct driver path. On-chip GMEM =
  the EDRAM-emulation target. SINGLE graphics+compute queue. LRZ early-Z (defeated by alpha-test/discard).
  EDRAM resolves are per-pixel COMPUTE dispatches. Turnip exposes ~80 extensions, most unused (audit doc
  docs/research/20260620-adreno-turnip-feature-gap-audit.md). Absent: fragment_shader_interlock,
  external_memory_host, mesh shaders.

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
(structure/EDRAM — GPU-bound scenes). **BD's bottleneck IS RESOLVED (clean 2026-06-29): the field is GPU-bound +
STRUCTURE-bound (resolution-invariant) — ~74ms per-pass tile-resolve latency × 42 fixed post-process passes +
~49ms binning/draws. The BD-30 lever is post-process-pass reduction (user-approved lower bloom) + residual cuts,
NOT pixels/foliage/VRS. See [[bd-resolution-invariant-structure-bound]].** Still re-measure any NEW lever with a
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

## Config + git rules
- Device persists `files/xenia.config.toml` which OVERRIDES compiled cvar defaults (only `--ez/--ei/--es`
  beat it). When a default seems ignored, clear it. New cvars MUST be allowlisted in EmulatorActivity.java.
- **Never fabricate** — every number from device output read THIS turn; read the screenshot before asserting
  a visual; if unmeasured say "not measured"; if confounded say "unvalidated".
- **Targeted `git add` only — never `-A`.** Never commit ISOs/keys/dumps/screenshots/config backups. Work on
  `master`; commit + push durable results. End commit messages: `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`.
- **Forward-only** — never `git revert`; fix forward. Skills in `.agents/skills/`; knowledge in the memory
  files (MEMORY.md index); consult Codex/Gemini for hard rearch ([[consult-hard]]).
