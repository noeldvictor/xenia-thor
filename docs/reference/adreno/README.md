# Qualcomm Adreno GPU documentation

Qualcomm's **Game Developer Guide** (doc id `80-78185-2`), fetched 2026-08-10 and kept here because it is the
only vendor GPU documentation this project has, and we are GPU-bound on a TBDR.

| file | source topic | what it is |
|---|---|---|
| `mobile_best_practices.txt` | `mobile_best_practices.html` | **"Adreno GPU on Mobile: Best Practices"** - the useful one. Render passes, subpass merging, GMEM, binning, LRZ, UBWC, VRS |
| `overview.txt` | `overview.html` | feature/extension overview - tile shading Vulkan extensions, mesh shading, VRS |
| `gpu.txt` | `gpu.html` | Adreno GPU architecture summary |
| `cpu.txt` | `cpu.html` | Kryo CPU notes (the Arm SWOGs in `../arm/` are authoritative for the cores) |
| `sdp.txt` | `sdp.html` | Snapdragon Profiler |
| `vk_adreno_layer.txt` | `vk_adreno_layer.html` | the Vulkan Adreno Layer and its `VKDBGUTILWARN*` diagnostics |
| `landing.txt` | `landing.html` | index / what's new |

## How to re-fetch (the part that was the actual obstacle)

**The site is a JavaScript SPA. A plain HTTP GET returns nothing useful**, which is why this project recorded
Adreno docs as unobtainable for months. A headless browser solves it:

```bash
pip install playwright && python -m playwright install chromium
```

**Use the right URL shape.** The guessable `docs.qualcomm.com/bundle/publicresource/topics/80-78185-2/<x>.html`
paths return the site's 404 page (which still renders with a plausible `<title>`, so check the body text, not
the title). The real one is:

```
https://docs.qualcomm.com/doc/80-78185-2/topic/<page>.html
```

Discover the page list by loading `https://docs.qualcomm.com/bundle/publicresource/topics/80-78185-2/` and
reading its anchors. Allow ~5s after `domcontentloaded` for the SPA to settle, and treat a body under ~1200
chars, or one containing "could not find the page", as a miss.

## Scope, so the old claim is not re-derived

There is still **no SM8550 CPU technical reference manual** - the 8 Gen 2 uses stock Arm cores, so the four
Cortex software optimization guides in `../arm/` are the authoritative CPU documents, and
`../snapdragon/snapdragon-8-gen-2-product-brief.pdf` really is 2 pages of marketing. That claim was correct for
the CPU and got over-generalised to the GPU. **A substantial Adreno GPU optimization guide does exist**, and it
is these files.
