# Blue Dragon VMX128 PACK/UNPACK Coverage Complete

## Summary

The reset VMX128/NEON lane needed complete semantic coverage before any
PACK/UNPACK behavior patch. This slice closes the remaining `8_IN_16` and
`16_IN_32` coverage gaps and fixes the stale CPU test harness stack assert so
these HIR unit tests can execute on Windows.

This is not a speed proof. No Thor route capture or quiet A/B was run.

## Changes

- Added active HIR tests for:
  - `PACK_8_IN_16`
  - `PACK_16_IN_32`
  - `UNPACK_8_IN_16`
  - `UNPACK_16_IN_32`
- Removed stale commented UNPACK placeholders for the same families.
- Fixed `TestFunction::Run` to construct `ThreadState` with the already
  computed stack base and PCR/thread-state address instead of hitting the old
  unconditional `assert_always()` TODO.
- Scoped the LO/HI UNPACK subcases so the singleton `Memory` test fixture is
  destroyed before the next `TestFunction` is constructed.
- Updated `tools/thor/thor_a64_vmx128_neon_family_audit.ps1` so PACK/UNPACK
  reports `coverage_complete_needs_route_volume` once all expected modes are
  covered.

## Validation

Commands run from repo root:

```powershell
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' build\xenia-cpu-tests.vcxproj /nologo /m /v:minimal '/p:Configuration=Debug Windows' /p:Platform=x64
```

Result: passed and produced
`build\bin\Windows\Debug\xenia-cpu-tests.exe`.

```powershell
build\bin\Windows\Debug\xenia-cpu-tests.exe "PACK_8_IN_16,PACK_16_IN_32,UNPACK_8_IN_16,UNPACK_16_IN_32"
```

Result: passed, `6 assertions in 4 test cases`.

```powershell
powershell -ExecutionPolicy Bypass -File tools\thor\thor_a64_vmx128_neon_family_audit.ps1
```

Result: passed with:

- `pack_tests=9/9`
- `unpack_tests=9/9`
- `commented_unpack_tests=0`
- `pack_unpack status=coverage_complete_needs_route_volume`
- `decision=do_not_patch_vmx128_behavior_next_without fresh route-volume counters proving broad non-closed usage; complete unit coverage alone is not speed evidence.`

```powershell
git diff --check
```

Result: passed with only CRLF conversion warnings for touched files.

## Decision

PACK/UNPACK coverage is no longer the blocker. The next VMX128 slice must be
route-volume evidence, not a behavior patch:

1. Count dynamic PACK/UNPACK usage by hot function/block on the current Thor
   Blue Dragon route.
2. Compare against PERMUTE and LOAD_VECTOR_SHL/SHR route volume so the next
   NEON behavior change targets a broad family, not a closed one-off PC.
3. Only patch behavior after counters show useful non-closed volume and the
   fallback/correctness path is explicit.

The broader sprint truth is unchanged: no sustained 30 FPS proof exists, and
latest captures remain Main Thread / A64 generated-code bound rather than broad
Vulkan-bound.
