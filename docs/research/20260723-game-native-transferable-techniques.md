# Game-native (Box64 / DXVK / Turnip) transferable techniques → xenia-thor

The goal names "game native" explicitly. GameNative/Winlator = Box64 (x86→ARM64
dynarec) + DXVK (D3D→Vulkan) + custom Mesa Turnip. Below are the SPECIFIC
mechanisms (not "GameNative exists") with measured numbers and exactly where they
map onto xenia-thor's recompiler + native-Vulkan renderer. Sources: box86.org
"Revisiting the dynarec", Box64 CHANGELOG/USAGE, DXVK 3.0 notes + dxvk-async/
dyasync, WinNative/Banners-Turnip driver repos.

## 1. Box64 CALLRET — native CALL/RET + return prediction (>10%) → xenia's #1 CPU direction

`BOX64_DYNAREC_CALLRET`: instead of routing every guest RET through the dispatch
jump-table, use a SEMI-DIRECT NATIVE call/ret with a return-address predictor.
Levels: L1 skip the jump-table when possible; L2 add dirty/SMC-block return
handling; L3 (default) + Secondary Entry Points (SEP = enter a block partway).
Measured >10% on ARM64.
- **Maps to:** xenia's cross-function residency trap — today the LLVM guest-call
  path round-trips ctx regs + resolves the return through the entry-table/resolve
  cache at every call boundary (the residency-writeback WIN is WITHIN a function;
  ACROSS calls it spills). The committed direction is the RETURN-TRAMPOLINE
  ([[llvm-jit-backend-build]]); Box64's CALLRET is the same lever with a concrete
  >10% and a proven design: keep a host return-address stack, return NATIVELY
  (no dispatch) into the caller's continuation, fall back to dispatch only on
  SMC/mismatch. **Box64 validates the return-trampoline is worth building.**
- **Composes with the save/restore lever just landed** (cpu_inline_saverest):
  Box64's SEP = entering a block at a non-start offset, exactly the kProlog
  save-helper fallthrough entry points ([[xenonrecomp-saverest-inline-lever]]);
  inlining the save calls also removes call frames that would otherwise pollute
  the return-address stack. Do CALLRET-style native returns AFTER the trampoline.
- **Effort/where:** LLVM/a64 backend (Android) + device-validated; SMC handling
  is the hard part (BD SMC rare, but the guest-call ABI must stay correct).

## 2. Box64 FORWARD — bigger blocks past overlap (>30%) → mostly already have

`BOX64_DYNAREC_FORWARD=3`: build bigger dynablocks without stopping at block
overlaps (esp. Wine). xenia already compiles WHOLE FUNCTIONS (bigger than basic
blocks), so most of this is covered; the transferable sliver = don't terminate a
function early at an ambiguous fall-through the scanner could follow (relates to
the AOT discovery work [[aot-coverage-measured]]). Lower priority.

## 3. DXVK dyasync — placeholder-pipeline + background compile-swap → stutter/power

DXVK async/dyasync: when a new pipeline variant is needed, DON'T stall the game
to compile it — grab the closest already-compiled pipeline as a PLACEHOLDER, use
it while the correct variant builds on a BACKGROUND worker, then silently swap it
in. Plus full SPIR-V translation offloaded to workers (not the app thread).
- **Maps to:** xenia's Vulkan pipeline creation. If a first-seen state combo
  compiles the pipeline on the CP/guest-blocking path, that's a frame stall AND
  the guest thread burns waiting = a power spike. dyasync-style
  placeholder-then-swap removes the stall (smoother = faster perceived) and lets
  the guest thread proceed = lower power on the stall. **This is the GPU-side
  "faster + lower power" analogue of the AOT-primary CPU idea (don't compile on
  the hot path).**
- **Effort/where:** vulkan_pipeline_cache / the Vulkan CP (parallel-agent's area
  — coordinate). Graphics Pipeline Library (GPL) makes the placeholder cheap.

## 4. Custom Turnip — disable_gmem plumbing + Adreno-8xx GMEM fixes → driver bump

WinNative/Banners-Turnip auto-build Mesa every commit; the custom drivers carry
per-chip GMEM plumbing (freedreno_dev_info.h / tu_cmd_buffer.cc) + KGSL UBWC
gralloc-detection bypass + Adreno-8-series GMEM fixes. The Thor bundles Turnip
26.0 R8; a bump to a current build (26.1+ has A8XX GMEM fixes) could cut GPU
power / fix GMEM edge cases relevant to the EDRAM-emulation pass storms.
- **Effort/where:** rebuild Mesa (WSL /root/mesa) + re-bundle turnip.zip; DEVICE
  work to validate. Driver source now identified (Banners-Turnip / WinNative).

## Priority (game-native, given constraints)

1. **CALLRET / return-trampoline** — the biggest CPU lever (Box64 >10%), on BD's
   CPU-bound critical path; composes with the landed save/restore lever. Backend
   + device.
2. **dyasync pipeline compile** — GPU "faster + lower power"; parallel-agent area.
3. **Turnip bump** — device/driver.
All reduce hot-path work (dispatch, compile stalls, GMEM flush) = faster + lower
power. See also [[bd-turnip-crash-race-diagnosis]] (fix stability first — a run
that crashes at ~180s can't show any of these gains sustained).
