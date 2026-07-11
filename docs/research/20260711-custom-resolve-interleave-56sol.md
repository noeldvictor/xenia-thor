Decisive recommendation: proceed with (a), using (a-i) only as the correctness bring-up. Then make the real implementation “persistent intermediate segments + one resolve at the guest publication edge.” Do not pivot to captured/replayed field draws yet.

Interleaving destroys the zero-spill premise, but it does not necessarily destroy the win. The new goal is to replace the large EDRAM transfer graph with the minimum native spill/reload pairs required by genuine dependencies.

## Q1: Correct architecture

Use three compatible render-pass variants per native surface generation:

| Segment type | MSAA color | Depth | A2B10 output |
|---|---|---|---|
| First/intermediate | Guest-correct initial op, `STORE` | Guest-correct initial op, `STORE` if reused | No convert; discard |
| Reentered intermediate | `LOAD` / `STORE` | `LOAD` / `STORE` | No convert; discard |
| Terminal/publication | `LOAD` / `DONT_CARE` | Independent lifetime decision | Full custom-resolve draw, `STORE` |

Keep the same two-subpass attachment/reference structure for compatibility. On intermediate closes, still call `NextSubpass` so the pass ends legally, but issue no convert draw and do not publish att1. On the terminal close, issue the fullscreen convert.

