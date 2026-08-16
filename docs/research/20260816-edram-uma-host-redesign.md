# Rethinking Xenia's EDRAM onto a UMA host — the design, and what the device says

2026-08-16. User ask: *"really rethink how xenia does its edram, into our uma/host system and keep 100%
compatibility but superspeed."* Scoping answers the same day: **never regress a game**, **all three
mechanisms**, **harness first**, **Vulkan-only**.

**Read `EDRAM / GMEM / UMA: ANSWERED ON THE DEVICE` in CLAUDE.md first.** This document only covers what that
one leaves open.

---

## 1. WHAT THE DEVICE ALREADY RULED OUT TODAY

Measured on the Thor, Turnip/Adreno 740, at the representative shape (1280x2048 + depth + up to 256 draws):
| hypothesis | result |
|---|---|
| framebuffer bandwidth is the constraint | **NO.** Blend free; 2x bytes/pixel = +8-10%; flat across a 36x working set (0.9 MB to 66 MB), no cache cliff |
| the EDRAM-span attachment shape is the cost | **NO.** +1.5% autotune, +1.1% forced-bin |
| GMEM/tiling can win | **NO.** +157% worse at 1 draw, exactly neutral at 16/64/256. Best case is parity |
**⇒ Any design whose payoff is "fewer framebuffer bytes" is dead on arrival here. That kills the obvious
"map EDRAM onto GMEM" reading, and it kills transient/LAZILY_ALLOCATED attachments and subpass merging as
PERFORMANCE plays** - ARM measures those at 45% fewer reads / 56% fewer writes, which is a large saving of a
resource we have spare.

## 2. WHAT IS ACTUALLY EXPENSIVE — MEASURED, 159 GAMEPLAY FRAMES
| counter | per frame |
|---|---|
| `rt_transfers` (EDRAM ownership transfers) | **45** |
| `rt_transfer_calls` | 25 |
| `pass_break_rt_change` | **27** |
| `pass_break_barrier` | 18 |
| **`xfer_same_fmt`** (transfer with NO format change) | **24** |
| `xfer_diff_fmt` | 14 |
| total passes/frame | ~74-77 |
**🔑 A THIRD OF BD's RENDER PASSES EXIST ONLY TO SERVICE EDRAM OWNERSHIP TRANSFERS. And 24 of the 45
transfers do not change format at all** - they are moves, not reinterpretations.
**⇒ THIS is the EDRAM cost on this device. Not tiling, not bandwidth, not attachment size.**

## 3. THE ROOT CAUSE, FROM XENIA'S OWN AUTHOR
Triang3l, *Leaving No Pixel Behind* (xenia.jp, 2021-04-27), on why transfers are generated:
> The height **is not even passed** when drawing regular world geometry. The Xbox 360 does not require a
> render target height, only width. Xenia must conservatively estimate the affected region from viewport and
> scissor. **For viewport-less draws - common in screen-space operations - the emulator pessimistically
> assumes the entire remaining eDRAM could be affected**, forcing it to consider the full 10 MB modified in
> the worst case, **creating spurious ownership transfers.**
**⇒ The transfers are not intrinsic to the guest's behaviour. A large share are ARTEFACTS OF A CONSERVATIVE
ESTIMATE.** That is consistent with `xfer_same_fmt=24`: a same-format transfer is a pure move, which is what
a spurious "this range might have been touched" conclusion produces.

