# Banjo-Kazooie stalls after its first world frame: two lock protocols on one word

Date: 2026-09-20. Title: Banjo-Kazooie: Nuts & Bolts (4D5307ED). Axis on the paradigm matrix:
CPU ISA and memory model. The a64 backend and the kernel HLE wrote different encodings of
"held" into the same `KSPINLOCK` word.

## Symptom

- The intro renders at 27.7 fps. The first game-world frame renders with the lower half black.
- No new frames after that. The FPS badge reads 0.0. GPU busy is 1 %.
- One thread runs at 69 to 95 % of one core inside `sched_yield` (simpleperf).

## Diagnostic

A stall report in `xeKeKfAcquireSpinLock` (`xboxkrnl_threading.cc`). After two seconds of
spinning, once per call, it logs the lock address, the owner word, the owner's thread id, the
spinner, and the guest link register. Output on the device:

```
SPINLOCK STALL: lock=405143C0 owner_pcr=FF0BD000 owner_tid=00000000
  spinner_tid=00000015 spinner_lr=8266C30C our_pcr=000CD000 r13=00000000000CD000
  words=00000000 00000000 00000000 405142AC (held over 2 s)
```

The game's own critical section around the lock says unheld (count 0, owner 0). The kernel
lock word holds `0xFF0BD000`. No PCR has that value. `owner_tid` is 0 because no thread owns
it.

## Cause

The HLE protocol (`xeKeKfAcquireSpinLock`, `KeTryToAcquireSpinLockAtRaisedIrql_entry`,
`xeKeKfReleaseSpinLock`): held = the owner's PCR (r13) stored big-endian; free = 0.

The a64 inline fast paths (`a64_emitter.cc`, commit `1a34dd7b41` of 2026-05-20, cvar
`a64_inline_kernel_spinlock_exports`, default true):

| export | old inline behavior | HLE behavior |
|---|---|---|
| `KeAcquireSpinLockAtRaisedIrql` | CAS 0 to host value 1 | CAS 0 to big-endian PCR |
| `KeTryToAcquireSpinLockAtRaisedIrql` | CAS 0 to host value 1 | CAS 0 to big-endian PCR |
| `KeReleaseSpinLockFromRaisedIrql` | atomic decrement (`ldaddal -1`) | store 0 with release |

The two protocols agree only when the same side does both halves. Banjo mixes them: the
spinner's own PCR is `0x000CD000`. Stored big-endian the bytes are `00 0C D0 00`, which the host
reads as `0x00D00C00`. The inline release subtracts one: `0x00D00BFF`, bytes `FF 0B D0 00`,
read big-endian as `0xFF0BD000`. That is the value in the log, exactly. The sequence:

1. Thread 0x15 acquires the lock through the HLE (`KfAcquireSpinLock`, the IRQL-changing
   export, which is not inlined). The word holds `BE(0x000CD000)`.
2. It releases through the inlined `KeReleaseSpinLockFromRaisedIrql`. The word holds
   `0xFF0BD000`, which is not zero.
3. It acquires again through the HLE. The CAS from 0 never succeeds. The thread yields forever.

The old inline acquire also stored 1, which the HLE spin path then read as a PCR of
`0x01000000` and dereferenced (`TranslateVirtual<X_KPCR*>`). That is a second latent fault.

## Fix

The inline paths now use the HLE protocol. Acquire and try-acquire load r13 from the context,
byte-swap it, and CAS it into the word (slow path when r13 is 0). Release stores zero with
release semantics (`stlr wzr`). The decrement is gone. The LSE and the exclusive-monitor
variants both changed.

## Why the decrement existed

The 2026-05-20 note (`20260520-a64-context-cache-and-spinlock-fastpaths.md`) does not say.
A decrement is the release of a count, not of an owner word. The x64 backend has no inline
spin-lock path; this was new code for a64 and it was tested on titles where the a64 fast path
did both halves.

## Result of the first fix (spin lock)

Probe after the build (`xenia_probe`, warm cache, 38,104 functions in 11.1 s): intro at
29.6 fps; START at +41 s; the puzzle transition and the game world behind it at +91 s; no
`SPINLOCK STALL` line. The stall is gone. The FPS badge showed 0.0 in the 30 s windows, and
the badge lines with a value show 1 to 4 swaps per 500 ms with gaps of 3 to 6 s, and one burst
of 16 swaps. So the game ran at 2 to 6 fps with pauses, not 0. The probe aborted at a GPU
temperature of 70.5 C.

The log for that run held 1,381,223 lines. 1,330,825 of them were two lines in pairs:
`KeRaiseIrqlToDpcLevel - old_irql > 2` and `KfLowerIrql : new_irql > kpcr->current_irql!`,
13,000 per second. 50,267 were `Texture fetch constant ... has "invalid" type`.

## The second mismatch: the IRQL state

Same class, same file. The inline `KeRaiseIrqlToDpcLevel` and `KfLowerIrql` in
`a64_emitter.cc` swapped a private word, `Processor::irql_` (through
`A64BackendContext::processor_irql`). Nothing else reads that word. The HLE
(`KeRaiseIrqlToDpcLevel_entry`, `xeKfLowerIrql`, `xeKfRaiseIrql`), the kernel's DPC
impersonation (`kernel_state.cc`), the APC delivery gate (`current_irql >= 1` masks APCs in
`xboxkrnl_threading.cc`), `XObject::Wait` (`wait_irql`), and the guest's own code use
`X_KPCR::current_irql`, the byte at `0x18(r13)`.

