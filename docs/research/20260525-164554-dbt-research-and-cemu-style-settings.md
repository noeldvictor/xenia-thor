# DBT Research Triage And Cemu-Style Settings

Date: 2026-05-25 16:45 EDT

## Sources Checked

- Elevator static translation: https://arxiv.org/abs/2605.08419
- Partial cross-compilation / mixed execution: https://arxiv.org/abs/2512.00487
- Learning-based DBT rules: https://www.usenix.org/system/files/atc19-song_0.pdf
- Arancini weak-memory hybrid translator: https://research-portal.st-andrews.ac.uk/en/publications/arancini-a-hybrid-binary-translator-for-weak-memory-model-archite/
- AtoMig TSO-to-WMM migration: https://brightfu.github.io/papers/atomig.pdf
- Mainline Cemu: https://github.com/cemu-project/Cemu
- SSimco Android Cemu fork: https://github.com/SSimco/Cemu

## Triage

The user's paper list is useful, but it should not be turned into a vague
"replace the JIT with AI/static translation" mandate.

- Fully-static translation is a long-term offline translation/checking lane.
  Elevator targets whole x86-64 to AArch64 binaries and pays with significant
  code expansion. For xenia-thor, the near-term analogue is route-backed static
  guest-function specialization or generated-code audit artifacts, not a whole
  Xbox 360 game to native APK conversion.
- Partial cross-compilation maps well to hot function/edge variants. The useful
  lesson is explicit native-call channels with fallbacks, which matches the
  current `82282490 -> 82287788` edge-variant and state-carrier lane.
- Learning-based DBT rule generation is a rule-mining idea only when paired
  with symbolic equivalence checks and test coverage. Do not accept LLM- or
  ML-suggested instruction rewrites without proof, targeted PPC tests, and Thor
  route captures.
- Arancini and AtoMig reinforce that ARM weak-memory correctness must be
  explicit. Do not remove barriers, stack/state sync, call-visible writebacks,
  or external visibility points only because they look expensive in a local
  span.
- Host-GPU memory latency work supports the existing "measure before pivot"
  rule. Do not offload Blue Dragon CPU/JIT work to Vulkan compute unless
  captures show a bulk, stable, cache-friendly kernel and GPU counters show
  actual headroom.

## Implemented Settings Slice

Started the Cemu-style Android settings lane with a small persistent settings
surface:

- New launcher `Settings` entry.
- New `SettingsActivity` with profiles and GPU/audio/controller/runtime groups.
- New `XeniaAndroidSettings` shared-preferences helper.
- Game launch now builds cvar bundles from saved settings instead of hard-coded
  `gpu=vulkan`, `cpu=arm64`, `apu=nop`, and `hid=android`.
- Stable user-facing defaults remain conservative: Vulkan, ARM64, nop audio,
  Android/AYN Thor input, Discord off, research counters off.

This is not the full Cemu-level UX yet. It is the first scaffold for per-game
profiles, controller remapping, an input test screen, import/export, and an
expert cvar page.

## VMX128 Inline Patching Lane

The user's Box64 and DBT notes point at the right kind of speed lane:
aggressive inline native patches for hot translated blocks, with strict
fallbacks and invalidation/correctness discipline.

Thor device feature check on ADB device `c3ca0370`:

```text
model=AYN Thor
platform=kalama
features=fp asimd crc32 atomics fphp asimdhp asimdrdm jscvt fcma lrcpc dcpop
sha3 sm3 sm4 asimddp sha512 asimdfhm dit uscat ilrcpc flagm ssbs sb paca pacg
dcpodp flagm2 frint i8mm bf16 bti
```

Important conclusion: do not assume SVE/SVE2 for this Thor. The available
near-term hardware is 128-bit NEON / Advanced SIMD plus dot-product (`asimddp`),
I8MM, BF16, FHM, FCMA, and CRC-related extensions.

Actionable VMX128 patch guidance:

- Prefer route-backed, default-off, function/PC-gated inline VMX128 sequences
  over broad opcode rewrites.
- Use NEON for direct 128-bit vector ops: boolean ops, splats, permutes,
  shifts, compares, min/max, pack/unpack, and load-vector-shift joins.
- Consider `SDOT` / `UDOT` only for integer VMX sum-of-products forms after
  proving signedness, lane grouping, saturation, endianness, and horizontal
  accumulation semantics. Do not apply them to FP32 VMX dot products.
- Treat `FMLA`/fused FP forms as risky for VMX FP. Existing evidence shows
  FMA-style shortcuts can shrink code but miss route-wide speed proof, and x64
  avoids fused host FMA for some VMX semantics due test differences.
- Treat BF16/FHM/FCMA as not directly usable for Xbox 360 VMX FP32 unless a
  specific opcode audit proves exact guest-visible semantics.
- Add counters before behavior changes: static opcode counts, dynamic hit
  counts, fallback reasons, feature-gate decisions, NaN/denormal/FPCR fixup
  counts, source/destination aliasing, and generated-code size delta.
- Follow the Box64 lesson as architecture, not code: fast native blocks need
  robust invalidation/fallback. For Xenia, that maps to code-cache invalidation,
  normal-entry fallback, title/function/PC gates, and route-clean Thor captures.

## Next

- Build/deploy this APK shell to Thor and verify the launcher Settings screen
  opens, persists values, and launches Blue Dragon with the selected settings.
- Add an input test screen that displays Android key/axis values and the mapped
  Xbox 360 control state.
- Move from global settings to per-game profiles once the first screen is
  verified.
