# A64 Wrapped Addi Lowering Bisect

Date: 2026-05-20 14:48 America/New_York

## Question

Can we take the obvious aX360e-style A64 backend cleanup farther by turning
wrapped integer immediates such as guest `addi -1` into native AArch64
`sub #1` instead of materializing `0xFFFFFFFF` and doing a register add?

## Donor Check

The local aX360e donor at
`scratch/upstream/ax360e/app/src/main/cpp/xenia-canary/src/xenia/cpu/backend/a64`
does not contain a better implementation for this exact path. Its
`ADD_I32`, `ADD_I64`, `SUB_I32`, and `SUB_I64` lowering matches the current
Thor fork shape: small positive immediates use AArch64 immediate forms, and
larger constants are materialized into a scratch register.

So this was not a clean paste from aX360e. It was an attempted improvement on
top of the donor backend.

## Baseline

Baseline before this experiment:

- Capture: `scratch/thor-debug/20260520-142915-*`
- APK SHA-256:
  `5D60BD98548642CEE488FB2AB9C3C7478619CD700687A8C2A28C17C381E31100`
- Route stayed active through 100 seconds.
- `8272A3A4 code_size=13100`
- Later intervals kept moving, including:
  - `entry_delta=5,446,465` around 75 seconds;
  - `entry_delta=26,455,559` around 90 seconds.

## Attempt 1: Broad I32/I64 ADD/SUB

Changed:

- `ADD_I32` and `ADD_I64` could emit opposite-op wrapped immediates.
- `SUB_I32` and `SUB_I64` could emit opposite-op wrapped immediates.
- Encoded shifted immediates where possible.

Thor capture:

- Capture: `scratch/thor-debug/20260520-143752-*`
- APK SHA-256:
  `056085154CF2120B2013672AC1228C5C22A88CAAEF42DBEA22589E72DE1C9903`
- No native fatal marker.
- Process stayed in `EmulatorActivity`.
- Bad route signature: guest counters went flat after the early boot burst.
  `entry_delta=0` from the interval at 14:38:41 onward.
- `8272A3A4 code_size=13020`, so this did shrink code, but the route was not
  valid.

Decision: reject.

## Attempt 2: Narrow ADD_I32 Only

Changed:

- Only `ADD_I32` with a constant could use the wrapped-negative immediate path.
- No `ADD_I64`, `SUB_I32`, `SUB_I64`, or shifted-immediate widening.

Thor capture:

- Capture: `scratch/thor-debug/20260520-144309-*`
- APK SHA-256:
  `4B127F9FDD538A7DCEC890379E385B9CDFEF717DF881B47048F3D25BEBBB7494`
- No native fatal marker.
- Process stayed in `EmulatorActivity`.
- Bad route signature reproduced:
  `entry_delta=0` at 14:43:58 and 14:44:13.
- Idle snapshot could not obtain the processor debug lock:
  `last_global_owner_sys_tid=9718`, `global_lock_count=0`.
- `8272A3A4 code_size=13100`, so this narrower change did not even shrink the
  main hot block.

Decision: reject.

## Restore Proof

After backing out the source change, the known-good APK was rebuilt and
redeployed.

- Capture: `scratch/thor-debug/20260520-144638-*`
- APK SHA-256:
  `5D60BD98548642CEE488FB2AB9C3C7478619CD700687A8C2A28C17C381E31100`
- No native fatal marker.
- Route stayed active through the 60 second capture:
  - 14:47:12 `entry_delta=1,651,888`
  - 14:47:27 `entry_delta=1,905,762`
  - 14:47:42 `entry_delta=2,029,704`
- At 60 seconds, `top -H` showed the expected active profile:
  - Main Thread about 80.7% CPU
  - XMA Decoder about 42.3%
  - GPU Commands about 19.2%

This confirms the wrapped-immediate rewrite, not the device state, caused the
idle regression.

## Interpretation

The source registers are allocated from `x22..x28`, so this is not the usual
AArch64 register-31 SP/ZR immediate-form trap.

The important lesson is narrower: code-size shrink from replacing scratch
materialization is not enough proof for this backend. Even an apparently
equivalent `ADD_I32 + 0xFFFFFFFF -> SUB_I32 #1` rewrite can perturb the current
Blue Dragon route. The likely causes are a subtle guest-width/sign-extension
assumption, an existing scratch-clobber dependency elsewhere, or route timing
coupled to early generated code shape. We need instrumentation that names the
guest function and HIR instruction before retrying.

## Decision

No code change was kept.

Do not reintroduce wrapped-immediate `ADD`/`SUB` lowering as a default path
until a compile-time audit logs the exact guest functions and HIR instructions
being rewritten, and a Thor capture proves the route reaches at least the same
later active intervals as `20260520-142915` or `20260520-144638`.

Next useful work:

- Add a default-off A64 lowering audit that counts and optionally logs
  guest-function/opcode/constant for rejected immediate-rewrite candidates.
- Use it to decide whether a title-specific hot function, such as `8272A3A4`,
  even benefits from the rewrite.
- Keep importing aX360e backend structure, but do not assume generic A64
  cleanup is safe without Thor route proof.

## Follow-Up

The audit was added later the same day and found that the safe candidate set in
the first Blue Dragon budget was `ADD_I64`, not the earlier failed `ADD_I32`
path. The narrow `ADD_I64 reg, reg, wrapped-small-negative` lowering was then
route-proven and kept default-on. See
`docs/research/20260520-151030-a64-add-i64-wrapped-immediate-fastpath.md`.
