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

## 2026-09-21 01:45: the dialog, what is known and what is next

Known, with evidence:

- The dialog is the game's reaction to a failed resource lookup, not to a read error. The
  provider search `0x826EFC20` (a critical section around a vector of six provider objects,
  types 00, 01, 02, 03, 81, 83) returns no provider for a request whose type string is
  "texture" (request `+0x3C` -> "texture", `+0x20` = 00030006). The completion callback
  `0x82273078` reads status 2 at `request+0xC`, finds `context+0x138 == 0` (no fallback), and
  calls `XamShowDirtyDiscErrorUI`. The status 2 is set at `0x826F6A34` in the op's method
  `0x826F69F8` when the provider Open (vtable+0x14) left `op+0x1C` and `op+0x20` at 0.
  Trap data: `trap RtlEnterCriticalSection lr=826EFC54` on the device.
- It reproduces on the PC (x64, Windows) in every run; the canary oracle passes. The loader
  thread's kernel call sequence matches canary's through the header read of
  `undle(85374` (open, XctdCompression query, close, reopen, NetworkOpenInformation,
  two reads with a notification event, PENDING both times). The bytes read are the image's
  bytes (FNV hash equal to `tools/pc/gdfx_read.py`). The eight frontend fast-path cvars off,
  the handle cache off, and the cache mount off do not change it. The suppression cvar leads
  to the game's own exit (`cc45b9efc3` said so in June).
- On the device the dialog was rare (1 in 4) before the profile applied and certain after.
  The difference is timing (30 fps cap) or the font-cache redirect; not resolved.

Next, in this order:

1. Finish the PC bisect between `db92dc6327` and now in `../xenia-thor-bisect`. The June
   tree needs two premake fixes (done: the demo exclusions) and one more link fix
   (`GetWindowedAppCreator` in the app). Each step is a Windows build (3 to 10 min) and a
   60 s run; the failing commit names the subsystem.
2. If the bisect points at the CPU frontend, run the PPC corpus on that commit.
3. If it points at the kernel, diff that file against edge and port the fix.

The device trap and the PC oracle are in place, so each of these steps is minutes, not a
device run.

## 2026-09-21 03:00: the PC bisect names the x64 NaN helper

`git bisect run tools/pc/bisect_banjo.sh` over the 1,152 commits since June 26 (nine builds of
the Windows app in `../xenia-thor-bisect`, 70 s of Banjo each, 55 minutes in all):

| verdict | commit | date |
|---|---|---|
| good | `0a428a74f9` | 2026-07-31 |
| good | `a9883e3f4d` | 2026-08-14 (after the Edge kernel port) |
| good | `f30e89ce7d`, `61eda233cd`, `131521989b`, `2c4996cfc1` | 2026-08-18 |
| **bad** | **`2edd685258` Give x64 the PPC positive default QNaN for generated NaNs** | 2026-08-18 |
| bad | `6db6a91364`, `b5e604c01a`, `6935b6cdff` | later |

The helper `EmitScalarFpWithPpcDefaultNan` used `xmm0` and `xmm1` as scratch for the ordered
mask before the op. `EmitCommutativeBinaryXmmOp` and `EmitAssociativeBinaryXmmOp` stage a
constant operand in `xmm0`, so every scalar `x op constant` on x64 computed `x op mask`. The
PPC corpus uses register operands and reported zero regressions. Fixed in `943c14d15f`: scratch
`xmm1/xmm2`, and a 32-bit mask test for single precision (`vcmpordss` leaves bits 32 to 127 as
`src1`'s). Two PC runs after the fix: no dialog, the game continues past the bundle opens.

So the June note was half right: it is shared code in the sense that the same game path fails,
but the PC cause is x64-only. The device (a64 and LLVM) fails at a later point (after the full
read of `undlefǯc4b`, not after the header) and only some of the time, so it has its own
cause. The PC is now a passing oracle with the same kernel, which makes the device diff a
backend question, not a kernel one.

## 2026-09-21 03:30: the device cause is in the a64 backend (and LLVM), same game path

- With the LLVM backend off (a64 for every function) the dialog comes at +11 s, every run.
  With LLVM on it comes at +20 s. So both backends fail the same guest computation.
- The PC (x64, fixed) and the device run the same kernel calls up to the third large-page
  allocation after the full read of `undlefǯc4b` (`NtAllocateVirtualMemory(0,
  0x18E440, 0x60801000) = 46B40000` on both). Then the PC opens `\debug	\cf\84f` and
  continues; the device calls `XamShowDirtyDiscErrorUI` with no kernel call in between. The
  difference is inside guest code that runs after that allocation: the bundle's index build
  or resource lookup. On x64 the same path failed on `x op constant` scalar float math, so
  the code is float-sensitive.
- The three Android-only CPU levers (`arm64_jit_inline_extern_thunk`, `cpu_aot_maximize`,
  `cpu_drop_redundant_atomic_release_barrier`) are not float levers.

Next (bounded): name the guest functions between the last allocation and the dialog with the
trap (`trap NtAllocateVirtualMemory pause=1` on the last hit, then `disasm` from the recorded
lr and stack), scan them for the a64 corpus's failing instruction classes (3,038 failures,
one class is 94 %), and compare the function's results between the PC and the device with
`cpu_backend_llvm_trace_addr` / `arm64_compiled_call_trace_returns`. The PC is the oracle.
