# Thor Max Translation Target Check

Date: 2026-05-19 17:25 EDT

Access date for web sources: 2026-05-19

## Question

Is the AYN Thor Max a good fit for translation-based Xbox 360 emulation research,
and is the "roughly 20x more powerful" intuition useful?

## Short Answer

Yes, Thor Max is a strong research target for translation/HLE work, but "20x"
should be treated as a headroom intuition rather than a literal performance
promise. The device has modern mobile CPU cores, Adreno 740, Vulkan 1.3 support,
16 GB RAM in Max configurations, active cooling, microSD storage, Android
developer access, and built-in handheld controls. That is exactly the sort of
machine where a good PPC-to-AArch64 JIT, Xenos-to-Vulkan path, and Xbox OS HLE
can plausibly work.

The caveat is that emulation performance is not a straight hardware multiple.
Endian conversion, VMX128 lowering, guest/host transitions, code-cache dispatch,
kernel timers/events, shader translation, Xenos EDRAM/resolve behavior, and
Adreno driver behavior can burn a lot of that raw headroom.

## Evidence

- AYN's current Thor product page lists Max 16+512GB and Max 16+1TB variants,
  both using Snapdragon 8 Gen 2 and Android 13, with UFS 3.1 storage listed for
  the Thor line.
- AYN's store front summarizes Thor as a Qualcomm 8Gen2 Android handheld with a
  customized FHD 6-inch AMOLED display and 6000 mAh battery.
- Qualcomm lists Snapdragon 8 Gen 2 as a 64-bit Kryo CPU platform with clocks up
  to 3.36 GHz depending on OEM implementation. Qualcomm also lists Adreno GPU
  APIs including Vulkan 1.3, OpenGL ES 3.2, and OpenCL 2.0 FP, and describes
  Snapdragon 8 Gen 2 as its first Snapdragon mobile platform with Vulkan 1.3
  support.
- Retail and press listings for Thor Max identify the Max configuration as
  Snapdragon 8 Gen 2, 16 GB RAM, 1 TB storage, Adreno 740, and active cooling.
  Notebookcheck also reports Thor Max as Snapdragon 8 Gen 2 / 16 GB RAM / 1 TB
  storage and notes an active cooling fan, microSD slot, and Android 13.
- Xbox 360 hardware references describe a 3.2 GHz triple-core PowerPC Xenon CPU
  with two hardware threads per core, 512 MB unified GDDR3 memory, and ATI Xenos
  graphics with 10 MB eDRAM. TechPowerUp's Xbox 360 GPU entry lists 240 GFLOPS
  for the Xenos-class GPU.
- Xenia architecture references describe the emulator as dynamic translation and
  HLE rather than cycle-accurate hardware duplication: PowerPC guest CPU code is
  JIT-translated, Xbox 360 GPU work requires command/shader/EDRAM translation to
  modern APIs such as Vulkan or Direct3D 12, and kernel/system services are
  re-created in software.

## Interpretation

Thor Max has enough theoretical headroom to be worth the experiment:

- RAM capacity alone is huge compared with Xbox 360: 16 GB versus 512 MB is
  roughly 32x capacity.
- GPU raw compute is plausibly an order of magnitude or more above Xenos-class
  240 GFLOPS, depending on the Adreno 740 clock and workload. This does not mean
  Xenos features are free because EDRAM, tiling, resolves, packed formats, and
  synchronization do not map directly to modern Vulkan.
- CPU comparisons are not clean because Xenon is an in-order PowerPC console CPU
  with VMX128 and game-tuned memory behavior, while Snapdragon 8 Gen 2 is an
  out-of-order ARM SoC with very different caches, SIMD, memory, OS scheduling,
  and thermal limits. Translation quality matters more than clock comparison.
- Memory bandwidth is not simply 20x. Xbox 360 has unusual eDRAM bandwidth for
  render-target operations, while Thor has modern LPDDR and a mobile GPU memory
  system. Some paths will be much faster; some Xenos-specific paths need careful
  emulation.

So the right mission statement is:

1. PPC guest code to AArch64 DBT/JIT with good direct chaining, indirect lookup,
   endian-aware load/store lowering, VMX128/FP lowering, and low transition cost.
2. Xenos command/shader/EDRAM behavior to Vulkan/SPIR-V on Adreno 740.
3. Xbox OS, devices, input, audio, files, timers, threads, and events to HLE with
   focused low-level probes only where a game proves the detail matters.

## Decision

Update `AGENTS.md` to make this the durable mental model. Future work should
treat Thor Max as a translation/HLE target with real hardware headroom, but all
performance claims must come from actual Thor captures: APK hash, cvars,
screenshot/route proof, speed profile counters, thread samples, and worklog
notes.

## Sources

- AYN Thor product page: https://www.ayntec.com/products/ayn-thor
- AYN store front Thor summary: https://www.ayntec.com/
- Qualcomm Snapdragon 8 Gen 2 product page:
  https://www.qualcomm.com/smartphones/products/8-series/snapdragon-8-gen-2-mobile-platform
- Qualcomm Snapdragon 8 Gen 2 product brief:
  https://www.qualcomm.com/content/dam/qcomm-martech/dm-assets/documents/Snapdragon-8-Gen-2-Product-Brief.pdf
- Best Buy Thor Max listing:
  https://www.bestbuy.com/product/thor-max-6-dualamoled-android-gaming-handheld-snapdragon-8-gen-2-16gb-ram-1tb-storage/J3R85PHQWG
- Notebookcheck Thor shipping/spec report:
  https://www.notebookcheck.net/AYN-Thor-dual-screen-handheld-begins-shipping-but-only-the-Snapdragon-8-Gen-2-version.1138344.0.html
- TechPowerUp Xbox 360 GPU specs:
  https://www.techpowerup.com/gpu-specs/xbox-360-gpu-80nm.c3507
- Xbox 360 technical specifications:
  https://en.wikipedia.org/wiki/Xbox_360_technical_specifications
- Xenia CPU emulation knowledge base:
  https://xenia-emulator.com/knowledge-base/cpu-emulation
- Xenia GPU emulation knowledge base:
  https://xenia-emulator.com/knowledge-base/gpu-emulation/
- Xenia kernel/system API emulation knowledge base:
  https://xenia-emulator.com/knowledge-base/kernel-system-api-emulation/
