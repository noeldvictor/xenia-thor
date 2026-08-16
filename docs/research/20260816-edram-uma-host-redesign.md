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

## 3. ❌❌ RETRACTED SAME DAY: "SPURIOUS TRANSFERS FROM A CONSERVATIVE EXTENT" IS **NOT** OUR ROOT CAUSE
**I wrote section 3 below from the author's 2021 article, then read our code, and it refutes it. Correcting
in place rather than deleting, because the distinction is the useful part.**
**⚠ TWO DIFFERENT NUMBERS WERE CONFLATED - by me, and it is an easy mistake to repeat:**
| | what it is | value on BD | narrowed by the estimator? |
|---|---|---|---|
| `GetRenderTargetHeight()` | the **ALLOCATION** - how tall the host image is | EDRAM-span, e.g. **2048**, up to 8192 | **NO.** Pure `kEdramTileCount / pitch` arithmetic |
| `height_used` | the **OWNERSHIP EXTENT** - what range is claimed | `min(above, EstimateMaxY())` | **YES, ALREADY** |
`render_target_cache.cc:853`:
```cpp
uint32_t height_used = std::min(
    GetRenderTargetHeight(pitch_tiles_at_32bpp, msaa_samples),
    draw_extent_estimator_.EstimateMaxY(..., vertex_shader));
```
**`EstimateMaxY` bounds by the SCISSOR** (`draw_extent_estimator.cc:491`), and only falls back to CPU vertex
processing for `clip_disable` draws. **So ownership is ALREADY scissor-narrowed. The proposed "Stage 1" is
already implemented, and has been all along.**
**⇒ THE 45 TRANSFERS ARE NOT AN ARTEFACT OF A LOOSE ESTIMATE.** They are the guest genuinely re-binding
overlapping EDRAM with different keys - 24 at the same format (differing in base/pitch/msaa/depth) and 14 at
a different format. **BD really does ping-pong its render targets: `pass_break_rt_change=27` per frame.**
**⇒ SO THE FIX IS NOT "ESTIMATE BETTER". It is "make the transfer not cost a pass break", which is Stage 2 -
and Stage 2 already exists as a lever.**

## 3b. THE ORIGINAL (NOW-REFUTED) REASONING, KEPT AS A WARNING
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

### ~~Stage 1 — kill the spurious transfers~~ ❌ **DELETED. ALREADY IMPLEMENTED.** See section 3.
`height_used` is already `min(GetRenderTargetHeight(), EstimateMaxY())`, and `EstimateMaxY` is
scissor-bounded. There is no loose extent to tighten. **The 45 transfers are real guest behaviour.**

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
| ~~spurious transfers come from conservative extent estimation~~ | ❌ **REFUTED BY OUR OWN CODE, same day.** `height_used` is already scissor-narrowed via `EstimateMaxY`. The idea came from the 2021 article and does not describe this tree |
| ~~narrowing the estimate removes a large share of the 45~~ | ❌ **DEAD.** There is nothing left to narrow |
| the 45 transfers are genuine guest RT re-binding | **INFERRED** from the extent already being tight, plus `pass_break_rt_change=27`. Consistent, not proven |
| the between-pass 17% is dominated by these transfers | **PLAUSIBLE, UNMEASURED.** The instrument was broken until today |
**⇒ NEXT ACTION: re-run `tools/thor/bd_direct_host_resolve_ab.sh` on a build with the logging fix, and read
`between_us` against `rt_transfers`. That single number decides whether Stage 1 is worth building.**


---

# ADDENDUM (same day): THE HEAVY PASSES, IDENTIFIED

Everything above argues about EDRAM within a 10.4% budget. Two measurements landed after it was written and
they move the target.

## 1. The control: Burnout vs BD, same build, driver and device
| | BURNOUT (race) | BD (gameplay) |
|---|---|---|
| total_vertices | **862,632** | 159,324 |
| frame | **16,900 us (59.2 fps)** | 30,125 us (33.2 fps) |
| in-pass | **11,801 us** | **23,487 us** |
| between-pass | 5,099 us | 6,638 us |
| rt_transfers | 9 | **45** |
| pass_break_barrier | 1 | **18** |
**BD costs ~10x per vertex (189.1 vs 19.6 us/1000 verts).** It churns EDRAM 5x harder - that half of the
hypothesis is confirmed - but between-pass time differs by only 1.3x while in-pass differs by 2.0x at one
fifth the geometry. **The whole gap is inside the passes.**

## 2. What those passes are
`MaybeLogSmallGuestPass` carried the answer and never printed it: it needs `gpu_trace_resolve_timing`, and it
returned early for any pass with many draws, so the heavy ones were excluded by design. Fixed in `69f11fb9b`
(tagged BIGPASS).
```
GPU pass split: n=94  pass_us=59,666  top_pass_us=[25,796  22,844  2,097]
    -> TWO passes of 94 are 81.5% of in-pass time
```
All the heavy shaders share one framebuffer, `fb=e20d`, 720x1824, with **blending enabled**
(`blendctl0=07060706`), RGB-only colour mask, depth test on, and no shader depth write - at **671, 220 and 219
draws**.
**⇒ BD's frame is hundreds of ALPHA-BLENDED depth-tested draws stacked into one target. That is an overdraw
profile, and it explains the 10x per-vertex cost far better than transfers, attachment size or bandwidth.**

## 3. What this does to the design above
- **Stage 2 (in-pass transfers) and Stage 3 (UMA resolve) stay valid but stay SMALL** - they live in the
  between-pass bucket, which the control shows is only 1.3x Burnout's.
- **The redesign is not where the 2x is.** If one exists it is in the 81.5%, i.e. in fragment work.
- **The next measurement is `lrz_disable_reason` on `fb=e20d`** - if the depth test cannot early-reject those
  blended layers, that is the mechanism, and it is the first mechanical link from EDRAM emulation to the 81.5%.
  Prior: `gpu_lrz_spike_depth_clear` measured +13.1% WORSE and looked broken, so LRZ has been poked once and
  failed - but nobody has ever asked the driver WHY it is off.
