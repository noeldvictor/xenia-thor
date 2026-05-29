# Gears / Native Assert Symbolication (autonomous iter 1)

## Status

Symbolication only (no fix yet). Offsets from the
`game-pass-all-20260529-184405` sweep tombstones, resolved against the unstripped
arm64 lib with NDK addr2line.

- Lib: `android/.../build/intermediates/merged_jni_libs/githubDebug/out/arm64-v8a/libxenia-app.so` (256 MB, symbolized)
- Tool: `ndk/25.0.8775105/.../llvm-addr2line.exe -e <so> -f -C -i`
- All four native crashers are `#00 abort` -> `#01 __assert2` (failed `assert()`).

## Findings

**Gears 3 == Gears: Judgment (shared root cause): GPU shader interpreter ALU path.**

- `0xdc9d40` -> `xe::gpu::ShaderInterpreter::ExecuteAluInstruction(...)`
  `src/xenia/gpu/shader_interpreter.cc:866`
- `0xdcb6b4` -> `ucode::VertexFetchInstruction::src_swizzle()` `src/xenia/gpu/ucode.h:715`
- `0xdcaa6c` -> `ucode::AluInstruction::vector_opcode()` `src/xenia/gpu/ucode.h:1872`
- caller `0x9e9f34` -> `VulkanCommandProcessor::AcquireScratchGpuBuffer(...)`
  `src/xenia/gpu/vulkan/vulkan_command_processor.cc`

Reads as an assertion tripping inside CPU-side shader interpretation of an ALU
instruction (likely an unhandled/unexpected opcode or operand), reached while
acquiring a scratch GPU buffer. Both UE3 Gears titles hit the identical PCs.

**Gears of War 1: distinct - `XmaDecoder::ReadRegister` (`src/xenia/apu/xma_decoder.cc:260`)** (audio register path).

**Back to the Future: distinct - a `condition_variable::__do_timed_wait` /
`__safe_nanosecond_cast` path** (threading/timing wait), fires at boot.

So the true shared cluster is Gears 3 + Judgment only; Gears 1 and BttF are
separate asserts. This narrows the earlier "one shared native crash" claim.

Caveat: `addr2line -i` attribution on optimized builds can name an inlined
neighbor; treat the line numbers as strong leads to verify by reading source.

## Next (iter 2)

- Read `shader_interpreter.cc:866` and the ucode accessors around `ucode.h:715`
  / `:1872`; find the firing assertion and the unexpected opcode/operand.
- Reproduce on Gears 3 with a capture; decide a safe handling (clamp/skip/log the
  unhandled case) vs a real fix; verify the Gears cluster advances on-device.
