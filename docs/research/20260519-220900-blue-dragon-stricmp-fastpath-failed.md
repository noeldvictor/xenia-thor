# Blue Dragon Stricmp Fastpath Failed Probe

Date: 2026-05-19 22:09 EDT

Branch: `master`

Base commit: `add869454`

Device: AYN Thor Max, serial `c3ca0370`

Target:
`/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Question

`826C5620` repeatedly appears in the Blue Dragon A64 entry profile after the
draw-wait and memcpy probes. Focused PPC dumps classify it as an ASCII
case-insensitive string compare. Could replacing the whole guest function body
with a hand-emitted A64 loop reduce generated-code time without changing game
progress?

## Probe

A temporary default-off cvar, `arm64_blue_dragon_stricmp_fastpath`, replaced
guest function `826C5620` with an A64 loop that:

- read string pointers from PPC `r3` and `r4`;
- walked translated guest memory byte by byte;
- returned `r5 - r6` in `r3`;
- lowercased ASCII `A` through `Z` before the second compare;
- on the second revision, also wrote back volatile-looking guest registers
  `r4`, `r5`, `r6`, and `r9`.

The probe was built and deployed, but was not committed because both Thor runs
regressed visible progress.

## Evidence

First capture:
`scratch/thor-debug/20260519-220047-*`

Result:

- APK SHA-256:
  `C8D8C4DB9FC4CEEF385B080D5861B6272CE250687D5684A7F06515E8AB466F9C`
- Screenshot at 130s was black instead of the previous Blue Dragon loading
  spinner.
- A64 entry deltas dropped to zero after roughly 2026-05-19 22:01:19 EDT.
- The 105s-117s simpleperf sample had only 501 samples and mostly showed
  timer/logging idle work.
- No fatal signal, AndroidRuntime crash, Vulkan device loss, GPU-hung marker,
  or ANR marker explained the regression.

Second capture after preserving extra volatile registers:
`scratch/thor-debug/20260519-220549-*`

Result:

- APK SHA-256:
  `223BC236F0FA26DE9AE606E0621123C482C9E63A587CE8E7F668AB8E17BE842D`
- Screenshot at 75s was still black.
- A64 entry deltas again dropped to zero after roughly
  2026-05-19 22:06:21 EDT.
- Perf at 70s showed guest threads sleeping or idle, with no useful crash
  marker.

## Current Inference

The replacement loop is not safe as a body fastpath.

Most likely causes:

- The original PPC helper updates condition-register fields (`cr0`, `cr5`,
  `cr6`, and `cr7`) that a caller or inlined path still observes.
- The game depends on more exact `lbzu` side effects than the probe preserved.
- The generated function boundary, LR/return semantics, or caller ABI behavior
  is not as normal as the helper-shaped code first suggested.

This also confirms that title-specific helper fastpaths must preserve
PPC-visible state, not only the obvious return register.

## Decision

Remove the temporary stricmp code and keep `826C5620` on the normal generated
A64 path until a narrower state probe proves the exact caller dependencies.

Next useful experiment:

- Add a low-budget `826C5620` return-state probe that logs/samples LR, input
  pointers, return `r3`, touched volatile registers, and condition-register
  fields after the normal generated function runs.
- Use that evidence to decide whether a safe leaf fastpath should update CR
  state exactly, or whether the next speed target should move to `827294CC`,
  `8272A3A4`, or `8272A8E8` instead.