Load/store ops and initial/final layouts do not affect Vulkan render-pass compatibility, so these variants can reuse the same framebuffer and graphics pipelines as long as formats, samples, attachment references, and subpass structure match. [Vulkan render-pass compatibility and attachment operations](https://docs.vulkan.org/spec/latest/chapters/renderpass.html)

Two cautions:

- “First use = DONT_CARE” is valid only if the new generation is actually initialized or completely overwritten before any blend/depth-dependent draw. Otherwise seed or `LOAD`.

There is no generally safe draw reordering. A possible later optimization is an order-preserving “super render pass” containing A and B as separate attachments, with A preserved across B subpasses. That avoids reordering, but only works if:

- no intervening compute/copy/non-local texture dependency requires leaving the pass;
- all attachment extents and layouts can be represented coherently;
- Turnip can fit the combined live attachments in GMEM without shrinking tiles enough to hurt the foliage workload.

Dynamic-rendering suspend/resume is not an escape hatch: Vulkan forbids another render-pass instance between suspend and resume. [VK_KHR_dynamic_rendering suspend/resume rules](https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_dynamic_rendering.html)

Option (d) is not inherently better. Merely rendering to a private image still has the same A→B pass breaks. Capturing and replaying contiguously requires snapshotting/versioning descriptors, constants, textures, vertex data, depth, queries, and any interleaved RT dependencies—in other words, an HLE framegraph.

## Q2: Terminal detection

Do not predict the future or use end-of-frame.


- `copy_src_select`;
- source RT key;
- destination base/format/dimensions;
- `copy_sample_select`;
- exponent bias and swap;
- color/depth clear flags.

Arm a `publication_pending` record before `Resolve()` causes `DumpRenderTargets`/barriers to end the producer pass.

Then:

- If the matching source pass is still open, patch its deferred begin to the terminal-compatible render pass, run the custom resolve, and publish att1.
- If the source was already stored because another RT intervened, open a resolve-only pass that `LOAD`s the stored MSAA source, advances to the CR subpass, converts, and discards the MSAA afterward.
- If a title samples an RT without an explicit resolve, first-consumer lazy publication is a valid fail-safe. It may require that resolve-only load.
- Never use “last before swap/frame end” as the correctness rule.

Associate validity with `(source RT key, generation, destination resource)`, and only redirect consumers/drop the LLE path after the terminal draw and legal pass end succeed.

## Q3: Is convert-every-segment correct?

Yes, under these invariants:

- att0 is valid before every `LOAD`;
- every preceding segment used `STORE`;
- depth/stencil persistence matches guest behavior;
- A and B are genuinely distinct logical resources, not overlapping EDRAM views whose writes must alias;
- the convert covers every output pixel that may be sampled.

The last point matters because custom-resolve color/depth outputs become undefined at the start of the shader-resolve subpass. A partial conversion cannot preserve untouched att1 pixels. [VkSubpassDescription custom-resolve semantics](https://docs.vulkan.org/refpages/latest/refpages/source/VkSubpassDescription.html)

So your accumulated-MSAA reasoning is sound: the final conversion sees all stored-and-reloaded samples.

However, convert-every-segment is not “free on-tile.” Turnip keeps the MSAA input in GMEM, but marks the custom-resolve output non-GMEM, allowing the shader to write it directly to system memory. Thus every redundant conversion performs a full-surface fragment pass and another A2B10 materialization. [Turnip `tu_pass.cc`](https://chromium.googlesource.com/external/gitlab.freedesktop.org/mesa/mesa/+/bc649a02d4c1ca2148ba6eb2e0a83fa774b4d037/src/freedreno/vulkan/tu_pass.cc), [custom-resolve rationale](https://docs.vulkan.org/features/latest/features/proposals/VK_EXT_custom_resolve.html)

Use a-i to prove complete pixels. Then remove the redundant conversions.


## Q4: Performance verdict

My prior is that terminal-only (a) is still worth pursuing and has a credible chance of being net-positive, but it is not measured.

The uncompressed traffic model per intermediate boundary is:

```text
MSAA color store+load = 2 × width × height × samples × 8 bytes
32-bit depth store+load = 2 × width × height × samples × 4 bytes
each extra A2B10 convert = width × height × 4 bytes
```

The half-width/four-sample surface therefore has the same raw MSAA color footprint as the full-width/two-sample surface. Actual bus traffic may be lower with UBWC, but the relative model remains useful.

The important comparison is not “spill versus no spill.” It is:

```text
minimum interleave-boundary native spills
+ one publication conversion
versus
the reported EDRAM ownership-transfer/resolve graph
```

That is favorable enough to implement and measure. But adding `LOAD/STORE` may change Turnip’s GMEM-versus-sysmem autotune decision; verify the producer still uses GMEM rather than assuming it. Turnip explicitly chooses between these modes based partly on estimated attachment load/store bandwidth. [Freedreno/Turnip architecture and autotuning](https://docs.mesa3d.org/drivers/freedreno.html)

So the path is:

1. Make a-i correct: persistent color/depth, complete output, validation clean.
2. Bind terminal conversion to `IssueCopy`/the resolve edge.
3. Eliminate redundant att1 writes and, using deferred begin patching, the terminal att0 store.
4. Measure that terminal-only variant against the same-scene LLE baseline.
5. If it still loses, pursue order-preserving multi-attachment subpass fusion. Treat captured/replayed private rendering as the strategic HLE-framegraph route, not the next tactical patch.

Per the repo’s evidence discipline, this is a design verdict—not a Thor speed claim.
233,349
Decisive recommendation: proceed with (a), using (a-i) only as the correctness bring-up. Then make the real implementation “persistent intermediate segments + one resolve at the guest publication edge.” Do not pivot to captured/replayed field draws yet.

Interleaving destroys the zero-spill premise, but it does not necessarily destroy the win. The new goal is to replace the large EDRAM transfer graph with the minimum native spill/reload pairs required by genuine dependencies.

## Q1: Correct architecture

Use three compatible render-pass variants per native surface generation:

| Segment type | MSAA color | Depth | A2B10 output |
|---|---|---|---|
| First/intermediate | Guest-correct initial op, `STORE` | Guest-correct initial op, `STORE` if reused | No convert; discard |
| Reentered intermediate | `LOAD` / `STORE` | `LOAD` / `STORE` | No convert; discard |
| Terminal/publication | `LOAD` / `DONT_CARE` | Independent lifetime decision | Full custom-resolve draw, `STORE` |

Keep the same two-subpass attachment/reference structure for compatibility. On intermediate closes, still call `NextSubpass` so the pass ends legally, but issue no convert draw and do not publish att1. On the terminal close, issue the fullscreen convert.

Load/store ops and initial/final layouts do not affect Vulkan render-pass compatibility, so these variants can reuse the same framebuffer and graphics pipelines as long as formats, samples, attachment references, and subpass structure match. [Vulkan render-pass compatibility and attachment operations](https://docs.vulkan.org/spec/latest/chapters/renderpass.html)

Two cautions:

- “First use = DONT_CARE” is valid only if the new generation is actually initialized or completely overwritten before any blend/depth-dependent draw. Otherwise seed or `LOAD`.

There is no generally safe draw reordering. A possible later optimization is an order-preserving “super render pass” containing A and B as separate attachments, with A preserved across B subpasses. That avoids reordering, but only works if:

- no intervening compute/copy/non-local texture dependency requires leaving the pass;
- all attachment extents and layouts can be represented coherently;
- Turnip can fit the combined live attachments in GMEM without shrinking tiles enough to hurt the foliage workload.

Dynamic-rendering suspend/resume is not an escape hatch: Vulkan forbids another render-pass instance between suspend and resume. [VK_KHR_dynamic_rendering suspend/resume rules](https://docs.vulkan.org/features/latest/features/proposals/VK_KHR_dynamic_rendering.html)

Option (d) is not inherently better. Merely rendering to a private image still has the same A→B pass breaks. Capturing and replaying contiguously requires snapshotting/versioning descriptors, constants, textures, vertex data, depth, queries, and any interleaved RT dependencies—in other words, an HLE framegraph.

## Q2: Terminal detection

Do not predict the future or use end-of-frame.


- `copy_src_select`;
- source RT key;
- destination base/format/dimensions;
- `copy_sample_select`;