Consequences:

- An inline raise leaves the KPCR byte unchanged. APCs deliver at what the guest believes is
  DPC level.
- An HLE raise followed by an inline lower leaves the KPCR byte at 2. APCs are masked until
  some HLE lower runs. Threads that wait for an APC-driven completion wait longer or forever.
  This is the shape of the Gears stall (five threads wait on an event the HLE never signals);
  not tested there yet.
- Every HLE raise or lower on a thread whose byte is stale logs an error line. Two lines per
  pair, 13,000 per second, each a write to logcat from the guest thread.

Fix: the inline raise and lower read and write the KPCR byte with `ldrb` and `strb` through
r13. The KPCR is per guest thread, so the plain byte access is the HLE's own sequence. The
APC guard around the inline lower is unchanged. The two HLE error lines print 16 times, then
once in 65,536, with the values and the occurrence count. The texture fetch warning prints
16 times, then once in 4,096.

## Rule

An inline fast path in the backend must read and write the same state, in the same encoding,
as the HLE it replaces. The check: for each inlined export, name the memory the HLE touches
and the value it stores, then read the emitter. Two exports failed that check today. The
remaining inlined exports (`RtlEnterCriticalSection`, `RtlLeaveCriticalSection`,
`RtlTryEnterCriticalSection`, `KeEnterCriticalRegion`, `KeLeaveCriticalRegion`) use the guest
structure fields with the guest byte order; they pass.

## Shader compilation was the 2 fps (measured 2026-09-20, evening)

Profile in the game world (`xenia_profile`, 15 s): the command processor thread was the largest
thread at 23.9 % of all samples; 74.5 % of that thread sat inside `libvulkan_freedreno.so`
(spread over hundreds of small symbols), 12.9 % in malloc, 9 % in xenia (`spv::Builder`,
`LoadShader`, `ConfigurePipeline`, the shader interpreter of the draw extent estimator). That
is pipeline compilation. The GPU was 4 % busy. Three guest threads spun at 67 % each in the
game's own wait function `0x8264DCD0` (a timed poll with a 5,000-tick window), which is the
game waiting for the GPU.

The counters added the same day: 320 pipelines in the first 100 s of the world, 54.5 s spent in
`vkCreateGraphicsPipelines`, 170 ms per pipeline on average, batches of 64 at 6 to 18 s.

The VkPipelineCache blob was saved only in `Shutdown`, which a force-stop never reaches. No
blob ever existed on the device. With the periodic save (20 s after 16 new pipelines, written by
a detached thread, renamed over the old file) the first run wrote 28.1 MB for 320 pipelines
(88 KB each). The second run seeded from it: the same 320 pipelines took 5 ms in total, and the
badge read 61 to 63 fps in the puzzle transition and the game's dialog (the earlier runs read
0.0 there). Mesa's own disk cache (`MESA_SHADER_CACHE_DIR`) created its directory but wrote no
files: this Turnip build has no disk cache, so the VkPipelineCache blob is the only layer.

Open: 88 KB per pipeline means every pipeline carries its own shader binaries. The state that
xenia bakes into pipelines (blend, depth, stencil, cull) could be dynamic state on Turnip
(`VK_EXT_extended_dynamic_state3`) or graphics pipeline libraries, which would cut both the
count and the first-visit cost. Next after the loader race below.

## The loader race (open)

Guest thread 0x1C (Banjo's loader) is not deterministic across runs: it reached the world at
22:21 and 23:06; it read `\debug\db_index.txt`, passed null critical sections, and made a wild
access at 22:56 (a run with a different route timing); it called `XamShowDirtyDiscErrorUI` at
+21 s, before any input, at 23:09. The content check that fails is cryptographic and the fork
stubs it to success (`xboxkrnl_crypt.cc`), so the failing check is elsewhere. The probe now
saves the in-process log ring before the force-stop so the loader's file trace before the call
is kept.

## The dirty-disc dialog: the profile never applied, and the cache device was not mounted

Two findings from the run at 23:31, the first with Banjo's game profile active:

1. The launcher guessed the title id from the file name through the games database and got
   `58410954`, the XBLA Banjo-Kazooie, for the Nuts & Bolts disc. So `GameProfiles` never
   applied: no 30 fps cap (the 60 fps cap let the GPU reach 70 C within 45 s of the puzzle
   transition), no XUI font cache redirect, no forced NtReadFile completion (the fork's own
   fix for the async `undle` verify that ends in the dirty-disc dialog). Fix: the emulator
   records the id it read from the disc per launch target (`files/title_ids.properties`) and
   the launcher uses it next time. With the profile: 29.7 fps capped, GPU 54 C for 200 s.
2. With the redirect active the log says `ResolvePath(cache:) failed - device not found`.
   `mount_cache` is an Android code default (true) since 2026-09-18, but the device's
   `xenia.config.toml` from an older build pinned it false, and the file wins over a compiled
   default. `SaveConfig` writes every cvar with its current value, so a file from an older build
   pins every old default forever: the 18 Android code defaults of 2026-09-18 were all dead on
   this device. Fix: Android writes the file for inspection and never reads it. The menu
   toggles, the game profiles, and the launch extras are the control surface (directive 17);
   the in-app MCP sets a cvar live for diagnosis.

The dirty-disc dialog came twice in two runs with the redirect active and unmounted cache, and
once in four without the profile. The run after both fixes is the test.
