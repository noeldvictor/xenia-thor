# 82485DD8 Word Copy-Loop Fastpath Probe

Date: 2026-05-24 01:15 EDT

Branch: `master`

Starting point:

- `docs/research/20260524-005156-82486178-vmx-copy-loop-fastpath.md` left
  `arm64_blue_dragon_vmx_copy_loop_fastpath` route-clean but not speed-proven.
- Fresh filtered capture `scratch/thor-debug/20260524-010013-*` reached the
  visible Blue Dragon opening sky/wing route on commit `50da5a0a9`, APK SHA
  `215678AC4C57B0D4DB7847311777CBF2A39910CE2A13000D668F0867A6A99019`, with
  no searched fatal markers.
- That capture found `82485DD8` is the stronger remaining `82490030` child:
  final `82485DD8 body_ticks_total=2039478`, `82486018=954041`, and
  `82486178=1432675`.
- `82485DD8` block body-time was dominated by local block `82485E70`:
  `body_ticks_total=840817`, `entries_total=1645735`, no direct calls.

## Block Shape

The hot block is a simple PPC word load/store update loop:

```text
82485E70 lwzu   r10, 0x4(r29)
82485E74 addic. r11, r11, -0x1
82485E78 rldicr r31, r31, 1, 62
82485E7C stwu   r10, 0x4(r4)
82485E80 bne    crf0, 0x82485E70
```

The following block checks `r31`:

```text
82485E84 cmpli  crf6, 1, r31, 0x0
82485E88 bne    crf6, 0x82485DF4
```

The HIR hotpath report for `82485DD8` shows `store_context=88`,
`load_context=59`, `branches=8`, `calls=5`, and `context_barriers=13`. Top
state traffic is GPR-heavy: `r[4]`, `r[29]`, `r[31]`, `r[28]`, `r[30]`,
`r[26]`, and CR6/CR0 fields.

## Patch

Added a default-off, title/block-gated A64 experiment:

- Cvar: `arm64_blue_dragon_word_copy_loop_fastpath=false`.
- Android extra passthrough:
  `arm64_blue_dragon_word_copy_loop_fastpath`.
- Thor launch flag:
  `-Arm64BlueDragonWordCopyLoopFastpath true|false`.
- Backend hook:
  `A64Emitter::TryEmitBlueDragonWordCopyLoopBlock`.

The shortcut fires only for function `82485DD8`, block `82485E70`, and loops in
A64 until the 32-bit `r11` count reaches zero, matching the HIR branch on CR0
EQ from the low 32-bit `addic.` result. It stores final PPC-visible `r29`,
`r4`, `r11`, `r31`, `r10`, `xer_ca`, and CR0 state. It preserves guest memory
bytes by loading raw 32-bit words from guest memory and storing those same raw
bytes to the destination, while storing byte-swapped host-endian `r10` into PPC
context.

Like the earlier VMX copy-loop probe, this does not preserve per-iteration
context stores or per-trip block counter entries. It stays research-only and
default-off until a quieter route A/B proves broader speed.

## Validation

Build and deploy:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode NativeCore
powershell -NoProfile -ExecutionPolicy Bypass -File tools\thor\thor_build.ps1 -Mode FullDeploy -DeviceSerial c3ca0370
```

Both passed. Deployed APK SHA:

`B240F237AC9ED68637BA5152699A742F75A4F6AD7555CF61845F78FB50992965`

Post-capture code review tightened the loop exit to `cbnz(w11, loop)` so the
fastpath follows the low 32-bit CR0 EQ branch condition emitted by HIR. The
rebuilt/deployed APK after that safety fix has SHA:

`CB234AD454F2F458BB22E436F16CCDC7DBD8EA7510DDD8005B8AC2E2B9E0CA3C`

## Same-APK A/B

Fastpath on:

- Capture: `scratch/thor-debug/20260524-010729-*`.
- Commit: `50da5a0a9`.
- APK SHA:
  `B240F237AC9ED68637BA5152699A742F75A4F6AD7555CF61845F78FB50992965`.
- Cvar: `arm64_blue_dragon_word_copy_loop_fastpath=true`.
- Screenshot reached visible Blue Dragon opening sky/wing route.
- Fatal-marker search was clean.
- Final body-time:
  - `82490030=4351286`
  - `82486178=2401264`
  - `82486018=884293`
  - `82485DD8=695645`
- Final `82485DD8:82485E70` block body-time:
  `75310`.
- Final perf: Main Thread `96.1%`, XMA Decoder `15.3%`,
  GPU Commands `15.3%`, Draw Thread `3.8%`.

Fastpath off control:

- Capture: `scratch/thor-debug/20260524-011110-*`.
- Same commit and APK SHA.
- Cvar: `arm64_blue_dragon_word_copy_loop_fastpath=false`.
- Screenshot reached visible Blue Dragon opening sky/wing route.
- Fatal-marker search was clean.
- Final body-time:
  - `82490030=5508640`
  - `82485DD8=2307804`
  - `82486178=1619067`
  - `82486018=1124678`
- Final `82485DD8:82485E70` block body-time:
  `1122726`.
- Final perf: Main Thread `92.3%`, XMA Decoder `42.3%`,
  GPU Commands `11.5%`, Draw Thread `3.8%`.

## Decision

This is a positive default-off probe, not a default-on promotion yet.

The same-APK profiled A/B says the shortcut removes most of the local
`82485E70` wall and lowers parent `82490030` body-time on this route:

- `82485DD8`: `2307804` off -> `695645` on.
- `82485E70`: `1122726` off -> `75310` on.
- `82490030`: `5508640` off -> `4351286` on.

Keep `arm64_blue_dragon_word_copy_loop_fastpath` default-off until a quieter
same-APK A/B without block body-time/disassembly confirms the broader route
speed. Do not combine it with the `82486178` VMX copy-loop toggle unless a
separate matrix proves the interaction.

## Next

Run a quiet same-APK route A/B with:

- `arm64_blue_dragon_word_copy_loop_fastpath=false`
- then `arm64_blue_dragon_word_copy_loop_fastpath=true`
- no block body-time profiler;
- no disassembly dump;
- body-time comparators kept to `82490030,82486178,82485DD8,82486018`.

If that still shows a route-wide win with clean screenshots and fatal search,
consider promoting the cvar for the Blue Dragon speed preset. Otherwise keep it
as a targeted default-off research toggle and move to `82486018:82486124`.
