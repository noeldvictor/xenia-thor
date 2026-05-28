# Project Sylpheed guest free-site disassembly plumbing

## Context

The physical free-site audit for `scratch/thor-debug/remote-debug-20260528-163432`
found a stable Project Sylpheed guest free-site cluster:

- All 15 failed interior frees use owner allocation `BC220000:03A80000`.
- None of the failed free addresses match exact `MmAllocatePhysicalMemoryEx`
  results.
- All failed frees share runtime LR `821507BC`.
- CTR targets are `821511D0`, `821512F0`, `82151320`, `821513B0`,
  `82151470`, `821514A0`, `82151530`, `82151560`, `821515C0`,
  `82151680`, `821516B0`, `82151740`, `82151770`, `821517A0`,
  and `821517D0`.

`MmFreePhysicalMemory` is void, and hiding these failed frees would be cosmetic.
Blindly rounding interior frees down to the owner allocation would release the
whole 58.5 MiB parent region, so the next behavior step needs guest-code
size/lifetime proof.

## Finding

Direct `EmulatorActivity` launches already accepted `disassemble_functions` and
`disassemble_function_filter`, and the PPC translator can emit filtered PPC,
HIR, and machine-code dumps for matching functions. The real launcher/game
library path did not forward those two debug extras through `LauncherActivity`
debug overrides, so focused guest-code mapping would silently fail when using
the product path required for compatibility claims.

## Patch

The launcher debug bridge now forwards:

- `disassemble_functions`
- `disassemble_function_filter`

from `tools/thor/thor_xenia_debug.ps1 -Mode LaunchLauncher` into
`LauncherActivity`, then into the selected game-library row's
`EmulatorActivity` launch bundle.

This is behavior-neutral unless the debug cvars are explicitly supplied.

## Next command

Use the real game library route and keep the filter tight:

```powershell
.\tools\thor\thor_xenia_debug.ps1 -Mode LaunchLauncher -DeviceSerial c3ca0370 -DisassembleFunctions true -DisassembleFunctionFilter "821507BC,821511D0,821512F0,82151320,821513B0,82151470,821514A0,82151530,82151560,821515C0,82151680,821516B0,82151740,82151770,821517A0,821517D0" -XboxkrnlPhysicalMemoryAudit true -XboxkrnlPhysicalMemoryAuditBudget 512
```

After launching Project Sylpheed from the library row, capture logcat and map
the filtered dumps before considering any physical suballocation compatibility
behavior.
