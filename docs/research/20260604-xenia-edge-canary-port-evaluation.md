# Evaluating xenia-edge (has207) + xenia-canary for useful ports into xenia-thor (2026-06-04)

Remotes added: `edge` -> has207/xenia-edge (branch `edge`), `canary` -> xenia-canary/xenia-canary
(`canary_experimental`). Our merge-base with both = `95a5c3ee2`. Since the base: canary +1593
commits, edge +2725. Our fork has heavily diverged (own a64 backend work, Vulkan binning re-arch),
so integration = targeted hand-ports / cherry-picks, NOT a merge. Evaluated via 3 parallel agents
(CPU/a64, GPU/Vulkan, Kernel/XAM/XMA) each diffing candidate commits against our tree.

## PULLED THIS SESSION (build-verified, committed)
- **`26451c354`** ports edge **`0a18453bc`** [A64] LoadV128Const crash on encodable v128 splat
  constants (we passed a compressed imm8 to oaknut movi(VReg.2D,u64) which wants the full value),
  and edge **`7eb0c7671`** [A64] UNPACK SHORT_4 lane order (rev64+ext(...,8) rotated a half;
  rev64 alone matches x64/PPC - fixes vupkd3d128 visual bugs: Halo 3/ODST/Reach/Halo 4, Nier).
- **`159be60a3`** ports edge **`adeacfa0e`** [A64] MMIO byte_swap UB + LDR+REV detection. Our a64
  JIT emits LDR+REV for byte-swapped I32 MMIO loads but the handler never set byte_swap AND the
  struct wasn't zero-init'd -> double-swap on any faulting MMIO register load. Fixed both.

## RECOMMENDED NEXT PULLS (portable, our tree is missing them)
1. **edge `c2674b19d` [Vulkan] hoist global lock around the per-draw RequestRange loop** - LOW
   risk CPU win on our IssueDraw cpu_other hotspot (vulkan_command_processor.cc ~4226 vertex loop
   + ~4248 memexport). Diff won't apply (reverts edge's residency cache we don't have); port =
   wrap our existing loop with ONE hoisted global_critical_region::Acquire(). Could be a stacking
   XeniaOptimizations toggle. **Best next pull.**
2. **canary `b15fcc73e` [Vulkan] route guest oDepth -> gl_FragDepth** - CORRECTNESS gap on our
   DEFAULT FBO path: our SPIR-V backend has NO FragDepth/kDepth handling, so guest oDepth writes
   are silently DROPPED (DXBC handles it, Vulkan doesn't). Affects depth sprites/soft particles/
   decals. MEDIUM effort (must add the depth-staging var + kDepth StoreResult + FBO gl_FragDepth +
   FSI oDepth branch our tree lacks). Foundational; pull when doing a Vulkan-correctness pass.
3. **edge `c383d049e` [CPU] AND+NOT -> AND_NOT HIR fold** - arch-agnostic, self-contained, ships
   its own tests; our a64 already lowers AND_NOT. Minor free CPU win (Lost Odyssey). Easy.
4. **edge `89b81da8d` [A64] lift JIT code-size + cond-branch range limits** - real robustness (our
   emitter is fixed 1MiB cap, no veneers) BUT invasive (AutoGrow buffer relocation + cond-branch
   veneers touch our code-cache copy path). DEFER until a priority title actually trips the cap.
5. Audio stability (small, plug real gaps in our older APU baseline): canary **`1662c7570`**
   (client-unregister loop in AudioSystem::Shutdown - our Shutdown is bare) + **`64c59d3d0`**
   (submit silence on invalid client index, avoid semaphore leak). audio_system.cc halves only;
   adapt to our SubmitFrame(samples_ptr) signature.

## ALREADY-PRESENT (our independent equivalents - do NOT pull)
a64: `9f0ef3123` helper RESERVED_LOAD/STORE, `904c6c8b1` LSE label fix, `d2c8ca675` bitmask imms,
`59f9fb996` lazy FPCR, `84b05bd20` conditional denorm flush, RSQRT/V128-shift opts. XAM locale:
`4c396fe61`/`072ef7eff` (XamGetLanguageLocaleFallbackString/Typeface/OnlineLanguageAndCountry are
in xam_locale.cc + xam_table.inc 0x589-0x58D).

## NOT APPLICABLE / DEFER (code paths we don't have)
- canary `9478cda5d` "titles not booting (title id)" - fixes a frame_limiter_worker we don't have;
  does NOT address our non-booting titles (Sylpheed phys-mem / BTTF network / Infinite Undiscovery
  black-stall are unrelated root causes).
- GPU: ZPD occlusion queries (`fbd620c22`+`73945c06d`) - huge, and ANTI-priority (BD/LO benefit
  from DISABLING occlusion on Adreno TBDR). FSI dynamic-rendering fix, resolve_downscale fixes -
  target paths we don't run. `b98880856` decal polygon offset stacks on b15fcc73e (defer).
- XMA: the whole "XmaContextNew" cluster targets canary's pluggable decoder family
  (xma_context_new.cc) we DON'T have - not line-portable, and the headline AC6-RexGlue one is a
  CORRECTNESS fix (cinematic audio gaps) NOT a CPU-cost reduction, so it does NOT help our
  "XMA Decoder ~32% CPU" hotspot. A full APU-family port is a separate large effort.

## BANJO dirty-disc - explicit verdict: NONE of these ports affect it.
Trigger is NtQueryFullAttributesFile(GAME:\loctext\englishus\) -> NO_SUCH_FILE = ISO/GDFX path
resolution. edge `7e2412a79` "STFS language slot" is STFS CONTENT-PACKAGE metadata, not GDFX
directory lookup. XAM language exports only change the reported language ID, not which on-disc dir
the guest opens. Banjo bypass stays a guest-side game-patch problem.

## Minor real gaps noted (low priority): our XamGetLanguage (0x3D2) + XamUserGetOnlineLanguageFrom-
XUID (0x229) ordinals are in xam_table.inc but have NO _entry impl (canary `6a380bcd9`/`4d7b30e84`
would fill them; wire only if a title needs non-English system language).
