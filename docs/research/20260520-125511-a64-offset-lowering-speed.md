# A64 Offset Lowering Speed Note

Date: 2026-05-20 12:55 America/New_York

## Question

Can generic A64 memory-offset lowering shrink Blue Dragon's current Thor hot
blocks without another title-specific helper splice?

## Change

`AddGuestMemoryOffset` now keeps guest address arithmetic in 32-bit W-register
space while avoiding unnecessary scratch materialization:

- Skip redundant `mov w0, w0` when the base address is already `x0`.
- Return immediately for constant offset `0`.
- Emit `add w0, w0, #imm` for constant offsets `0..4095`.
- Emit `add w0, w0, #imm, lsl #12` for page-aligned constants encodable by
  the AArch64 shifted-immediate form.
- Preserve the old `mov w17; add w0, w0, w17` path for other constants.

This is generic A64 backend work, not a Blue Dragon-only replacement.

## Evidence

Baseline speed-pack capture before this change:

- `scratch/thor-debug/20260520-110714-*`
- Reached the Blue Dragon loading spinner at 95 seconds.
- Hot `8272A3A4` code size was around `15952`.
- Hot `8272A8E8` code size was around `6412`.

First safe offset-lowering capture:

- `scratch/thor-debug/20260520-124856-*`
- APK SHA-256:
  `07BEBC73E8984A73820C66A02B876632D939E31D764CD233195AA936226EB39C`.
- Stayed in `EmulatorActivity` with PID `24691` through final metadata.
- No searched fatal markers in the focused filtered log.
- Final screenshot was black, so this is route-clean process proof, not title
  or loading-spinner visual proof.
- `8272A3A4 code_size=14612`.
- `8272A8E8 code_size=5892`.

Shifted-immediate follow-up capture:

- `scratch/thor-debug/20260520-125303-*`
- APK SHA-256:
  `07C80969A22E135C429C0F0628C87B0A68D736AECE1A3469DC6EBB9198F30526`.
- Stayed in `EmulatorActivity` with PID `26982` through final metadata.
- No searched fatal markers in the focused filtered log.
- Final screenshot was black, so this is route-clean process proof, not title
  or loading-spinner visual proof.
- `8272A3A4 code_size=14612`.
- `8272A8E8 code_size=5892`.

The useful measured shrink is from avoiding the redundant base copy and small
constant materialization. The shifted-immediate case did not hit the current
Blue Dragon hot blocks, but it is still a correct generic encoding path for
future guest memory offsets.

## Rejected Experiment

A broader wrapped-immediate `ADD`/`SUB` lowering was tested before this note:

- `scratch/thor-debug/20260520-124314-*`
- APK SHA-256:
  `0DC5E39B92646B8F26374DE29ABA3CEC7E26D7EE1675B0BB12732EF1053DC8E5`.
- `8272A3A4 code_size=14532`.
- `8272A8E8 code_size=5876`.
- The emulator process exited cleanly around 41 seconds, before the scripted
  START/A route completed. Subsequent launcher/file-picker activity was script
  fallout, not useful game evidence.

Decision: keep the guest memory offset lowering; do not retry broad
`ADD`/`SUB` immediate rewrites without per-op HIR semantic proof and a
route-clean Thor capture.

## Next

- Keep making generic A64 lowering passes before title-specific splices.
- Use code-size changes as a first filter only; keep/reject based on comparable
  Thor route proof.
- For `8272A3A4`, the next useful pass is disassembly/HIR-region analysis to
  find repeated generated patterns, not another blind whole-function shortcut.
