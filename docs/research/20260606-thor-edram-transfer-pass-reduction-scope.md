# EDRAM transfer / render-pass-count reduction — scoping (2026-06-06)

**Goal:** cut the per-frame render-pass count on the Adreno 740 TBDR, the runtime-confirmed
#1 GPU lever for the GPU-bound priority titles (Blue Dragon, Lost Odyssey). Each
`vkCmdBeginRenderPass`/`vkCmdEndRenderPass` on a tiler = a GMEM tile load + store; cutting
passes cuts the tile-flush thrash. This is a scoping doc, not an implementation — it
records the confirmed mechanism + ranks the candidate fixes by risk/reward so the build is
de-risked. See memories [[xenia-thor-gfxreconstruct-profiling]],
[[autonomous-blue-dragon-fullspeed]].

## Measured baseline (gfxreconstruct, BD frames 100-103 = MENU/light scene)
- **~75 render passes/frame** for only ~380 draws (~5 draws/pass = heavy fragmentation).
- **~86 compute dispatches/frame** (xenia EDRAM host-depth-store + resolve-copy compute).
- **ZERO `vkCmdResolveImage` / `vkCmdCopyImage`** — all EDRAM transfers/resolves are
  DRAW- or DISPATCH-based (not Vulkan resolve/copy ops). ~170 barriers/frame.
- (Heavy field not yet captured; pass count will be higher — re-capture with a later
  `capture_frames` range, thermal-gated, to size the win on the real workload.)

## Confirmed mechanism (`src/xenia/gpu/vulkan/vulkan_render_target_cache.cc`)
`PerformTransfersAndResolveClears` (line ~4604) does the EDRAM ownership transfers when the
guest re-binds an EDRAM tile range to a new RT config:
- Transfers to a single DEST RT are **already batched into ONE render pass** (the dest is
  the sole color/depth attachment): see the "single pass, to load / store only once"
  comment (~line 4917), `GetHostRenderTargetsRenderPass` / `GetHostRenderTargetsFramebuffer`
  with `transfer_framebuffer_render_targets[... dest_rt]`. Compatible transfer invocations
  are further merged (`current_transfer_invocations_` merge loop ~line 5195+).
- The SOURCE RT(s) are read as **sampled-image descriptors** via
  `source_vulkan_rt.GetDescriptorSetTransferSource()` (transitioned to
  `VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, e.g. line ~4720) — i.e. the source's tile
  must be **stored to memory** before it can be sampled.
- Transfer render passes use distinct **transfer formats**
  (`color_rts_use_transfer_formats`, ~line 4942), which may differ from the game's render
  formats for the same RT.
- An existing crude lever: cvar **`gpu_skip_edram_transfers`** (line ~4634) skips the
  transfers entirely (default off; "aliased EDRAM content not preserved -> may glitch").
  Use it as the **A/B speed ceiling** to bound the achievable win before building anything.

## Prerequisites / constraints (confirmed)
- xenia's Vulkan RT path uses **traditional `VkRenderPass`** (`CmdVkBeginRenderPass`), NOT
  dynamic rendering. There is **no input-attachment usage** anywhere in
  `src/xenia/gpu/vulkan/`.
- `VK_KHR_dynamic_rendering_local_read` / `rasterization_order_attachment_access` are
  **not detected or enabled** in `src/xenia/ui/vulkan/` yet (device SUPPORTS them per the
  device-capability check in [[autonomous-blue-dragon-fullspeed]], but wiring is absent).
- Transfers run in `PerformTransfersAndResolveClears`, called during RT setup **before** the
  game's render pass begins (`SubmitBarriersAndEnterRenderTargetCacheRenderPass`).

## Candidate approaches (ranked by reward/risk)

### A. Merge transfer draws into the subsequent game render pass (BEST first target)
Today each dest RT's transfer is a **separate** pass (dest tile load+store) that runs right
before the game begins its own pass on the same dest (another load+store). If the transfer
full-screen draw(s) are emitted as the **first draws inside the game's render pass** (dest
is already that pass's attachment), the separate transfer pass disappears -> one load/store
instead of two, per transferred dest, per frame. **No new Vulkan feature, no shader rewrite,
no local_read** — the source stays a sampled descriptor (it's in memory anyway).
- Win: removes a whole pass per transferred-dest (a large share of the ~75).
- Hard parts: (1) the transfer uses transfer-FORMATS that can differ from the game pass's
  formats for that attachment — merging needs format compatibility or a reinterpreting view;
  (2) control-flow: defer the transfer draws from RT-setup time into pass-begin time;
  (3) the source must not also be an attachment of that pass (alias hazard) — keep the
  separate-pass fallback for that case.
- Risk: medium (guest-visible RT correctness); fully cvar-gateable + A/B-able vs
  `gpu_skip_edram_transfers` (correctness) and the current path (speed).

### B. On-tile source read via input attachments / local_read (keeps source on-tile)
Co-bind source + dest as attachments of one pass; transfer shader reads source via
`subpassLoad` (traditional render-pass input attachment + by-region self-dependency) or
`VK_KHR_dynamic_rendering_local_read`. Same-pixel (EDRAM tile-aligned) so it is valid.
- Win: also avoids the SHADER_READ store of the source.
- Hard parts: requires either input-attachment render passes (rewrite the transfer shaders
  to `subpassLoad` + new render-pass/framebuffer with source as input attachment) OR moving
  the RT path to dynamic rendering + enabling local_read (feature wiring in ui/vulkan).
  Bigger than A and only pays off if the source's prior pass is also merged (else the source
  was already stored). **Do A first; B is the follow-on.**

### C. Batch multiple dest RTs into one MRT transfer pass
Bind several dest RTs as MRT color attachments of a single transfer pass (one load/store
cycle for all). Bounded but limited by differing dest formats/keys; lower priority than A.

## Recommended staged plan
1. **Measure the ceiling:** A/B `gpu_skip_edram_transfers` on the BD heavy field (re-capture
   gfxr heavy first) — quantifies max achievable pass/fps win. If small, stop here.
2. **Approach A (merge transfer draws into the game pass), cvar-gated default-off.** Validate
   pixel-correctness on BD + LO at matched guest_ms; measure pass-count + gpu_frame drop.
3. If A leaves source-store cost on the table, **Approach B (input attachments)** as a
   stacking follow-on.
4. Ship each as an explained `XeniaOptimizations` toggle (CLAUDE.md mission).

## Notes
- Do NOT guess on guest-visible RT semantics — every step cvar-gated + device-validated at
  matched guest_ms (MEASUREMENT RULE; [[autonomous-blue-dragon-fullspeed]]).
- This is a multi-cycle build; this doc is step 0 (scoping). Next concrete action = the
  heavy-field gfxr re-capture (step 1 ceiling measurement), thermal-gated.
