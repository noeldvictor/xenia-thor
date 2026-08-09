# Snapdragon / SM8550 reference — and why this directory is nearly empty

**Short version: there is no Qualcomm CPU manual to find. Stop looking. The authoritative
microarchitecture documents for this chip are the four Arm SWOGs in `../arm/`.**

This directory exists to close a question that kept being re-opened, with the document in hand
rather than by assertion.

## What is here

| file | what it actually is |
|---|---|
| `snapdragon-8-gen-2-product-brief.pdf` | Qualcomm's official public brief for the SM8550. **2 pages of marketing.** |

Fetched 2026-08-09 from
<https://www.qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/Snapdragon-8-Gen-2-Product-Brief.pdf>

**Measured content, so nobody has to re-check:** 2 pages, ~10.5 KB of text, and the keyword counts
are the whole story —

```
cortex: 0    cache: 0    pipeline: 0    issue: 0    throughput: 0
neon:   0    sve:   0    l3:    0
kryo:   7    ghz:   9    latency: 4  (all marketing usages, e.g. "low latency gaming")
```

**Zero mentions of Cortex, cache, pipeline, issue width or throughput.** It contains nothing that
can inform codegen. It is kept only so the next person can see that for themselves in ten seconds
instead of spending a session hunting for a better version of it.

## Why there is no TRM, and why it does not matter

1. **Qualcomm does not publish a CPU Technical Reference Manual for the SM8550.** Public material is
   the product brief above plus datasheets. Confirmed 2026-08-09 by search; a full TRM, if one
   exists, is behind an authorised customer program.
2. **It would not help if it did exist.** The 8 Gen 2 uses **stock Arm cores** — Cortex-X3, A715,
   A710, A510 — not custom Kryo designs. (Qualcomm still brands the cluster "Kryo", which is what
   makes this confusing.) So the per-instruction latency / throughput / issue-pipeline tables that
   codegen actually needs come from **Arm**, and they are already in `../arm/`:
   - `cortex-x3-software-optimization-guide.pdf`
   - `cortex-a715-software-optimization-guide.pdf`
   - `cortex-a710-software-optimization-guide.pdf`
   - `cortex-a510-software-optimization-guide.pdf`
   - `arm-architecture-reference-manual-a-profile.pdf` (69 MB)
   - `aapcs64-callee-saved-notes.md` (the procedure call standard — decides cross-call residency)

## ⚠️ THE "HUGE MANUAL FROM THE VIDEO" IS ALREADY IN THIS REPO

The recurring ask traces to Whatcookie's talk *"PS3 emulation is fast on ARM now"*, which is said to
have involved reading a huge manual. **That manual is the Arm Architecture Reference Manual, and the
talk names it directly: "the ARM Architecture Reference Manual … over 17,000 pages".** Independent
press coverage of the same work says the same thing — *"they scoured every page of an ARM
Architecture manual with over 17,000 pages"*.

**It is `../arm/arm-architecture-reference-manual-a-profile.pdf`, 69 MB, already in-repo.** It is not
a Qualcomm document and never was. There is nothing to scrape and no reason to reach for Playwright.

## Where the numbers a product brief would have actually come from

Cache sizes and clocks are chosen by the SoC integrator and are **read off the device**, not a
document:

```
/sys/devices/system/cpu/cpu*/cache/            # L1/L2/L3 sizes and sharing
/sys/devices/system/cpu/cpu*/cpufreq/          # per-core frequencies
/proc/cpuinfo                                  # ISA features actually present
```

Device-measured topology for the Thor is recorded in `src/xenia/base/thor_topology.h` and in
CLAUDE.md: cpu0-2 = 3× A510 @2.0 GHz, cpu3-6 = 2× A715 + 2× A710 @2.8 GHz, cpu7 = X3 @3.19 GHz.
Feature set from `/proc/cpuinfo`: `asimddp i8mm bf16 fphp asimdhp atomics lrcpc ilrcpc sha3` —
**and no SVE**, which is why every SVE idea from the RPCS3 work is N/A here.