## 4. PRIOR ART, AND WHAT TRANSFERS FROM IT
| source | idea | does it apply here? |
|---|---|---|
| **Dolphin, EFB copies** | "EFB to RAM" is accurate and slow; "EFB to Texture" is fast. Deferred/batched EFB copies took Xenoblade **62 -> 156 fps** (PR 7539) | **NO, and this is worth knowing.** The 2.5x is on the EFB2RAM path. Our equivalent, `vulkan_readback_resolve`, is **default OFF** ("very slow, for bring-up parity with D3D12 only"). **We are already on the fast side of that trade.** Do not expect Dolphin's win |
| **Dolphin, batching at guest sync points** | queue copies, flush at DrawDone/PE token, discard superseded ones | **Partially.** The "discard superseded" idea maps onto ownership transfers: a transfer whose destination is fully overwritten before it is read is dead work |
| **PCSX2, GS texture cache** | page-granular tracking: one list per memory page (512), MRU lookup, handles format/size reinterpretation of the same memory | **YES - for the identity half.** This is the battle-tested form of "EDRAM as a tag namespace, not geometry" |
| **ARM, Vulkan multipass** | transient attachments + subpass merging: 45% fewer reads, 56% fewer writes | **NO as a perf play** - bandwidth is not our constraint. Keep in mind only if a future title is bandwidth-bound |

## 5. THE DESIGN
**Principle: EDRAM is an IDENTITY namespace, not a geometry source, and not a memory to simulate.**
Its job is to answer "is this the same surface the guest wrote before?". It should never dictate host image
dimensions, and it should never force a copy that the guest's actual writes do not require.

### Stage 1 — kill the spurious transfers (the measured win)
**Narrow the conservative extent, never widen it.** We already compute real draw extents:
`DrawExtentEstimator` exists in this tree (it appears in the draw-outcomes log with its own bail taxonomy).
Feed the *actual* written extent into ownership tracking instead of "the rest of EDRAM".
- **100% compatible by construction:** the fast path only ever REPLACES a conservative over-estimate with a
  measured, provably-not-smaller one. When the estimator bails - and it has an explicit bail taxonomy for
  exactly this - fall back to today's conservative behaviour. A game can lose performance, never pixels.
- **Target:** the 24 same-format transfers/frame. A same-format move between ranges that the guest never
  actually aliased is pure loss.

### Stage 2 — make the surviving transfers free of pass breaks
`gpu_vulkan_inpass_edram_transfers` already executes transfers inside the guest pass. It measured FLAT on BD
because BD's 7e3-float transfers are excluded on NaN-exactness grounds. The open lever is already named in
the ledger: **`gpu_bd_inpass_float_transfers`**. 27 of ~75 passes per frame are RT-change breaks.

### Stage 3 — the UMA half
`vulkan_direct_host_resolve` compute-resolves the host render target straight into guest memory, with no
EDRAM staging and no round trip. On UMA the destination is memory the GPU already addresses.
**Status: ported, default OFF, XenDroid ships it ON.** First A/B attempted 2026-08-16; fps identical, and the
between-pass bucket was unreadable because of a logcat truncation bug (now fixed, `23f89c276`). **Re-run.**

### What is deliberately NOT in this design
- Screen-sized render target allocation. **Measured 1.5%.** Not worth the compatibility risk.
- Anything targeting framebuffer bandwidth: transient attachments, subpass merging, GMEM residency,
  tile-memory heaps. **Measured to be a resource we are not short of.**

## 6. HONESTY ABOUT WHAT IS AND IS NOT ESTABLISHED
| claim | basis |
|---|---|
| 45 transfers / 27 RT-change pass breaks / 24 same-format per gameplay frame | **MEASURED**, 159 frames, this device |
| bandwidth, attachment shape and GMEM are all dead ends | **MEASURED** today |
| Dolphin's 2.5x does not transfer to us | **VERIFIED IN CODE** - `vulkan_readback_resolve` is default off |
| spurious transfers come from conservative extent estimation | **AUTHOR'S STATEMENT + consistent with `xfer_same_fmt=24`.** NOT yet proven on this device |
| narrowing the estimate will remove a large share of the 45 | **HYPOTHESIS.** The next measurement, not a conclusion |
| the between-pass 17% is dominated by these transfers | **PLAUSIBLE, UNMEASURED.** The instrument was broken until today |
**⇒ NEXT ACTION: re-run `tools/thor/bd_direct_host_resolve_ab.sh` on a build with the logging fix, and read
`between_us` against `rt_transfers`. That single number decides whether Stage 1 is worth building.**
