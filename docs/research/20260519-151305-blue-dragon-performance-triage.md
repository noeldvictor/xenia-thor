# Blue Dragon Performance Triage

Date: 2026-05-19 15:13 EDT

## Question

Blue Dragon now reaches the opening scene on AYN Thor, but it feels around
single-digit FPS. Is the bottleneck GPU or CPU, and what is the fastest path to
make it less painful?

## Evidence

Relevant captures:

- `scratch/thor-debug/20260519-144529-*`: traced/debug-heavy run reached the
  opening scene after title and language selection.
- `scratch/thor-debug/20260519-145818-*`: same live process later showed Shu
  rendered in-engine.
- `scratch/thor-debug/20260519-150037-*`: same live process still advanced with
  opening credits over the rendered scene.
- `scratch/thor-debug/20260519-150821-*`: trace-off clean sample reached the
  opening sequence and remained alive.

The attempted 300 second run `scratch/thor-debug/20260519-150129-*` is not a
valid FPS sample. It started a new emulator process, then Android force-stopped
that process and relaunched `LauncherActivity` around `15:01:36`.

Live thread sample from the trace-off process `25646`:

```text
Main Thread (F8...)  81.4% CPU
XMA Decoder          44.4% CPU
GPU Commands         14.8% CPU
Draw Thread           3.7% CPU
Audio Worker          3.7% CPU
```

GPU/Vulkan evidence:

- Vulkan device is Adreno 740.
- Swapchain creation succeeds.
- No `VK_ERROR_DEVICE_LOST`, GPU-hung, native fatal signal, or AndroidRuntime
  crash lines were found in the trace-off sample.
- The GPU command thread is active but not the top CPU consumer.

## Diagnosis

The current slowdown is primarily CPU-side emulator work, not an Adreno
rendering failure.

The GPU is rendering real frames through Vulkan, but the host CPU still has to
run Xbox 360 PowerPC guest code, Xenia kernel/HLE, file IO, audio/XMA, command
processor work, and texture/render-target management. The active A64 backend is
still a research import, not a tuned production-grade ARM64 JIT path.

Heavy tracing makes the situation worse. The title-bring-up command lines often
enabled GPU swap traces, draw-state traces, texture-source checksums, shared
memory checksums, and huge logcat output. Those runs are proof runs, not speed
runs.

Even with the trace firehose off, the main emulation thread and XMA decoder are
large CPU consumers, so the real speed sprint is CPU/JIT/audio first, then GPU
cleanup.

## Speed Sprint Order

1. Create a repeatable fast-run lane:
   - no GPU trace/checksum flags
   - no shader dumps
   - no broad log spam
   - only the needed `vulkan_force_signed_2101010_unorm_fallback=true`
   - scripted nop HID sequence for title/language progression

2. Add a tiny perf snapshot helper:
   - capture screenshot and meta
   - sample `top -H` for the Xenia process
   - collect filtered crash/device-lost lines
   - record current command-line cvars

3. Profile the top CPU consumers:
   - main guest CPU thread / A64 backend
   - XMA decoder
   - GPU command processor
   - file IO and logging writer

4. A64 backend speed work:
   - verify block linking and dispatch overhead
   - identify interpreter/helper fallbacks, if any
   - reduce generated-code debug/disassembly work
   - compare hot PPC functions against the donor aX360e/Edge A64 path

5. Audio bring-up speed:
   - test `apu=nop` or a cheaper XMA path during graphics/input bring-up
   - keep real audio as a later correctness pass

6. GPU speed cleanup:
   - remove all trace/checksum flags from speed runs
   - narrow `2_10_10_10` fallback work
   - look for excessive texture uploads/resolve copies
   - only after CPU and logging are controlled, profile Vulkan/Adreno with AGI
     or RenderDoc.

## Current Answer

Stuff is on both CPU and GPU:

- GPU: Adreno/Vulkan is rendering the title, language screen, and opening scene.
- CPU: the performance wall is mostly host CPU emulation/JIT/audio/debug
  overhead.

The most likely near-term win is not a shader tweak. It is a real speed lane:
trace-off runs, thread/perf sampling, XMA/audio shortcut for bring-up, then A64
backend hot-path work.
