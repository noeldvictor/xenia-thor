# ARM64 JIT Emitter Research: AsmJit vs VIXL

Accessed: 2026-05-17 14:22:24 -04:00

## Decision

Prefer VIXL for the first Xenia AArch64 backend prototype, unless a spike shows Android NDK/Premake integration is painful enough to reverse the choice.

Rationale:

- Xenia needs a low-level AArch64 emitter more than a separate high-level compiler.
- VIXL is purpose-built for ARMv8 runtime code generation and provides A64 assemblers, disassemblers, and a simulator.
- The simulator is valuable because this project is being developed from Windows/x64 while the target runtime is Android/ARM64.
- VIXL's MacroAssembler can synthesize some multi-instruction sequences for awkward immediates and is recommended by VIXL itself for most users.
- VIXL is already mirrored in Android's source tree, which lowers Android ecosystem risk.

## AsmJit

Sources:

- https://asmjit.com/doc/index.html
- https://asmjit.com/doc/group__asmjit__build.html
- https://asmjit.com/doc/group__asmjit__a64.html
- https://github.com/asmjit/asmjit
- https://raw.githubusercontent.com/asmjit/asmjit/master/LICENSE.md

Useful facts:

- AsmJit is a C++ low-latency machine-code generation library.
- Official docs list an AArch64 backend with `a64::Assembler`, `a64::Builder`, and `a64::Compiler`.
- It supports C++17, which matches this repo's `cppdialect("C++17")`.
- It can be embedded by adding the source tree and defining `ASMJIT_STATIC` / `ASMJIT_EMBED`.
- Build docs say AArch64 is tested in CI.
- License is zlib.

Pros for Xenia:

- Easy static embedding story.
- Modern C++17 codebase.
- AArch64 instruction database, validation, logging, and error handler support.
- Could disable x86 backend to keep size down once integrated.

Risks for Xenia:

- No built-in AArch64 simulator for x64-host testing.
- Xenia already has its own HIR/compiler/register allocation pipeline, so AsmJit's high-level compiler may not remove much work.
- Need a spike to verify Android NDK static integration and generated-code memory behavior.

## VIXL

Sources:

- https://android.googlesource.com/platform/external/vixl/
- https://github.com/Linaro/vixl
- https://gitlab.arm.com/runtimes/vixl
- https://raw.githubusercontent.com/Linaro/vixl/master/README.md
- https://raw.githubusercontent.com/Linaro/vixl/master/LICENCE

Useful facts:

- VIXL is an ARMv8 runtime code generation library.
- It provides programmatic assemblers for A64/A32/T32, disassemblers, and an A64 simulator.
- The GitHub README says active maintenance moved to Arm GitLab; the Linaro GitHub repo is no longer updated.
- Android's source tree carries VIXL 8.0.0 under `platform/external/vixl`.
- VIXL lists support for ARM architecture features including FP16, dot product, atomics, SVE/SVE2, and others.
- VIXL documents known limitations, including limited rounding mode support, limited synchronization/system instruction support, and some missing miscellaneous integer/floating-point instructions.
- License is BSD-style permissive.

Pros for Xenia:

- Strong fit for explicit AArch64 emission.
- Simulator can help test emitted backend thunks and small translated functions on non-ARM64 hosts.
- MacroAssembler is a good safety layer for immediate encodability and generated instruction sequences.
- Android ecosystem precedent through AOSP mirror.

Risks for Xenia:

- Active upstream is Arm GitLab, not the older Linaro GitHub mirror; submodule URL choice matters.
- VIXL's own test/build tooling mentions SCons, though Xenia can integrate sources directly through Premake.
- Known floating-point and synchronization limitations need scrutiny against PPC/HIR lowering needs.
- Need to verify code size and compile time on Android NDK.

## Prototype Plan

1. Add VIXL as a third-party dependency in a separate branch/spike.
2. Integrate only the source files required for AArch64 MacroAssembler, disassembler, and simulator if practical.
3. Add a tiny host-side AArch64 emission test that emits simple arithmetic code, disassembles it, and runs it through VIXL simulator on Windows/x64 if supported.
4. Add Android runtime test for writable memory, generated AArch64 code, executable protection, instruction-cache flush, and calling generated code.
5. Only after this prototype should the real `Arm64Backend` start lowering Xenia HIR.

## Fallback

If VIXL integration becomes too heavy or its limitations block Xenia lowering, use AsmJit as the fallback because it has clearer CMake/static embedding docs, current AArch64 backend docs, CI coverage, and a simple zlib license.
