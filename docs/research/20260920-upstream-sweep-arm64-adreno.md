# Upstream sweep 2026-09-20: ARM64 and Adreno 740 performance

Question: did any upstream source gain ARM64 or Adreno 740 performance work since the 2026-09-18 sweep?
Method: fetch every tracked remote, count new commits from the sweep marks, read the ones that touch
the a64 backend or the GPU path, and search online for sources outside the tracked set.

## Tracked sources

| source | new since mark | relevant to ARM64 or Adreno |
|---|---|---|
| xenia-edge `edge/edge` | 21 commits (2026-09-18 to 09-20), tip `f6184518b4` | 1 a64 commit, part of a series; 3 standalone CPU fixes |
| xenia-canary | 0 | |
| XenDroid main, author rfandango | 0 (tip still 2026-08-20) | |
| xenia-project master | 0 | |
| Wunkolo, IhorHarayda forks | 0 | |

## Online

| found | verdict |
|---|---|
| Banners-Turnip releases up to `v26.3.0-20260919` | Our bundled driver is `26.3.0-20260807-r11`, 43 days old. Mesa has 167 turnip, ir3, and freedreno commits in that window. See below. |
| WinDroidEmulation/X360-Mobile | A release-manifest repository with no emulator source. 3 stars. Not a source. |
| edilsongg/xenia-arm64 | A 2024 fork of Wunkolo. Last commit 2023-08-04. Dead. |
| XenDroid press coverage (2026-08-19) | Describes the XenDroid release we already track. No new code. |

## Edge: the user-mode series

18 of the 21 commits implement the kernel's user mode for XNA titles (.NET Compact Framework):
a second address space, `KeEnterUserMode` and `KeLeaveUserMode`, `sc` trapping to a guest handler,
user code on its own host fibers, icache sweep invalidation, and a guest page table. The a64 commit
`1e640ba491` is the backend half: per-host-stack stackpoint records, a dynamic call cache for
targets with no indirection slot, and returns that land in a live frame of dynamic code.

Verdict: compatibility work for XNA indie titles, not performance. The a64 half changes ResolveFunction
and the stack sync path, which our tree rewrote (a64_backend.cc +3,909 lines, a64_emitter.cc +6,646).
Port only if an XNA title is a target. Not now.

Decision (user, 2026-09-20): not ported. Size measured against master: 18 commits, 55 files, 2,616
added lines; 1 of 21 commits applies clean; the largest commit conflicts in 11 files; the series also
depends on the edge stackpoint and function-resolution rework of 2026-09-18, which we skipped. A port
is a manual read of every file against ours plus a re-derivation of the a64 half, then a test with an
XNA title, and no title on the device is one. Estimate: one to two sessions. Start here if an XNA title
becomes a target: `32a95fff0d..f6184518b4` on `edge/edge`, in order, with `1e640ba491` for a64.

Standalone fixes worth a later port (all conflict on context):

| hash | what | why |
|---|---|---|
| `9d0df83a0f` | Implement XER[OV] and XER[SO] for the OE forms | We made the OE forms ignore overflow on 09-18 (canary 7d8ac86f6c). This is the correct implementation. Adds corpus files instr_mullwo.s and instr_nego.s. 11 files. |
| `f6184518b4` | Take a function's extent from the last instruction scanned | Scanner correctness. 31 lines in ppc_scanner.cc. |
| `2fe7b7d635` | Fix XEX patches that move the image base | Title update correctness. 42 lines in xex_module.cc. |

## Mesa Turnip since our bundled build (2026-08-07 to 2026-09-19)

167 commits under src/freedreno/vulkan, ir3, common, and fdl. Most are A8xx bring-up, sparse
binding, YUV, ray query, and cleanups. The ones that touch what our backend uses:

| Mesa commit | what | effect on us |
|---|---|---|
| `4c36bbc8518` | Fix the sType of the returned VkFragmentShadingRateKHRs | Our VRS clamp (`31f99fa6f6`) sets sType on the input array and reads only fragmentSize and sampleCounts, so the old bug did not break the clamp. No change. |
| `0cd184e94fc` | Do not reuse LRZ when it is cleared with a partial render area | Our passes set renderArea at offset 0 with the framebuffer extent. If the extent is smaller than the depth image (the tile-rounded 8192-tall images from the archive), the old driver reused corrupt LRZ across passes. The new driver disables the reuse, which is correct but may cost LRZ occlusion. Measure. |
| `84d36193fdf` | Disable LRZ for depth resolve targets | Correctness. |
| `74865282cf8` | Do not write LRZ direction when not writing LRZ | Fewer LRZ invalidations when Z write is off. A possible small win on passes with depth test and no depth write. |
| `99200197cc3`, `cc1cf17c2e0`, `10b1ef466fa` | SUBPASS_FENCE for GMEM invalidation and by-region dependencies | Avoids a CACHE_INVALIDATE for input attachment reads in GMEM. We use dynamic_rendering_local_read, so this is on our path. Possible small win. |
| `f5f841333e6`, `099ba5d80e0`, `df96a4daa7a` | TU_DEBUG=gmem_warmup, VSC overflow log, autotune force-reason log | Diagnostics. The VSC overflow log answers a question the archive left open: whether binning fails on the first frames. Read it with TU_DEBUG=perf. |
| `f6239b4be6c`, `096fc2be80a` | Custom resolve fixes | We use VK_EXT_custom_resolve. Correctness. |
| `5cb6e8cb888` | Resuming dynamic rendering re-emits attachment state | Correctness. |

Verdict: no headline Adreno 740 performance commit. Three items may move a number: the LRZ direction
change, the SUBPASS_FENCE change, and the LRZ partial-clear change in the other direction. The
diagnostics are worth having. Update the bundled driver and measure Blue Dragon and Gears on the
route, matched pairs, frame generation off.

## Decisions

- Turnip: update to `v26.3.0-20260919` and measure. The update is a rebuild and reinstall.
- Edge: no port now. Queue the three standalone fixes for the next CPU pass; run the corpus after.
- New sweep marks: edge `f6184518b4`; canary, XenDroid, and forks unchanged.
