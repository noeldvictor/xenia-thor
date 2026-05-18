# Blue Dragon Swap Trace Checkpoint

- Date: 2026-05-18 14:07 EDT
- Branch: `master`
- Base head: `e157abe6c`
- APK SHA-256: `F32E2E8888B9EF5CBDC7E654B1716B384F96C3D3EED5BF6181D85955C8334D85`
- Device: AYN Thor Max, ADB serial `c3ca0370`
- Target: `/storage/2664-21DE/roms/xbox360/Blue Dragon.m3u/Blue Dragon (USA, Europe) (En,Fr) (Disc 1).iso`

## Runs

- Baseline swap trace:
  - `scratch/thor-debug/20260518-135816-logcat.txt`
  - `scratch/thor-debug/20260518-135816-screenshot.png`
  - Launch flags included `gpu_trace_swap=true`.
- Ring-idle interrupt experiment:
  - `scratch/thor-debug/20260518-140109-logcat.txt`
  - `scratch/thor-debug/20260518-140109-screenshot.png`
  - Launch flags included `gpu_trace_swap=true` and
    `gpu_interrupt_on_ring_idle=true`.

## Findings

- The explicit runtime swap trace reached:
  - `VdInitializeEngines`
  - `VdSetSystemCommandBufferGpuIdentifierAddress`
  - `VdInitializeRingBuffer`
  - `VdEnableRingBufferRPtrWriteBack`
- The explicit runtime swap trace did not show:
  - `GPU swap trace: VdGetSystemCommandBuffer`
  - `GPU swap trace: VdInitializeScalerCommandBuffer`
  - `GPU swap trace: VdSwap`
  - `GPU swap trace: XE_SWAP`
  - `GPU swap trace: Vulkan IssueSwap`
- Import/symbol listings still mention `VdSwap`; those lines are not runtime
  call proof. Runtime proof requires the `GPU swap trace:` prefix from the new
  kernel/GPU instrumentation.
- Both runs still hit Blue Dragon's D3D frame-0 watchdog:
  - `The GPU is hung! D3D version is 3529.0 retail, kernel is 65535, frame is 0`
  - Ring pointers were drained and matched at `0x1f/0x1f`, then `0x25/0x25`.
- Enabling the current ring-idle interrupt experiment did not move the runtime
  path to `VdSwap` or `XE_SWAP`.

## Decision

Stop treating Blue Dragon as the only unit test for the ARM64 port. The latest
logs are useful, but the ARM64 backend still lacks broad x64-grade backend
infrastructure and relies heavily on helper calls. The next faster loop is a
batched x64-to-ARM64 conversion audit plus broad first-pass backend work, then
Thor as a milestone checkpoint.
