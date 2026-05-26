# VMX128 PACK/UNPACK Coverage Slice

## Context

The reset plan keeps Blue Dragon work on structural A64/JIT lanes unless new
Thor captures show a different wall. The current evidence still says the route
is Main Thread / generated-code bound around `82282490`, `82281D28`, and
`82287788`, while exact f1 edge-payload, stvewx, three-PC `MUL_ADD_V128`,
fpscr, and narrow r1/r11 lanes are closed.

`docs/research/20260525-210145-vmx128-neon-family-audit.md` ranked VMX128
PACK/UNPACK as a broad candidate only after unit coverage improves. This slice
adds coverage; it does not change generated behavior.

## Code Changes

Added x64 HIR semantic tests for:

- `PACK_SHORT_4`
- `PACK_UINT_2101010`
- `PACK_ULONG_4202020`
- `UNPACK_SHORT_4`
- `UNPACK_UINT_2101010`
- `UNPACK_ULONG_4202020`

The audit helper now reports PACK/UNPACK missing modes dynamically instead of
printing the stale `4/9` summary.

## Audit Result

Command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_a64_vmx128_neon_family_audit.ps1
```

Current result:

- PACK coverage: `7/9`
- UNPACK coverage: `7/9`
- Remaining missing modes: `8_IN_16`, `16_IN_32`
- Decision remains: do not patch VMX128 behavior until coverage is complete
  for the chosen opcode family or fresh route-volume counters prove broad
  non-closed usage.

## Validation

- `python xenia-build premake` passed and regenerated `build/xenia.sln`.
- `git diff --check` passed with only existing CRLF conversion warnings.
- Direct checked MSBuild for `build\xenia-cpu-tests.vcxproj` did not reach the
  new CPU tests because local VS BuildTools 2022 / MSVC 14.44 treats existing
  third-party `fmt` `stdext::checked_array_iterator` deprecation warning C4996
  as C2220 in `xenia-base` before compiling the modified test target. Logs:
  `scratch/thor-debug/20260525-vmx-pack-tests-msbuild.log`,
  `scratch/thor-debug/20260525-vmx-pack-tests-msbuild-nowerror.log`, and
  `scratch/thor-debug/20260525-vmx-pack-tests-msbuild-silence4996.log`.

## Next

Close the remaining `8_IN_16` and `16_IN_32` PACK/UNPACK unit coverage, then
choose between a fresh route-volume audit for PACK/UNPACK/PERMUTE and a
separate structural register-allocation / guest-state cache slice. Do not run a
Thor speed A/B from this coverage-only patch.
