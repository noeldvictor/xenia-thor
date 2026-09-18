# Upstream triage, 2026-09-18

Goal: find upstream changes that make xenia-thor more stable or faster on the AYN Thor.
Method: four read-only scans, one per source. Each scan bucketed commits by area, read the diffs that
matter on the device, ran `git show <hash> | git apply --check` against master, and checked whether the
fix is already in our tree. The four per-source reports are appended to this note in full.

## Sources and counts

| source | range | scanned | out of scope | already in tree | kept |
|---|---|---|---|---|---|
| xenia-edge `edge/edge` | `01efb80ed..edge/edge`, no merges | 298 | 87 | 8 | 69 |
| xenia-canary `canary_experimental` | `master..canary`, since fork base `95a5c3ee2` | 950 | 221 | 219 | 52 |
| xenia-canary open pull requests | all open on 2026-09-18 | 60 | 26 | 3 | 16 |
| forks: Wunkolo, xenia-mac, IhorHarayda, XenDroid branches | unique commits vs canary and edge | 398 | 170 | 147 | 7 |
| xenia-project `master` | `master..upstream/master` | 0 | | | 0 |
| XenDroid `main`, author rfandango | `0b1120187..xendroid/main` | 0 | | | 0 |

New sweep marks for the next scan: edge `1689b263d3` (2026-09-18), canary `aee0871dd7` (2026-09-16),
XenDroid main `0b1120187` (unchanged).

Facts that shape every port:

- Our kernel is a 3-way port of the edge kernel as of 2026-08-03. Kernel patches conflict on context,
  not on design. Diff against the edge tip file, do not apply the patch.
- Our a64 backend is the canary donor of 2026-05-18 (Herman S., xbyak_aarch64). It is not Wunkolo's
  oaknut backend. Wunkolo and xenia-mac patches port by function name only.
- Our threading_posix.cc has 300 changed lines. Port POSIX fixes one at a time and test suspend, resume,
  and save state after each.
- Our XMA decoder is the master-lineage `xma_context.cc`. Canary's new-decoder fixes port by idea only.
- `guest_scheduler` is off in our tree. The 24 edge scheduler commits are skipped as a block.
- Only 2 of the 60 PRs apply to the fork base. PR conflicts are upstream drift, not our churn.

## Tier 1: stability, applies clean or nearly clean. Apply first.

| # | hash | source | subject | why |
|---|---|---|---|---|
| 1 | 3494b56a0b + e88555c35c | edge, canary | Saturate WinSystemClock to steady_clock; fire timers with past due time | A timer armed with due time 0 lands two centuries out and never fires. Silent audio or hang. Both clean. |
| 2 | PR 981 | canary PR | Mutex for UserProfile properties vector | Unlocked vector read and push from several guest threads. Use after realloc. Crash in Poker Smash. 16 lines, clean. |
| 3 | 4aeb518c9c | edge, canary | Clamp scalar maxas/maxasf a0 to [0,255] | Negative a0 reads the constant array out of bounds in the shader. GPU fault risk on Adreno. Clean. |
| 4 | 1fdbe569e4 | canary | Barrier consecutive uploads to the same image | No TRANSFER_WRITE to TRANSFER_WRITE barrier. A tiler can reorder the copies and show stale texels. Clean. |
| 5 | a5f59a48ae | edge | LVR aligned-address early-out | The sequence reads 16 bytes one past the last mapped page and faults. Clean. |
| 6 | 915532b832 | edge | Swapped stage and access masks on scratch buffer growth | Wrong barrier after growth. 2 lines, clean. |
| 7 | 72ce130971 | canary | dcbz clears 128 bytes | Ours clears 32. Xenon has no 32-byte line. Code that relies on dcbz sees garbage. Clean. |
| 8 | f73c521230 | edge | Fix identifier pasting in cvar update macros | Prerequisite for any port that uses UPDATE_from_bool. Clean. |
| 9 | 92ada8ebc0 | canary | Scalar ALU swizzle with three-source vector ops | Reads .wz not .wx for the second source. Wrong shading. Clean. |
| 10 | f3e42609a2 | canary | Float7e3To32 mantissa shift | Shift is 3, must be 16. CPU-side texture unpack. Clean. |
| 11 | 9369464396, fb76cbb291 | canary | Enable vsubcuw; implement mcrxr | Both abort translation of the whole function today. Clean. |
| 12 | 566e6c5866 hunk 1 | xenia-mac | Pass the real length to DeallocFixed at heap dispose | munmap with length 0 fails with EINVAL. 1 line, clean. |
| 13 | 79feeb9095 | edge | Align guest system time with host time at setup | Guest clock runs ahead by the startup delay. Clean. |
| 14 | 211e7d9531 + 33f3a47ce2 | edge | Refresh host entry timestamps and size after a write | Stale size after write. 8 lines, clean. |
| 15 | 16ba848086 | edge | Case-insensitive content package match | Android storage is case-sensitive. Package not found. Clean. |
| 16 | 2f6baa7514 | edge | Create the content directory XamContentResolve asks for | In-game DLC install fails with PATH_NOT_FOUND. Clean. |
| 17 | 037eef5403 | edge | Resolve a content root named without its trailing separator | Every open under such a link fails. Clean. |
| 18 | ed0e06112c | edge | Hide the package thumbnail from guest enumeration | A title can load the thumbnail as a save. Clean. |
| 19 | 0da2a16134 | edge | allow_incompatible_title_update default off, log when applied | Stops a mismatched TU applied in silence. Clean. |
| 20 | 2d5b41080c, 77597d62ae, 53061c63fe, 3eab2b8b39, 24f90c0efc, bea885689d | edge, canary | Stacked-texture layer clamp; back-face stencil select; alpha blend factor map; tessellated strips and fans; stencil transfer discard; constant index clamp | Small GPU correctness fixes. All clean. |
| 21 | fdd583eced | canary | resolve_fast_32bpp_4xmsaa sample addressing | Samples 2 and 3 read the wrong dword. Clean. Needs a shader rebuild. |
| 22 | 1a53f261f7 | canary | Arena chunk 4 MiB to 8 MiB | Fewer chunk allocations during JIT. 1 line, clean. |

## Tier 2: stability, manual port. Highest value in the whole scan.

| # | hash | source | subject | why | size |
|---|---|---|---|---|---|
| 1 | 8ffe24e372 | edge, canary | Publish JIT entry results under the entry table lock | ResolveFunction stores function and status with plain stores. On ARM64 a reader can see STATUS_READY with a stale function pointer and jump through it. Our lock-free lookup cache needs the same release order. | small |
| 2 | 7dab7132b5 | edge | Move overlapping XEX patch regions with memmove | aarch64 memcpy copies forward with no overlap check. A title update delta patch comes out periodic. Boot crash with TUs. Our lzx.cc and xex_module.cc still use memcpy. | small |
| 3 | 2cdfb45cbf | edge | Revert the LDR + REV byte-swap detection in MMIO decode | Our port `159be60a3b` sets byte_swap for LDR+REV but resumes at pc+4, so the REV still runs. Every 32-bit MMIO load through the fault path reaches the guest byte-reversed on a64. Contradicts our own port commit. Verify on device with one XMA or GPU register read before and after. | small |
| 4 | 3fbbc60f9b | edge | Lock symbolic link lookups | FindSymbolicLink walks `symlinks_` unlocked while mounts and disc swaps mutate it. | 2 lines |
| 5 | 73388a080f + d8348b4f68 | edge, canary | POSIX thread suspend, join, terminate, affinity | Double pthread_join; Terminate on self spins in `for (;;)`; int-shift affinity masks are UB from bit 31; stale suspend token. Port the edge tip file state. | medium |
| 6 | PR 1183 | canary PR | Wake an alertable wait on a queued user callback | On POSIX the APC runs in the signal handler and the wait resumes. APCs deliver only when the wait ends by itself. Stalls. Port by hand, about 100 lines. | medium |
| 7 | 174d2d4205 | canary | Robust mutexes | A terminated guest thread that held a wait-object mutex deadlocks every later waiter. No PTHREAD_MUTEX_ROBUST in our tree. | small |
| 8 | c9eba5daf8 | canary | SA_RESTART on suspend and terminate signals | A suspend signal makes blocking syscalls in the target thread fail with EINTR. Port the SA_RESTART line and the atomic install flag. | small |
| 9 | 75b54532c8 | edge | Reuse one APC per timer | Each expiry allocates an XAPC freed only on delivery. A thread that waits non-alertable on its own periodic timer leaks until the system heap is empty. | medium |
| 10 | 54fd4a3078 + dcf2994ea1 | edge | Reconcile the guest dispatch header on native object lookup | Titles inline KeResetEvent as a plain store. The host primitive stays signaled. Next wait returns at once. Fixes musou crashes and a Crackdown stall. The two scans disagree on whether the XEvent part is present. Verify first. | medium |
| 11 | e6a86116f3 | edge | Keep a constant vector operand off the clobbered scratch | VECTOR_ROTATE_LEFT, DENORMFLUSH, SHL, SHR, DOT_PRODUCT rebuild v0 to v3 before the last read. Wrong vector results on a64. | small |
| 12 | 955e1d1723 | edge | Read SVOD fragments at explicit offsets | Seek plus fread on one FILE* shared by every file in the disc image, no lock. Two threads get each other's bytes. Drop the scheduler pool part. | small |
| 13 | 6f9840568b (edge c9536890c9) | canary, edge | Set O_APPEND only for append-only handles | Linux pwrite ignores the offset on an O_APPEND fd. Every positioned write on a save opened append+write lands at EOF. Save corruption. | small |
| 14 | PR 1226 | canary PR | Bounds check the STFS reader | A damaged DLC or save package reads past the mapping and crashes in memcpy at mount. STFS part is about 50 lines. | small |
| 15 | PR 1182 | canary PR | Alignment phase for offset-translated physical heaps | Any alignment above 4 KiB is off by 0x1000 through the 0xE0000000 heap. MmAllocatePhysicalMemoryEx fails every time. Port the phase into our loop-style AllocRange. | small |
| 16 | PR 1225 | canary PR | Clear the guest signature when an object dies | A reissued handle follows a stale stamp to an unrelated object. Guitar Hero 5 disc read error. Port the destructor part, about 20 lines. | small |
| 17 | 3e4a82b60d | edge | Close a content package's files by its device path | When the root link lookup fails the prefix is "" and every open handle in the title is released. | small |
| 18 | 93adb2bb95 | canary | Handle cvar type mismatches | A wrong-typed TOML value throws bad_optional_access and aborts at startup. Users edit the config on device. | small |
| 19 | 87c2411270 | edge | Round the AllocRange ceiling to the page, not the alignment | An allocation can land above the ceiling; align near UINT32_MAX wraps to 0. | small |
| 20 | c87c034153 | edge | Run the pending stack sync check before the a64 epilog | A guest call as the last instruction leaves the sync flag set and a longjmp escapes the stack sync. 5 lines. | tiny |
| 21 | 577fb8ee17 | edge, canary | Fix mcrf | StoreCR(crfD, LoadCR(crfS)) writes zeros whenever crfD != crfS. Compilers emit mcrf between fields. | small |
| 22 | 7d8ac86f6c, f4af1e2a77 | canary | Do not fail translation on OE-form ALU ops; implement mcrfs | A return of 1 fails translation of the whole function and the title aborts. Take the mcrfs hunk only. | small |
| 23 | 5ce7c336df | edge | Flip the lane index in the constant Extract fold | Wrong constant when a vector constant feeds EXTRACT. Fixed FIFA and State of Decay upstream. The two scans disagree on whether our fold exists. Verify. | small |
| 24 | 7ef873b0d5 | canary | Null-check the audio client driver after the lock | Worker thread and UnregisterClient race on the driver pointer. | tiny |
| 25 | 8e54f33d2b | xenia-mac | Guard the temp RW protect in NtAllocateVirtualMemory | If mprotect fails, Zero() writes a read-only page and the process takes SIGSEGV. 8 lines. | tiny |
| 26 | 672bc8b81a | edge | Keep loader-owned objects out of the title's handle numbering | Burnout Paradise indexes arrays by handle and overwrites its own globals. | medium |
| 27 | 1689b263d3 | edge | Strip `\??\` from symbolic link names | `\??\X:` opens fail with NAME_INVALID. | small |
| 28 | PR 1232 | canary PR | Classify directory entries from stat, not d_type | d_type is DT_UNKNOWN on FUSE. Directories vanish. Value depends on whether the content path is on FUSE. Test with a 5-line probe first. | small |
| 29 | 763b160c7a | canary | Fall back to nice values when SCHED_FIFO is refused | Android refuses SCHED_FIFO to apps, so set_priority does nothing. Audio and GPU threads get real priority. | small |
| 30 | 1ae82023ea | canary | WaitMultiple: poll with try_lock | A thread suspended between lock and wait blocks every other waiter (issue #1677). Larger behavior change. Test with save-state suspend. Port last of the POSIX set. | medium |

## Tier 3: speed, manual port

| # | hash | source | subject | why | size |
|---|---|---|---|---|---|
| 1 | 10ac2f5eff + edge IsPossibleMMIOInstruction + record_mmio_access_exceptions cvar | edge | Record MMIO faults on a64 and gate the 0xE0000000 host offset | Our IsPossibleMMIOInstruction is stubbed to false. Every guest MMIO access on a64 takes a SIGSEGV round trip every time. Needs edge's accessed_mmio flags. | medium |
| 2 | 29fcaeac32 | edge | Publish the ring read pointer every RB_BLKSZ dwords | A WAIT_REG_MEM mid-burst stalls the guest that waits for ring space we already consumed. Relevant to the Gears stall. We have a partial cvar-gated publish. | small |
| 3 | 52297ea8f8 + f21ebd49e9 | canary | Futex fast mutex for global_critical_region | Kernel exports take this lock on every call. Drops the per-lock gettid syscall. Keep our owner instrumentation. | medium |
| 4 | XenDroid 773efcdd05 + 81cfbe17c9 | XenDroid feat/vrs | VRS: emit the rate only on change; clamp the rate to device support at the pass sample count | We set the rate on every draw, about 890 per heavy pass. Our cvar text claims the clamp but no code exists. An unsupported rate is a valid-usage violation. About 110 lines. | small |
| 5 | 77852914ff | canary | Texture cache lockless pre-check | LoadTexturesData takes the global lock per draw even with nothing outdated. | small |
| 6 | bfca9b2d97, 7101021150, b308af27c7 | edge | Dynamic uniform buffers for guest constants; persistent shared memory binding; cached host viewport | Fewer descriptor writes and calls per draw on a mobile driver. Check overlap with our 5 UNIFORM_BUFFER_DYNAMIC uses first. | medium |
| 7 | 5845f3437b, ccf8fb66f5, 64e51c544e, 1b242658e5 | canary | Async shader compilation with placeholder pipelines and parallel startup translation | Largest stutter reduction available. About 2,600 lines into a file where we changed 9,019. Manual port, in that order. | large |
| 8 | 70ac6a3574 | edge | Rematerialize spills from the context | Fewer spill stores in large functions. | small |
| 9 | b4a5072505 | edge | Simplify the carry and srawx helpers | Fewer HIR ops for every subfc, adde, srawx. Corpus passes on a64. | small |
| 10 | fb1c3dec3f | edge | CR6 with native vector tests (uminv/umaxv) | CR6 is a recorded hot cost. A different micro-shape measured slower. A/B on device. | medium |
| 11 | d2c8ca675e, e644b0e6c9 | canary, Wunkolo | Bitmask immediates for AND/OR/XOR; USHR immediate for splat-constant vector shifts | One instruction saved per masked op; 3 saved per constant vector shift. Unmeasured. | small |
| 12 | 6e5b8324f4 | canary | Pace the audio subsystem on a 5.333 ms deadline | Behavior change in a master-lineage file with an Android driver. Test on device before keeping. | medium |

## Tier 4: GPU rendering correctness, manual port

9781a75a22 after 1e23aaf4fc (stale texture after invalidation); a092551735 with ac00d09244 and the
gpu_allow_invalid_upload_range cvar (upload range bounds); 9e9d3cdd3f (vertex fetch OOB reads 0);
e519d59e40 (stacked-texture lerp base); 878de4c352 (sampler min and mip filters); 0d38781760 (float
controls on GS); 3cc741caa3 (Inf/NaN through ReduceFloatPrecision); 2b071d9b09 (clip plane invalidation);
9da693480d (EDRAM bits for color/depth aliases); 3a44f20c7b (transcendental rounding, Ace Combat 6);
0c843efb32 (round unsigned fixed fetches); cb240560df (2x MSAA alpha-to-coverage); 3ff230d23b (extended
float16 in RT pack); e20f26963f (register reset defaults); 2eea146b1b (k_16_16 EDRAM packing clamp);
ec5e0f40e7 (dummy texture swizzles); 8f55b4abf7 (gamma RTs as UNORM16); 067641668f (min/max blend
factors); 7cd47947b0 (tfetch1D layouts); e6bdb0fdf7 (skip surface_pitch 0); 66779fb873 (adaptive
tessellation); 2f858c4629 (XMP notifications, Black Ops II music); f213cc6a9f (HID device-type mismatch);
7be830a2dd (deployment type from container); accaef3f07 (collated ListFiles order).

## Ideas only, no patch target

- VEC128 context promotion (canary 01dc16a604, reverted upstream on 09-05): measured 30.8 to 36.4 fps in a
  CPU-bound Lost Odyssey scene. Our ContextPromotionPass is a different pass.
- Precompile `_initterm` callees before the main thread resumes (canary fc22bceae6 series).
- XMA: drain the current frame before Work() exits when both inputs go invalid (NBA Live 06 deadlock);
  count a frame whose header crosses the packet boundary; follow the sub-stream skip chain (LEGO
  freezes); gate output space on subframe_decode_count. Retest those titles first.
- 16 KB page support (acb215696f, ca42e669c9) if the device ever moves to a 16 KB kernel.
- POSIX stack walker for crash dumps (PR 510 idea, not that branch).

## Port procedure

1. One commit per pick, in tier order. Reference the upstream hash in the commit message.
2. Clean picks: `git cherry-pick -x <hash>`. Manual picks: diff against the upstream tip file, write
   the change by hand, cite the hash.
3. After each CPU pick: NDK syntax check of the a64 files. After the whole CPU set: run the PPC corpus
   and record the failure count before and after.
4. After the POSIX set: build NativeCore, deploy, and test suspend, resume, and a save on device.
5. Record each ported hash and each N/A hash in the worklog.

## Port status, 2026-09-18

Result: 90 commits on master after the triage commit `29ad2fcb1c`. The full list is at the end of this section.

Verification on this PC:

| check | result |
|---|---|
| Windows MSVC build of xenia.exe | pass, 394 s |
| Android native core NDK build (arm64-v8a) | pass, 435 s |
| NDK aarch64 syntax check | pass on every changed .cc |
| x64 PPC corpus, 169,117 cases | 14,333 failures after the ports. A matched baseline on the pre-pick CPU sources (`29ad2fcb1c`, same PC, same build) also fails 14,333. The two failure sets are identical: 0 regressions, 0 fixes. The x64 corpus does not cover the a64 sequences or the OE-form, mcrf, mcrfs, mcrxr and vsubcuw paths, which have no corpus files. |
| a64 PPC corpus | not run. Needs the device. See the device list. |

Tier 1: all 22 items landed. 29 clean cherry-picks plus the one-line heap release hunk (`980c63e5a6`). One pick needed a follow-up: the lvlx/lvrx literal-pool commit requires constexpr vec128 factories, ported from edge 2a10971772 (`5d56bb32f9`). The resolve shader headers were regenerated (`6257e2253f`).

Tier 2: 28 of 30 landed. Not applicable with evidence: canary 174d2d4205 (robust mutexes; bionic has no PTHREAD_MUTEX_ROBUST) and canary 1ae82023ea (WaitMultiple try_lock poll; it reintroduces a 1 kHz wakeup per multi-waiter that our per-object condvar work removed). Partial: edge e6a86116f3 landed for SHL_V128 and SHR_V128; the other four sequences were already safe by register arrangement. canary 7ef873b0d5 was not needed: our worker never reads the client driver outside the global lock.

Tier 3: landed 29fcaeac32, b308af27c7, 773efcdd05 (dedupe only), 81cfbe17c9, 77852914ff, 7101021150, 70ac6a3574, b4a5072505, e644b0e6c9 (as our own xbyak sequence). Already present: bfca9b2d97 (our dynamic constants arena), d2c8ca675e (our EmitLogicalImm helpers). Deferred: 52297ea8f8 + f21ebd49e9 (futex global mutex; 29 call sites name the mutex type and 3 use it directly; gain over bionic's pthread mutex is unmeasured), 10ac2f5eff (a64 MMIO fault recording; needs the canary XexInfoCache, the record_mmio_access_exceptions cvar and the memory callback, none of which exist here), fb1c3dec3f (CR6 with uminv/umaxv; a similar micro-shape measured slower, needs a device A/B), 5845f3437b series (async shader compilation, about 2,600 lines into a file with 9,019 changed lines; a session of its own), 6e5b8324f4 (audio pacing; behavior change that needs the device).

Tier 4: landed the SPIR-V and cache half of the list (17 commits from the translator and cache package, plus 2f858c4629, 7be830a2dd as `8dc2265ecc`, accaef3f07 with a7a3eabdbb as `4e9f1c2e8e`, and the last hunk of 9da693480d as `5994a9b5e9`). Not applicable: 3cc741caa3 (our tree had no ReduceFloatPrecision; the 3a44f20c7b port carries the Inf/NaN pass-through), 0c843efb32 (needs the texture integer scale infrastructure from edge d119505289), cb240560df (no FSI_AlphaToMask here), 7cd47947b0 (needs the wide 1D texture base from edge 947075f880), 2b071d9b09 (no user clip plane support in our Vulkan backend), f213cc6a9f (our input system has no per-slot binding design). Not attempted: PR 1015, 1201, 1109, 858 (kernel correctness with heavy conflicts; rank C).

Facts found during the ports:

- Our a64 backend is the canary donor of 2026-05-18 (Herman S., xbyak_aarch64). Wunkolo's oaknut patches never apply.
- The MMIO LDR + REV detection in our port 159be60a3b was wrong. The handler resumes at pc + 4 with length 4, so the REV still ran on a value the handler already swapped. Reverted in `8e1ad97553`. Needs one device read to confirm.
- upper_ascii lowered instead of uppering. Nothing called it before the ListFiles collation.
- Our launch paths never set the deployment type; every title saw kOther.
- The SPIR-V shader storage kVersion is now 8.

Device tests still owed (the Thor was not used in this session):

1. a64 corpus run on device for `3c1b6421de`, `97fdd9f19e`, `8e1ad97553`, `fd3ec51a6b`, `a5f59a48ae`, `702be3b36f`, `eed733d546`. Last a64 count: 3,038 failures.
2. MMIO revert `8e1ad97553`: log one XMA or GPU register read before and after.
3. Dispatch header reconcile `a89c68078f`: a musou title or Crackdown loading.
4. POSIX set `b1d29cc0af`, `50bbb74647`, `a39fb6a3bb`: suspend, resume, save state, and a title that uses NtQueueApcThread.
5. VRS `31f99fa6f6` and `bf11a7b9b1`: Blue Dragon with VRS on; look for the one-shot clamp log line.
6. GPU correctness set: run the PC trace loop first (`tools/pc/bd_trace_ab.sh`), then a Blue Dragon and Gears pixel check on device.
7. Content and VFS set: in-game DLC install, save enumeration, a multi-fragment SVOD.

Commits, newest first:

- `5994a9b5e9` Disable aliased depth only when the color target writes depth bits
- `8e4bd1d3c3` Regenerate the resolve_full SPIR-V headers for the k_16_16 EDRAM clamp
- `316a27a4ba` Bind shared memory persistently for texture loads and resolves
- `c3f07407b0` Skip the texture cache lock when a texture looks up to date
- `12e754437d` Apply blend factors before MIN and MAX blend operations
- `32f998e1ba` Store Vulkan gamma render targets as linear UNORM16
- `97bad24483` Keep read-only depth bound when an aliased color target spares it
- `67575c8bdb` Keep dummy texture headers whose swizzle is all literal 0s and 1s
- `3f3f74e34d` Fix k_16_16 and k_16_16_16_16 EDRAM packing clamping in xesli
- `195b6bf721` Initialize GPU registers to hardware reset defaults
- `2ece4a1456` Emulate extended-range float16 in render target pack and unpack
- `2233a13b05` Round scalar approximation results to 21 mantissa bits
- `a17509e0d8` Apply float controls to Vulkan geometry shaders
- `75b13003fb` Use min_linear and mip_linear for Vulkan sampler min and mip filters
- `c45075e717` Fix stacked-texture inter-layer lerp base in SampleTexture
- `dec32502ed` Clamp out-of-bounds vertex fetch words to 0 in SPIR-V
- `af1fb529f3` Re-validate texture ranges before marking them up to date
- `eb0f40dd87` Return init failure from SharedMemory::InitializeCommon
- `4e9f1c2e8e` Collate ListFiles so directory order is the same on every host
- `bf11a7b9b1` [Vulkan] Record the fragment shading rate only when it changes
- `31f99fa6f6` [Vulkan] Clamp the coarse shading rate to the pass's sample count
- `22db9cf597` [Vulkan] Cache host viewport info across draws
- `bb64dfccdd` [GPU/PM4] Publish the ring read pointer every RB_BLKSZ dwords
- `be3112544d` [Vulkan] Skip draws with surface_pitch == 0 to match D3D12
- `8c9553bde4` [Base/POSIX] Classify directory entries from stat, not d_type
- `840fbe0102` [Base/POSIX] Set O_APPEND only for append-only handles
- `60199b1bf1` [Config] Keep the default when a config value has the wrong type
- `a39fb6a3bb` [Base/POSIX] Fall back to nice values when SCHED_FIFO is refused
- `50bbb74647` [Base/POSIX] Interrupt an alertable wait on a queued user callback
- `1fa1c3dd9a` [Base/POSIX] SA_RESTART on suspend and terminate, atomic install flag
- `b1d29cc0af` [Base/POSIX] Fix thread join, terminate, affinity and stale suspend token
- `9ae1f08cf8` Revert "Revert "[Kernel] Initial XMP Notifications""
- `fd3ec51a6b` a64: emit one immediate shift for splat-constant vector shift counts
- `c5ddeddd76` [Memory] Round the AllocRange ceiling to the page, not to the alignment
- `9154c42ad9` [Memory] Fix large-alignment physical allocs through offset heaps
- `4b42d4167d` [XAM] Close a content package's files by its device path
- `978a019896` [VFS] Bounds check the STFS hash table reader
- `c0cb8c37dd` [VFS] Read SVOD fragments at explicit offsets
- `6e99a4ef3f` [VFS] Lock symbolic link lookups
- `89a8b798b3` Guard temp RW protection during virtual alloc
- `3f15460ea5` Strip \??\ from symbolic link names before using them
- `5f708b1d32` Keep loader-owned objects out of the title's handle numbering
- `7bdc26f0e1` Take back the signature a dying object left in guest memory
- `a89c68078f` Reconcile the guest dispatch header on native object lookup
- `6935b6cdff` Reuse one APC per timer instead of allocating one per expiry
- `eed733d546` Simplify the carry and srawx helpers in the PPC frontend
- `f74856d6b5` [CPU/HIR] Flip the lane index in the constant Extract fold
- `19dee716f2` [CPU/PPC] Implement mcrfs
- `ff755357b7` [CPU] Do not return an error from unimplemented OE overflow checks
- `fd1196626d` [CPU] Fix mcrf: copy the CR field instead of routing it through LoadCR
- `97fdd9f19e` [A64] Run the pending stack sync check before the epilog
- `3c1b6421de` [A64] Keep a constant vector operand off the clobbered scratch in SHL/SHR_V128
- `8e1ad97553` [CPU/A64] Revert the LDR + REV byte-swap detection in MMIO decode
- `20bb5d6ed0` [CPU] Move overlapping XEX patch regions with memmove
- `4b64ffa1b0` [CPU] Publish JIT entry results under the entry table lock
- `49f00de541` CLAUDE.md: add the subagent limit as standing directive 16
- `702be3b36f` Rematerialize spills from the context instead of a local
- `6257e2253f` Regenerate the resolve_fast_32bpp_4xmsaa SPIR-V headers with glslc
- `8dc2265ecc` Set the deployment type in every launch path
- `5d56bb32f9` Make the vec128 factory functions constexpr
- `980c63e5a6` Release each heap region with its real length at dispose
- `7486818064` [VFS] Resolve a content package root named without its trailing separator
- `f36b16c40b` [Emulator] Align guest system time with host time at setup
- `8d67f9a96c` [Base] Fix identifier pasting in the cvar update macros
- `a26289522a` [GPU] Clamp dynamically addressed shader constant indices
- `ce8ad4bcda` [Base] Saturate the WinSystemClock to steady_clock conversion
- `df53b5e714` [VFS/XAM] Create the content directory XamContentResolve is asked for
- `39972f24b7` [XAM] Match a content package that differs only in case
- `dc90d20dc3` [Vulkan] Fix swapped stage and access masks on scratch buffer growth
- `f62c178613` [Kernel] Disable allow_incompatible_title_update by default and log when applied
- `1c9fe32385` [VFS] Refresh host entry timestamps on update
- `35aeacf519` [XboxKrnl/IO] Refresh entry info in NtQueryFullAttributesFile
- `ec913a3afd` [VFS] Hide the package thumbnail from guest enumeration
- `6d9b2a13ee` [Kernel] Fire timers set with a past/epoch absolute due time
- `d9afca652c` [A64] Load the lvlx/lvrx base control from the literal pool
- `bb37fdd9e1` [Vulkan] Select back-face stencil ref/mask when culling only front faces
- `71e8cf7378` [Vulkan] Use dedicated alpha blend factor map for alpha srcb/dest blend
- `067ba43043` [Vulkan] Clamp scalar maxas/maxasf address register to [0, 255]
- `c1102e3894` [GPU] Handle tessellated triangle strip and fan draws
- `8615964989` [Vulkan] Clamp stacked-texture layer index for Inf/NaN coords
- `b5e604c01a` [GPU] Fix scalar ALU swizzles with three-source vector ops
- `3a6e93d1f7` [GPU] Fix mantissa placement in CPU Float7e3To32
- `7d10c1df53` [Vulkan] Barrier consecutive uploads to the same image
- `18835a3b26` [XAM] Add mutex for properties vector
- `8a691c7dce` [CPU] Xbox360 cache line is always 128 bytes
- `8bca68ac24` [Base] Increase arena chunk size
- `3d29e0877b` [GPU/WGF] Fix resolve_fast_32bpp_4xmsaa sample addressing
- `e764e61f03` [Vulkan] Fix stencil bit transfer shaders not discarding pixels
- `778c7f9c5d` [CPU/PPC] Implement mcrxr instruction
- `edf1904a62` [PPC] vsubcuw is actually implemented, enable it

---

# Appendix A: xenia-edge report

# xenia-edge triage: 01efb80ed..edge/edge (2026-08-17 to 2026-09-18)

Scanned 298. Already handled (excluded) 1 (7ea10c4f2d). Out of scope by bucket 87. Evaluated 210. Kept 69 rows (some rows bundle 2 commits).

Facts that shaped the ranking:
- Our kernel is a 3-way port of the edge kernel as of 2026-08-03 (ec4a99586b). Edge kernel commits after that date are new to us.
- `guest_scheduler` defaults to false in our tree (preempt_check_injection_pass.cc:21). Every edge commit that only changes the cooperative fiber path is ranked D.
- Our a64 backend shares code shape with edge's (a5f59a48ae applies clean). Our LLVM backend is the on-device standard per CLAUDE.md; a64 fixes still matter for the a64 path.
- Our XMA decoder is edge's `xma_context_master.cc` shape, not `xma_context_new.cc`. New-decoder fixes do not apply by patch.
- Our Vulkan pipeline cache has no async/placeholder pipelines. Our shader translation has no background claim flag.
- Thor runs 4 KB host pages. 16 KB page work does not apply.
- Apply column: result of `git show <hash> | git apply --check` against master. "conflict" means manual port. "clean" means the patch applies as-is.

## Ranked table

| rank | hash | date | area | subject | what it fixes and why it matters on the Thor | files | our churn | apply | depends on |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 7dab7132b5 | 09-07 | cpu | Move overlapping XEX patch regions with memmove | XEX delta patch copies overlapping ranges with memcpy. aarch64 memcpy copies forward with no overlap check, so a patched image comes out periodic. Boot crash with title updates on arm64 (NG2+TU). Our lzx.cc:165, xex_module.cc:376,491 still use memcpy. | cpu/lzx.cc, cpu/xex_module.cc | lzx unchanged; xex_module +613/-1 | conflict (context only) | none |
| 2 | 8ffe24e372 | 08-17 | cpu | Publish JIT entry results under the entry table lock | ResolveFunction writes entry->function/status with no lock; GetOrCreate reads status under the lock. No happens-before on AArch64, so a thread can see STATUS_READY with a stale function pointer. Our processor.cc:383 has the unlocked write; Entry::status is a plain field. | cpu/entry_table.cc/.h, cpu/processor.cc | entry_table +61/-1; processor +1530/-26 | conflict | note our cpu_lockfree_entry_lookup path also reads status |
| 3 | 2cdfb45cbf | 08-27 | cpu/a64 | Revert the LDR + REV byte-swap detection in MMIO decode | Our mmio_handler.cc:266-328 (port 159be60a3b) sets byte_swap=true for LDR+REV. The handler resumes at LDR+4 (length stays 4), so REV still runs and the value reaches the guest byte-reversed. Our a64 LOAD_I32 emits exactly `ldr dest; rev dest,dest` (a64_seq_memory.cc:629-631). Every 32-bit MMIO load through the fault path is wrong on the a64 backend. Verify on device by logging one XMA/GPU register read. | cpu/mmio_handler.cc | +97/-4 | conflict (small) | none |
| 4 | 3fbbc60f9b | 09-18 | vfs | Lock symbolic link lookups | FindSymbolicLink and ResolveSymbolicLink walk `symlinks_` with no lock while Register/Unregister mutate it under the global lock (our virtual_file_system.cc:68,85 lock; 98-127 do not). Content mounts and disc swaps insert while other guest threads resolve paths. 2 added lines. | vfs/virtual_file_system.cc | +216/-11 | conflict (context only) | none |
| 5 | 73388a080f + d8348b4f68 | 08-27, 09-10 | base/posix | Fix thread suspend, join, terminate and affinity; double join, terminate spin, stale suspend token | Five real defects in our threading_posix.cc: post_execution joins under mutex_ and can deadlock against Terminate's exit tail; a handle waited on twice calls pthread_join twice; Terminate on self spins forever in `for (;;)` (line present); affinity masks use int shifts (`set << i`, `1 << i` present); WaitSuspended trusts one semaphore token so a create_suspended thread's first real Suspend releases at once. NanoSleep truncates sub-microsecond requests to 0. | base/threading_posix.cc | +276/-24 | conflict (both) | take edge tip file state, not the two patches in sequence |
| 6 | 3494b56a0b + e88555c35c | 09-02, 08-21 | base + kernel | Saturate the WinSystemClock to steady_clock conversion; Fire timers set with a past/epoch absolute due time | A guest timer armed with due time 0 (NT epoch) overflows int64 in the steady_clock cast and lands two centuries out. The timer never fires; a title whose audio tick uses it runs silent or hangs. Our chrono_steady_cast.h has no clamp; our xtimer.cc has no past-due clamp. | base/chrono_steady_cast.h, kernel/xtimer.cc | unchanged; +16/-11 | clean (both) | none; either alone fixes it, both is belt and braces |
| 7 | 75b54532c8 | 09-15 | kernel | Reuse one APC per timer instead of allocating one per expiry | Every timer expiry calls xeNtQueueApcThread, which SystemHeapAllocs an XAPC (our xboxkrnl_threading.cc:2121) freed only on delivery. A thread that waits non-alertably on its own periodic timer leaks one per fire until the system heap is exhausted. | kernel/xtimer.cc/.h, xthread.cc/.h, xboxkrnl_threading.cc/.h | xtimer +16/-11; xthread +994/-321; threading +1668/-302 | conflict | none |
| 8 | 54fd4a3078 (rework dcf2994ea1) | 09-05, 09-13 | kernel | Reconcile the guest dispatch header on native object lookup | Titles inline KeResetEvent/KeInitializeEvent as plain stores to signal_state. The host primitive stays signaled and the next wait returns at once. Adds SyncFromGuest in GetNativeObject for event, semaphore, mutant with a kernel-written mirror. Fixes musou crashes and Crackdown loading stall. Our xevent.cc has no mirror. | kernel/xevent, xsemaphore, xmutant, xobject | xevent +61/-5; xsemaphore +48/-5; xmutant +217/-21; xobject +676/-91 | conflict | port edge tip file state (both commits merged there) |
| 9 | e6a86116f3 | 08-18 | cpu/a64 | Keep a constant vector operand off the clobbered scratch | SrcVReg materialises a constant operand into v0-v3, and VECTOR_ROTATE_LEFT_V128, VECTOR_DENORMFLUSH, SHL_V128, SHR_V128 and DOT_PRODUCT_3/4 rebuild those registers before the last read. Wrong vector results. Our a64_seq_vector.cc:792,888 and a64_sequences.cc:3217 have the pre-fix shape. | cpu/backend/a64/a64_seq_vector.cc, a64_sequences.cc | a64 is new since base | conflict (line drift) | none |
| 10 | 955e1d1723 | 09-11 | vfs | Read SVOD fragments at explicit offsets | SvodContainerFile::Read does Seek + fread on a FILE* shared by every file in the container (our svod_container_file.cc:24-28, no lock). Two guest threads reading two files from the same disc image interleave seek and read and get each other's bytes. XFile::file_lock_ is per handle, so it does not cover this. | vfs/devices/xcontent_devices/svod_container_device.cc/.h, svod_container_entry.h, svod_container_file.cc | device +432/-0; file +32/-0 | conflict | drop the supports_concurrent_io part (scheduler pool) |
| 11 | 3e4a82b60d | 09-18 | kernel/xam | Close a content package's files by its device path | CloseOpenedFilesFromContent resolves the root link into a string that stays "" when the lookup fails, and every path starts with "", so it releases every open file handle in the title. Our content_manager.cc:603-609 has this shape. Match on the package mount path instead. | kernel/xam/content_manager.cc/.h | +432/-57 | conflict | our ContentPackage needs a device_path accessor |
| 12 | a5f59a48ae | 08-19 | cpu/a64 | Load the lvlx/lvrx base control from the literal pool | LVR with a 16-byte aligned address must read nothing, but the sequence still loads 16 bytes, which can sit one past the last mapped page and fault. Adds the early-out. Also drops a 4-instruction constant build per LVL/LVR. | cpu/backend/a64/a64_seq_vector.cc | a64 new since base | clean | none |
| 13 | 29fcaeac32 | 08-30 | gpu/pm4 | Publish ring read pointer every RB_BLKSZ dwords not per burst | The guest polls the read-pointer write-back for ring space. We publish only at burst end (our command_processor.cc:1554 loop, plus a cvar-gated publish before INDIRECT_BUFFER at :2378). A WAIT_REG_MEM mid-burst that waits on the guest, while the guest waits for ring space we already consumed, stalls both. CLAUDE.md ties the Gears drain routine to the read pointer. | gpu/command_processor.cc | +1690/-49 | conflict (our pm4 loop is inline) | none |
| 14 | 10ac2f5eff | 09-07 | cpu/a64 | Record MMIO faults on a64 and gate the 0xE0000000 host offset | Our A64Backend has RecordMMIOExceptionForGuestInstruction but never registers it, and IsPossibleMMIOInstruction is stubbed to `return false` (a64_seq_memory.cc:258, since f3b96a7e8b). Every guest MMIO access on the a64 backend takes SIGSEGV every time. Also MMIOAwareLoad/Store add 0x1000 unconditionally for 0xE0000000 (ours :533,549), wrong on a 4 KB host. | cpu/backend/a64/a64_backend.cc, a64_seq_memory.cc | a64 new since base | conflict | needs edge's IsPossibleMMIOInstruction (accessed_mmio flags) and the record_mmio_access_exceptions cvar, absent from our non-x64 cpu tree |
| 15 | fb1c3dec3f | 08-18 | cpu | Reduce CR6 with native vector tests | New HIR opcodes VECTOR_ALL_SET / VECTOR_NONE_SET lowered on a64 as uminv/umaxv + umov + cmn/cmp + cset, replacing the IS_FALSE(NOT(v)) shape UpdateCR6 emits. CLAUDE.md records CR6 as a hot cost. A/B on device; CLAUDE.md warns a different micro-shape measured slower. | cpu/hir/*, ppc_hir_builder.cc, a64_seq_vector.cc, constant_propagation_pass.cc | hir_builder +179/-6 | conflict | none |
| 16 | bfca9b2d97 | 08-28 | gpu/vulkan | Bind the guest constant buffers as dynamic uniform buffers | One descriptor set with dynamic offsets instead of a new set per constant buffer change. Fewer descriptor writes per draw; CPU-side win on a mobile driver. Our command processor already uses UNIFORM_BUFFER_DYNAMIC in 5 places; check overlap first. | gpu/vulkan/vulkan_command_processor.cc/.h | +8781/-238 | conflict | none |
| 17 | 7101021150 | 08-18 | gpu/vulkan | Bind shared memory persistently for texture loads and resolves | Stops re-writing the shared-memory descriptor for every texture load and resolve. CPU win per draw. | gpu/vulkan/vulkan_render_target_cache.cc, vulkan_texture_cache.cc/.h | rt +7126/-245; tex +340/-1 | conflict | none |
| 18 | 70ac6a3574 | 08-19 | cpu | Rematerialize spills from the context instead of a local | A spilled value that still lives in the context is reloaded from the context instead of stored to a local and reloaded. Fewer spill stores in large functions. | cpu/compiler/passes/register_allocation_pass.cc | +475 | conflict | none |
| 19 | b4a5072505 | 08-31 | cpu/ppc | Simplify the carry and srawx helpers | SubDidCarry becomes one CompareUGE; AddWithCarryDidCarry computes the sum once; srawx drops a redundant Min. Fewer HIR ops for every subfc/adde/srawx. Corpus passes on a64. | cpu/ppc/ppc_emit_alu.cc | +189/-4 | conflict | none |
| 20 | b308af27c7 | 08-28 | gpu/vulkan | Cache host viewport info across draws | Skips GetHostViewportInfo when its inputs did not change. Small CPU win per draw. | gpu/vulkan/vulkan_command_processor.cc/.h | +8781/-238 | conflict | none |
| 21 | 577fb8ee17 | 09-04 | cpu/ppc | Fix mcrf: copy the CR field instead of comparing it against zero | Our InstrEmit_mcrf (ppc_emit_control.cc:596-604) does StoreCR(crfD, LoadCR(crfS)). LoadCR/StoreCR bake the field bit position, so the store writes zeros whenever crfD != crfS. Fix uses LoadCRField/StoreCRField, which our builder has. No instr_mcrf.s in our corpus. | cpu/ppc/ppc_emit_control.cc | +185/-2 | conflict (context) | none |
| 22 | 5ce7c336df | 09-13 | cpu/hir | Flip the lane index in the constant Extract fold | Value::Extract folds a constant vector with the guest element number, but the backends flip byte/half lanes. Our value.cc:896-902 has the unflipped fold. Wrong constant when a vector constant feeds EXTRACT. Fixed FIFA and State of Decay rendering upstream. | cpu/hir/value.cc | +165/-45 | conflict (tests dir absent) | none |
| 23 | c87c034153 | 08-29 | cpu/a64 | Run the pending stack sync check before the epilog | A guest call as the last instruction leaves synchronize_stack_on_next_instruction_ set and the epilog never consumes it; a longjmp there escapes the host/guest stack sync. Our a64_emitter.cc epilog at :4246 has no consume. 5 lines. | cpu/backend/a64/a64_emitter.cc | a64 new since base | conflict (context) | none |
| 24 | 87c2411270 (orig 1ad151d124) | 09-07 | memory | Round the AllocRange ceiling to the page, not to the alignment | AllocRange rounds high_address up to the alignment, so an allocation can land above the ceiling the caller asked for, and align() near UINT32_MAX wraps to 0. Our memory.cc:1027 has the align. Same function that broke Far Cry 3/4 and Watch Dogs before. Use the 87c2411270 hunk (keeps the PhysicalHeap guards). | memory.cc | +284/-34 | conflict (test file absent) | none |
| 25 | 672bc8b81a | 09-05 | kernel | Keep loader-owned objects out of the title's handle numbering | Burnout Paradise indexes arrays by (handle - 0xF8000088) >> 2. GetNativeObject wrappers, the executable module and the main thread take guest slots, so its job threads land past the array and it writes over its own globals. Moves them to the host table. | kernel/kernel_state.cc, user_module.cc, util/object_table.cc/.h, xboxkrnl_ob.cc, xevent/xmutant/xsemaphore/xobject/xthread | object_table +304/-59; kernel_state +1029/-198 | conflict | none |
| 26 | c9536890c9 | 08-27 | base/posix | Set O_APPEND only for append-only handles | Linux pwrite ignores the offset on an O_APPEND fd. We set O_APPEND whenever FILE_APPEND_DATA is present (filesystem_posix.cc:184) and write with pwrite (:150). Every positioned write on a save opened with append+write lands at EOF. | base/filesystem_posix.cc | +22/-0 | conflict (context) | none |
| 27 | 16ba848086 | 08-31 | kernel/xam | Match a content package that differs only in case | Android storage is case-sensitive. A package created under one casing and opened under another is not found. Falls back to a case-insensitive match. | kernel/xam/content_manager.cc | +432/-57 | clean | none |
| 28 | 2f6baa7514 | 09-01 | vfs/xam | Create the content directory XamContentResolve is asked for | create_directory argument was ignored; a title that resolves a path then creates the package itself gets PATH_NOT_FOUND. Fixes in-game DLC install in BF3, FM3. | kernel/xam/xam_content.cc, vfs/virtual_file_system.cc | +783/-130; +216/-11 | clean | none |
| 29 | ed0e06112c | 08-23 | vfs | Hide the package thumbnail from guest enumeration | Titles with random save names can enumerate the thumbnail file and load it as the save. | vfs/devices/host_path_device.cc/.h, content_manager.cc | +97/-2 | clean | none |
| 30 | 037eef5403 | 09-18 | vfs | Resolve a content package root named without its trailing separator | A link canonicalised to \Device\Content\N fails to resolve the mount at \Device\Content\N\. Every open under such a link fails. | vfs/virtual_file_system.cc | +216/-11 | clean | none |
| 31 | 1689b263d3 | 09-18 | kernel | Strip \??\ from symbolic link names before using them | \??\X: opens fail with NAME_INVALID; \??\ deletes silently no-op. Fixes a title that repoints GAME: at its update. | kernel/xboxkrnl/xboxkrnl_io.cc, xboxkrnl_ob.cc/.h | io +674/-97; ob +256/-30 | conflict | none |
| 32 | 7be830a2dd | 09-01 | kernel | Take the deployment type from the container content type | GOD/SVOD disc rips reported as HDD, so a title looks for its install in the wrong place. Fixes Watch Dogs from a package. | emulator.cc | +1254/-36 | conflict | none |
| 33 | 2f858c4629 | 08-25 | kernel/apu | Revert "Revert [Kernel] Initial XMP Notifications" | Primes XmpPlaybackControllerChanged once per listener from live state. Black Ops II music fix without the Halo 4 / Borderlands 2 freezes the first version caused. | apu/audio_media_player.h, kernel_state.cc/.h, xam/apps/xmp_app.cc | amp.h +156/-0; xmp_app +427/-303 | clean | none |
| 34 | accaef3f07 | 08-23 | base/vfs | Collate ListFiles so directory order is portable | ext4/f2fs readdir order is hash order. Titles that take the first enumerated entry behave differently than on NTFS. | base/filesystem.cc/.h, filesystem_posix.cc, filesystem_win.cc, svod_container_device.cc | filesystem +113/-0 | conflict | none |
| 35 | f213cc6a9f | 09-12 | hid | Don't treat a device-type mismatch as a controller disconnect | A title polling a non-gamepad type marks the slot empty, so the game sees the pad disconnect. Fixes Madden 25 connect/disconnect spam. | hid/input_system.cc | +225/-2 | conflict | none |
| 36 | 915532b832 | 08-29 | gpu/vulkan | Fix swapped stage and access masks on scratch buffer growth | Our vulkan_command_processor.cc:6149 assigns the access mask to the stage field and vice versa after growing the scratch buffer. Wrong barrier on the next use. 2 lines. | gpu/vulkan/vulkan_command_processor.cc | +8781/-238 | clean | none |
| 37 | fb4270fb3b | 08-28 | gpu/vulkan | Match stage flags between the two storage buffer set layouts | Sets from the RT cache and the command processor bind into the same slot with different stage flags, which breaks pipeline layout compatibility. Ours still has kStorageBufferCompute (:719, :5615). | gpu/vulkan/vulkan_command_processor.cc/.h, vulkan_render_target_cache.cc, vulkan_texture_cache.cc/.h | see above | conflict | none |
| 38 | 9da693480d | 09-10 | gpu | EDRAM bits respected for color/depth aliases | Depth was dropped whenever a color target aliased it. Tracks which host RT owns the depth bits per range. Fixes sprites visible through geometry in 4D530A26. Host RT path included, behind a cvar. | gpu/render_target_cache.cc/.h, vulkan_command_processor.cc | rt +300/-5 | conflict | none |
| 39 | 3a44f20c7b | 08-31 | gpu | Replace AC6 ground hack with scalar approximation rounding | Rounds EXP/LOG/RCP/RSQ/SQRT results to 21 mantissa bits in SPIR-V and the interpreter. Fixes Ace Combat 6 ground. Costs one extra ALU op per transcendental. Our tree has no ac6 hack cvar. | gpu/spirv_shader_translator_alu.cc, shader_interpreter.cc, spirv_shader_translator.h | alu +72/-12; interp +25/-2 | conflict | none |
| 40 | 0c843efb32 (SPIR-V-only twin 7b2555f1f2) | 08-28 | gpu | Round normalized unsigned fixed fetches | Titles comparing filtered unsigned fixed samples against Q16 need rounding. Fixes 4D5309C9 and 4D530AA4. | gpu/spirv_shader_translator_fetch.cc, texture_cache.cc/.h | fetch +538/-64 | conflict | none |
| 41 | 9e9d3cdd3f | 08-18 | gpu | Clamp out-of-bounds vertex fetch words to 0 | Words past the fetch constant size read 0 like hardware instead of whatever sits in shared memory. | gpu/spirv_shader_translator_fetch.cc, spirv_shader_translator.cc/.h | fetch +538/-64 | conflict | none |
| 42 | 3cc741caa3 | 08-28 | gpu/spirv | Preserve Inf and NaN through ReduceFloatPrecision | Six non-finite patterns came out as wrong values; negative ties rounded the wrong way. | gpu/spirv_shader_translator_alu.cc | +72/-12 | conflict | none |
| 43 | 2b071d9b09 | 08-18 | gpu/vulkan | Invalidate user clip plane constants on source registers change | Clip plane constant buffer stayed stale after PA_CL_UCP/CLIP_CNTL writes. 5 lines. | gpu/vulkan/vulkan_command_processor.cc | +8781/-238 | conflict | none |
| 44 | 66779fb873 | 08-18 | gpu/vulkan | Fix adaptive tessellation issues | Domain shader read the patch index from the wrong source; tessellator winding inverted facing so backface culling removed surfaces. | gpu/spirv_shader_translator.cc/.h, vulkan_command_processor.cc/.h, vulkan_pipeline_cache.cc | spirv +793/-41 | conflict | none |
| 45 | 3eab2b8b39 | 08-18 | gpu | Handle tessellated triangle strip and fan draws | Strips and fans were rejected; now converted to lists. | gpu/primitive_processor.cc/.h | +92/-0 | clean | none |
| 46 | 0d38781760 | 08-18 | gpu/vulkan | Apply float controls to geometry shaders | GS NaN checks for vertex kill can be folded away without NaN preservation. | gpu/vulkan/vulkan_pipeline_cache.cc/.h | +744/-59 | conflict | none |
| 47 | 77597d62ae | 08-18 | gpu/vulkan | Select back-face stencil ref/mask when culling only front faces | Picked back-face state even with both faces culled. Matches D3D12. 3 lines. | gpu/vulkan/vulkan_command_processor.cc | see above | clean | none |
| 48 | 53061c63fe | 08-18 | gpu/vulkan | Use dedicated alpha blend factor map for alpha srcb/dest blend | Alpha factors mapped through the color table. Matches D3D12. | gpu/vulkan/vulkan_pipeline_cache.cc | +744/-59 | clean | none |
| 49 | 2d5b41080c | 08-18 | gpu/vulkan | Clamp stacked-texture layer index for Inf/NaN coords | Prevents an undefined layer index on bad coordinates. | gpu/spirv_shader_translator_fetch.cc | +538/-64 | clean | none |
| 50 | 4aeb518c9c | 08-18 | gpu/vulkan | Clamp scalar maxas/maxasf address register to [0, 255] | Matches DXBC; keeps relative addressing in range. | gpu/spirv_shader_translator_alu.cc | +72/-12 | clean | none |
| 51 | 7cd47947b0 | 08-18 | gpu | Select promoted tfetch1D layouts at runtime | Fixes wide 1D bindings (555308CE) without breaking 545407D4. | gpu/spirv_shader_translator_fetch.cc, texture_cache.cc, xenos.h | fetch +538/-64 | conflict | none |
| 52 | e6bdb0fdf7 | 08-18 | gpu/vulkan | Skip draws with surface_pitch==0 to match D3D12 | Memexport-only draws with no surface no longer reach the RT cache. 7 lines. | gpu/vulkan/vulkan_command_processor.cc | see above | conflict | none |
| 53 | bea885689d | 09-11 | gpu | Clamp dynamically addressed shader constant indices | a0/aL-indexed constant reads clamp to the declared array. Under Vulkan robustBufferAccess the read was already bounded, so this is correctness, not a crash fix here. | gpu/spirv_shader_translator.cc | +793/-41 | clean | none |
| 54 | ec5e0f40e7 | 08-28 | gpu | Don't drop swizzles from dummy texture headers | A header asking only for literal 0/1 still creates the texture with declared dimensions. | gpu/texture_cache.cc | +8/-0 | conflict | none |
| 55 | 3ff230d23b | 08-18 | gpu | Emulate extended-range float16 in render target pack/unpack | Exp 31 is a finite value up to 131008 on the guest; packing clamped at 65504 and unpacking gave Inf. Mostly FSI/memexport paths; the render_target_cache.cc hunk is 5 lines. | gpu/spirv_shader_translator_rb.cc, _memexport.cc, render_target_cache.cc | rb +17/-4 | conflict | none |
| 56 | cb240560df | 08-18 | gpu/vulkan | Fix 2x MSAA alpha-to-coverage sample layout | Wrong sample bit for guest 2x A2C. 5 lines. | gpu/spirv_shader_translator_rb.cc | +17/-4 | conflict | none |
| 57 | f25003c0ea | 08-18 | gpu | Load guest-resolved mips from scaled resolve memory | Only with resolution scaling. Also clamps uncompressed copies so they never overrun the host image. | gpu/texture_cache.h, vulkan_texture_cache.cc | tex +340/-1 | conflict | none |
| 58 | 31168682b0 | 08-18 | gpu/vulkan | Fix draw_resolution_scaled_texture_offsets cancelling itself out | Only with resolution scaling. Folds in the 5841095A fix. | gpu/spirv_shader_translator_fetch.cc | +538/-64 | conflict | none |
| 59 | bbb60630f0 | 08-24 | gpu | Match Canary's alpha bias in 2_10_10_10_FLOAT resolves | exp_bias applied to alpha for every dest format, as XGCopySurface does. | gpu/shaders/resolve.xesli | +11/-0 | conflict | none |
| 60 | a2b609d22d | 08-31 | gpu | Gate the 7e3 to 8_8_8_8 decode on the written channels | Blend probe ignored the write mask, so a red-only draw decoded the whole scene buffer (GoW Judgment ADS). Only the render_target_cache.cc hunk ports; edram_transfer_shader.cc does not exist in our tree. | gpu/render_target_cache.cc/.h | +300/-5 | conflict (file absent) | none |
| 61 | 22708301ba | 08-30 | memory/gpu/kernel | Initial XPS support | 0x7F000000 becomes a real physical heap; L2 range wrapped in vfetch and index buffers. Enables XPS titles. Our PM4 lives in command_processor.cc, so the pm4 hunk ports by hand. | memory.cc/.h, spirv_shader_translator_fetch.cc, shader_interpreter.cc, vulkan_command_processor.cc, xboxkrnl_memory.cc | memory +284/-34 | conflict | none |
| 62 | 79feeb9095 | 09-15 | base/emulator | Align guest system time with host time at setup | Guest system time ran ahead of host by the startup delay. Our emulator.cc:862 uses set_guest_system_time_base. | base/clock.cc/.h, emulator.cc | clock +16/-0 | clean | none |
| 63 | f73c521230 | 09-12 | base | Fix identifier pasting in the cvar update macros | UPDATE_CVar collapses every update in a TU to one name; two in the same hour fail to compile. Needed before porting anything that uses UPDATE_from_bool. | base/cvar.h | +53/-1 | clean | none |
| 64 | b0a1f40b41 | 09-03 | kernel | Leave the padded block in the SHA context | Console-exact XeCryptShaFinal buffer state; only observable on a double finalize with a tail over 56 bytes. | kernel/xboxkrnl/xboxkrnl_crypt.cc | +736/-146 | clean | none |
| 65 | 0da2a16134 | 08-25 | kernel | Disable allow_incompatible_title_update by default and log when applied | Config default; stops a mismatched TU being applied silently. | kernel/kernel_state.cc | +1029/-198 | clean | none |
| 66 | 211e7d9531 + 33f3a47ce2 | 08-23 | vfs/kernel | Refresh host entry timestamps on update; refresh entry info in NtQueryFullAttributesFile | Stale size/timestamps after a write. 8 lines total. | vfs/devices/host_path_entry.cc, xboxkrnl_io.cc | entry +98; io +674/-97 | clean (both) | none |
| 67 | 3ea5e4212d | 08-31 | ui/vulkan | Log the requested device extensions the device lacks | Diagnostic only; 6 lines. | ui/vulkan/vulkan_device.cc | unchanged | clean | none |
| 68 | 59d00d4baf | 08-27 | cpu/base | Make breakpoints and guest traps reachable on POSIX hosts | Installs a SIGTRAP handler; a64 OPCODE_TRAP and brk-based DebugBreak stop being silent process death. Diagnostics value only. 250 lines. | base/exception_handler_posix.cc, debugging_posix.cc, a64_backend.cc, a64_emitter.cc, code_cache_base.h | eh_posix +283/-8 | conflict | none |
| 69 | ff681ddf48 | 08-27 | cpu | Mark the host-to-guest thunk explicitly for unwind info | POSIX code caches infer the thunk from stack size; a guest function with the same frame size gets the thunk's unwind record. Only affects host unwinding through JIT frames. | a64_code_cache_posix.cc, code_cache_base.h, a64_backend.cc | a64 new since base | conflict | none |

## Considered and skipped

| hash | subject | reason |
|---|---|---|
| 462a1ac855 | Publish a shader translation's validity before is_translated() | Our shader.h has no TryClaimTranslation; nothing spins on is_translated() from another thread. |
| 920f35c938 | Fold float vector equality as a float compare | Already in our value.cc (FLOAT32/FLOAT64 cases compare f32/f64). |
| 63ea6e2725 / d39b10a2ab | Select the same vperm half as the backend in the int16 fold | Our Value::Permute has no INT16 table fold; unreachable. |
| 444104a33e | Inject preempt checks at every back-edge | Already ported as XenDroid bb86a836f (commit 63bcdc65cc). |
| 0cfa18775d, cfcc929b64, 1cf1169fa5, addda160ca | POSIX multi-wait wake series | Our WaitMultiple parks on the shared condvar with a predicate; per-object condvar + gated poke ported as 84fff766c4. Our thread exit already calls NotifyWaiters; our registration is RAII. |
| 833dd7df7d | Wake only the CPUs a signal can matter to | Ported earlier as XenDroid eb71db58d shape (threading_posix.cc:373). Scheduler half is off. |
| cd934b7ec9 | Add PreciseSleep | Prerequisite for scheduler backports only; wfe path default off. |
| 6a67369f85, cbc767df13 | pthread_self in global mutex; macOS fast mutex | Our mutex.cc has no futex-based xe_global_mutex; nothing calls gettid per lock. |
| a56d6b8838, f63a14e6dc, cfb81bfe54, b6e27cb4f9 | shm naming, mode 0600, memfd | GNU/Linux paths. Android uses ASharedMemory_create (memory_posix.cc:276). |
| a13309e331 | QueryProtect region merge | Our loop already advances map_region_end (:256). Remaining part is a warning fallback. |
| 614fedfbe3 | Honor the length-0 release | Leak at shutdown only. |
| 44e05e470a | Skip the granularity call below the physical heap | Our TranslateVirtual already tests the address first (ppc_context.h:458). |
| 2e15802618, 8d99ccefb2, 7301cf24cf, a1c2b745ba | d_type, getpwuid_r, file mapping growth, interrupt time units | d_type is populated on ext4/f2fs; GetUserFolder unused on Android; our mapped memory already ftruncates (:69); our interrupt time derives from guest time (clock.cc:181). |
| ca42e669c9 | Guest page protection on hosts with pages over 4 KB | Thor runs 4 KB pages. Revisit if a 16 KB kernel build is ever targeted. |
| aed81ca93a | Rework guest access resolution and unwatched invalidation | Host page = guest page here; "unwatched" invalidation partly present (4 refs in memory.cc). |
| 00a57b3815, 67e0804262, d1541a579b | Back unallocated pages on AV; allow_invalid_upload_range default; invalidate on alloc | Windows SEC_RESERVE behaviour; POSIX reads zero. We lack the gpu_allow_invalid_upload_range cvar the set revolves around. |
| 87c2411270 vs 1ad151d124 | AllocRange ceiling | Same memory.cc hunk; 1ad151d124 also replaces PhysicalHeap alignment guards with asserts. Kept as one row (24). |
| 8642d8eff9 | Load v128 constants from a literal pool | Already have it (a32ccc9605, GetV128ConstLabel). |
| d624e09eb4, 1383888834 | isb for db16cyc; coalesce yields | CLAUDE.md keeps a64_spin_hint_isb off after a confounded A/B; our DELAY_EXECUTION goes through EmitSpinHint. |
| 619170cc7a | Stack sync check after the source offset stamp | Only matters with debug info (COMMENT opcodes) on. |
| 8a8fe1d32a, 404bfa3a52, 229fec07c2, 0f55d6cb50 | Safepoint PC, wedged fiber naming, profiler weighting | Scheduler diagnostics and profiler infrastructure. |
| 038050012a | Number every block before allocating registers | Latent; changes no generated code today. |
| 504cbee7eb | Fix VMX128 opcode table entries | The removed va==0x60 hack already routed the encodings correctly; disassembly-only change. |
| bb4da59dac, d72059cd4a, e3d399e5dc, 01dc16a604, 0e1307bd2e | VEC128 context promotion series | Our ContextPromotionPass is a different pass (+2683 lines, no VEC128). Upstream re-enable was reverted on 09-05. Idea only: 01dc16a604 measured 30.8 to 36.4 fps in a CPU-bound Lost Odyssey scene. |
| fc22bceae6, 62140dea88, 510a9553be, e011d1eeab | Precompile on workers / off UI thread / by default / CRT initializers | Our xex_module.cc (+613) has its own AOT path (CLAUDE.md "AOT+LLVM standard"). Idea: precompile _initterm callees before the main thread resumes (Hydrophobia). |
| a77db1ed3b | Replace split_map with a hash map | Our entry table already differs (lock-free lookup, no split_map.h). |
| cdd60494d7 | Add support for dash 1746 | Dashboard support, not a title fix. |
| 9363df7d8f, a33ee3117a, ca9e079051, c814a73506, d3094f5e67, bb41c0e3d5, ec69937b09, a2003c3a48, 3fd67de684, 1c7df55be3, 66af56ed7a | x64 sequence fixes | x64 only. a64 twin of 9363df7d8f is row 9. a64 default NaN is already positive 0x7FC00000; a64 f16 unpack is hardware fcvtl. |
| 04b3b63d93, c6cc761676, 5e9eae601b, e987fd7f50, 5a9104a205, 44599705bc, 27db26839b, 086b4421df, 83abd43576, bf300b65f8, d275d8e30f, 10e20f3bf0, e0e1cf7e31, c097a6d319, 64a0242af3, f94aa54cd9, 1fad140848, 9a232c8a1a, bb798a0b84, 137d68bdc4, d4b1ae936c, 728c93d652, 76ce245b8f, 182cd53257 | Guest scheduler series | All change the cooperative fiber path only. guest_scheduler=false in our tree (CLAUDE.md: it crashes Gears). If it is ever enabled, take the edge tip guest_scheduler.cc wholesale; d275d8e30f (Halo Reach hang) and 54fd4a3078 note the scheduler cases. |
| 8705cd19cb | Align the stack when diverting a fault to a halt thunk | x64-only realign; the commit says AArch64 SP is always aligned. |
| 8c74cfd0c0 | Deliver guest C++ throws to a catch(...) handler | Our tree has its own guest EH (742d6245b8, a123c54d79) with typed catch. |
| 59c08cd462, 5cf409d6e5, a5262a968a, 0d395ce9ab | X_OBJECT_TYPES rename, C++20 cleanup, error code names, cvar moves | Cosmetic. |
| 6d3efdd31c, d9bf601e1c, 8d8ba9b6f7, d633c5e784, 3ba9c86eb0, 159337dbba, 99b3ccad26, 80679bcadd, afd282d505, 4d8b3d650e, 6b5c3233d5 | XAM stubs and argument checks | Compat stubs; no stability or speed effect. |
| b11458e49b, 5d4dc8a88a, 8ae1c14856, 2226749150, f5c8af81a5, 1e834f8a8c, 0e5bc07239, 3ecd9d36bf | XContent package system | Our tree has no kernel/xam/xcontent/. Large refactor; not a fix. |
| 9be56834e0, d7a868837c, 95a3d08948 | Dirty disc exit, disc tracking, media_type cvar | Desktop disc swap UI and a manual override. |
| 5dd1cdbbf5, adf56b76c4, 6bef1fbed1 / 052365bc01, 7015473a77 / 9d8210b32d | XMA new-decoder fixes (Tekken Tag 2, musou BGM, NBA Live 06 hang, LEGO freezes) | Files are xma_context_new.cc. Our decoder is the master shape (no GetNextPacketReadOffset). adf56b76c4 states master already decodes those frames correctly. Retest the named titles; if they fail, port by idea. |
| 1fae8f43a5 | Decode XMA on the kicking thread by default | Our kick has no work_completion_event_ wait to remove. |
| b495439ba9 | Take a renamed entry's name from the guest path | Our Entry::Rename already splits on '\' (entry.cc:142). |
| a7a3eabdbb | Fix upper_ascii | upper_ascii has 0 callers outside utf8.cc. |
| 85e4f325d5, cdfe727449, 7ac465fd6e | Placeholder draws, async pipeline queue, async shader compilation | Our Vulkan pipeline cache has no placeholder/async pipelines (0 refs). |
| 8d75a1779a, 830e605186, 654a8cacf5 | FSI pixel shader specialisation and 2x-as-4x mask | FSI only. CLAUDE.md records most of the RB translator as dead on this device. |
| 5c290c32e4 | Split host_buffer/memexport toggle | Our gpu_flags has neither cvar; shape differs. |
| a766c62c79, d5845a4941 | Texture upload compute blit | NVIDIA copy-rate workaround; neutral at native res. |
| 437a7280cf, 2d0f19746c, 4eb111560a | EDRAM single-sample layout; direct scaled resolve | 437a7280cf rewrites resolve shaders and both RT caches (about 1500 lines) with no named bug. The other two touch edram_dump_shader.cc, absent here. |
| 4a863a0e1a, e87321b067, 30a2ef6fc9, 61a8aa3607, 2c9a7cd07a, c8ac9258f0, 92952681a8 | Base map regression fix, cvar typo, storage version sum, RenderDoc cvar, NVIDIA 3D LOD, barrier refactor, Metal | Regression source (master 2606fa5) not in our tree; our cvar is native_2x_msaa; version bump only; tooling; NVIDIA-specific; refactor; Metal. |
| dd924f2d8a, 44cb87328c, 9d08d64b5d | Init log spam, GTK disc picker thread, plugin config path | Log noise; GTK; 1-line path fix in plugin_loader.cc (could take if plugins are used on device). |
| a5a18f5c75 | Random optimizations | emplace_hint and a cached ratio in clock.cc; our clock.cc differs. |

## Notes

1. Commit pairs that must go together: 3494b56a0b + e88555c35c (row 6, either suffices, both are clean); 73388a080f + d8348b4f68 (row 5, port the edge tip threading_posix.cc state, the two patches were written against different bases and both survive at tip); 54fd4a3078 + dcf2994ea1 (row 8, same for xevent/xsemaphore/xobject); 10ac2f5eff needs edge's IsPossibleMMIOInstruction and the accessed_mmio instruction flags before it does anything (row 14); a5f59a48ae is independent of 8642d8eff9 because we already have the literal pool (row 12); f73c521230 (row 63) must precede any port that uses UPDATE_from_bool.
2. Duplicate commits in the range (identical patch-id): 577fb8ee17 = 46e8fd8dcc, 63ea6e2725 = d39b10a2ab, 6bef1fbed1 = 052365bc01, 7015473a77 = 9d8210b32d, b0a1f40b41 = 6db5a346d2, 66af56ed7a = 74ab667785. Near-duplicates: 5ce7c336df / 45827cc2ba, dcf2994ea1 / 54fd4a3078, 87c2411270 / 1ad151d124, 0c843efb32 / 7b2555f1f2, 12a0558b8f / 2f858c4629.
3. Already in our tree under other hashes: 920f35c938 (value.cc float compare), 444104a33e (bb86a836f), 0cfa18775d (84fff766c4), 833dd7df7d (eb71db58d), 8642d8eff9 (a32ccc9605), 44e05e470a ppc_context part, b495439ba9 (entry.cc rename), a13309e331 merge loop.
4. Row 3 contradicts our port commit 159be60a3b, which claims the opposite. The handler code decides it: `if (!byte_swap) value = byte_swap(value)` then resume at pc + 4 (mmio_handler.cc:554-556, :626). With byte_swap=true the handler stores the logical value and the REV that still executes reverses it. 159be60a3b was build-tested only. Confirm on device before and after.
5. Rows 13 and 14 are the two largest speed items that are not micro-optimisations. Row 13 has a partial equivalent behind gpu_early_primary_read_pointer_writeback (command_processor.cc:2378). Row 14 removes a SIGSEGV round trip per guest MMIO access on the a64 backend.
6. The kernel is a port of edge as of 2026-08-03, so kernel rows conflict on context, not on design. Ports there should diff against the edge tip file rather than apply the patch.


---

# Appendix B: xenia-canary report

# xenia-canary triage for xenia-thor (AYN Thor, Adreno 740, Vulkan/Turnip)

Source: canary/canary_experimental, 950 commits in master..canary (list: canary_new.txt).
Method: bucketed by subject and touched paths; ran an added-line sampler (present.sh) against master for 729 commits; read diffs for the candidates below; ran `git apply --check` for every kept commit. Nothing in the tree was modified.

Facts that shape the result:
- 95a5c3ee2 is an xenia-master commit. Our tree is a hybrid: kernel/xam is canary-derived (most canary XAM fixes through 2026-07 are already present); apu, memory.cc, gpu/vulkan and base are xenia-master-derived with our own changes.
- Our a64 backend is the canary a64 donor taken 2026-05-18. All canary A64 commits before that date are present except three small ones listed below.
- Our XMA decoder is the master-lineage xma_context.cc. Canary's xma_context_new.cc series (25 commits) has no target file here.
- Our threading_posix.cc, memory_posix.cc and mutex.cc were reworked locally, so most base patches conflict on context but the logic gaps are real.

## Counts

Total scanned: 950. Out of scope by subject/path (no diff read): 221 (83 pre-2020 commits pulled in by the gen_tests merge, 90 build/CI/docs-only, 12 D3D12-only, 26 x64-only, 36 test-only, 4 merges; the 64 XAM stub/feature commits were sampler-checked and are counted below). Evaluated by sampler or diff: 729. Already in tree: 185 by sampler (>=60% of sampled lines present) plus 34 confirmed by hand (listed in the skipped table). Kept: 52.

## Ranked table

rank | hash | date | area | subject | what it fixes and why it matters on the Thor | files | our churn | apply --check | depends on
--- | --- | --- | --- | --- | --- | --- | --- | --- | ---
1 | 8ffe24e372 | 2026-08-17 | cpu/shared | Publish JIT entry results under the entry table lock | ResolveFunction writes entry->function, end_address and status with plain stores. GetOrCreate readers read status under the lock and then read function. On ARM64 a reader can see STATUS_READY before function is visible and jump through a stale pointer. Our processor.cc line 383 still does `status = entry->status = STATUS_READY` unlocked. Our cpu_lockfree_entry_lookup cache needs the same release ordering. | entry_table.cc/.h, processor.cc | 62 / 16 / 1556 | conflict entry_table.cc:83 | none
2 | 6f9840568b | 2026-09-14 | base/posix | Set O_APPEND only for append-only handles | Our OpenExisting sets O_APPEND whenever kFileAppendData is requested. Linux pwrite ignores the offset on an O_APPEND fd, so every positioned write to such a handle lands at end of file. Guests ask for append+write routinely. Save and cache files get corrupted. Also append-only handles were opened O_RDONLY. | filesystem_posix.cc | 22 | conflict :163 | none
3 | 3494b56a0b | 2026-09-02 | base | Saturate the WinSystemClock to steady_clock conversion | A timer armed with an absolute due time in the past overflowed int64 nanoseconds and waited about two centuries. A title that sets its audio tick with due time 0 gets no tick and runs silent, or hangs waiting on it. | chrono_steady_cast.h | 0 | clean | none
4 | d8348b4f68 | 2026-09-10 | base/posix | Fix double join, terminate spin, affinity, NanoSleep and a stale suspend token | Ours still has: post_execution/destructor can pthread_join twice (UB once the tid is reused); Terminate on an already-finished self spins in `for(;;)` and burns a core; affinity_mask/set_affinity_mask use int shifts (`set << i`, `1 << i`) that are UB from bit 31. NanoSleep is already fixed here. WaitSuspended differs (our ack-semaphore design), so port 3 of 5 hunks by hand. | threading_posix.cc | 300 | conflict :164 | none
5 | 4aeb518c9c | 2026-08-18 | gpu/spirv | Clamp scalar maxas/maxasf address register to [0, 255] | Ours clamps a0 to [-256,255] for scalar maxas. A negative a0 indexes the constant register array out of range in the shader. On Adreno that is an out-of-bounds buffer read and a possible GPU fault. Matches the DXBC translator. | spirv_shader_translator_alu.cc | 84 | clean | none
6 | 1fdbe569e4 | 2026-08-04 | gpu/vulkan | Barrier consecutive uploads to the same image | Two CmdCopyBufferToImage to the same image in one submission had no TRANSFER_WRITE->TRANSFER_WRITE barrier. A tiler like Adreno can reorder them and the texture shows the older data. | vulkan_texture_cache.cc | 341 | clean | none
7 | a092551735 | 2026-02-17 | gpu/vulkan | Validate upload range bounds before vastcpy | UploadRanges copied from guest pages without checking they are mapped. An invalid range faults inside the GPU thread. Adds a QueryRangeAccess check on both ends of the range. Drop the d3d12_shared_memory.cc hunk. | vulkan_shared_memory.cc (+d3d12) | 692 | conflict (d3d12 file) | 78e753ef52 (cvar gpu_allow_invalid_upload_range, absent here) and ac00d09244 (WriteCombine pages count as writable in QueryRangeAccess)
8 | 93adb2bb95 | 2025-12-01 | base/config | Gracefully handle type mismatches in cvars | LoadConfigValue calls `result->value<T>().value()`; a wrong-typed TOML value throws bad_optional_access and aborts at startup. Users edit the config on device. | cvar.h, cvar.cc, config.cc | 54 | conflict cvar.h:147 | none
9 | 174d2d4205 | 2026-03-09 | base/posix | Robust mutexes | PosixConditionBase::mutex_ becomes PTHREAD_MUTEX_ROBUST; Wait recovers EOWNERDEAD. XThread::Terminate kills threads with pthread_kill/cancel; if the victim held a wait-object mutex every later waiter deadlocks. Ours has no robust init (0 hits for setrobust). | threading_posix.cc | 300 | conflict :193 | none
10 | 9781a75a22 | 2026-06-29 | gpu/shared | Avoid stale texture state after invalidation | Adds SharedMemory::IsRangeValid and re-validates texture ranges after invalidation instead of trusting cached state. Prevents textures that keep old contents after the guest rewrites memory. | shared_memory.cc/.h, texture_cache.cc/.h | 66 / 25 / 8 | conflict shared_memory.cc:416 | none
11 | 1e23aaf4fc | 2026-02-25 | gpu/shared | Improve error handling in graphics memory init | InitializeCommon returns bool; a failed system page flags allocation returns false instead of writing through null. | shared_memory.cc/.h, vulkan_shared_memory.cc | 66 / 25 / 692 | conflict shared_memory.cc:23 | none
12 | 1ae82023ea | 2026-03-09 | base/posix | Per-instance condition variables; WaitMultiple polls with try_lock | Our WaitMultiple still uses the predicate-based wait on one shared mutex (issue #1677): a thread suspended between lock and wait blocks every other waiter. Canary polls with try_lock for multi-handle waits and routes single-handle waits to Wait. Larger behaviour change; test with save-state suspend. | threading_posix.cc | 300 | conflict :222 | none
13 | c9eba5daf8 | 2026-03-09 | base/posix | Signal handler / self-suspension improvements | Ours installs the suspend/terminate handlers without SA_RESTART, so a suspend signal makes blocking syscalls (read, write, poll, sem_wait outside our loops) fail with EINTR in the target thread. Port the SA_RESTART line and the atomic install flag; our self-suspend handling already exists. | threading_posix.cc | 300 | conflict :111 | none
14 | 52297ea8f8 + f21ebd49e9 | 2026-03-09 / 2026-09-15 | base/mutex | Linux fast mutex; pthread_self instead of gettid per lock | global_critical_region is std::recursive_mutex here. Canary replaces it with a spin-then-futex recursive mutex and drops the per-lock gettid syscall. Kernel exports take this lock on every call. Keep our owner-tracking instrumentation (mutex.cc +78) on top. | mutex.cc, mutex.h | 78 / 40 | conflict mutex.cc:10 and :12 | 52297ea8f8 before f21ebd49e9; de63bb18a6 already in tree
15 | 77852914ff | 2026-02-25 | gpu/shared | TextureCache lockless pre-check | LoadTexturesData takes the global lock per draw even when no texture is outdated. Adds a lockless outdated check first. Fewer lock acquisitions per draw on the GPU thread. | texture_cache.cc/.h | 8 / 0 | conflict :693 (context only) | none
16 | 763b160c7a | 2026-04-13 | base/posix | Fallback to nice values | SCHED_FIFO is refused for apps on Android, so set_priority does nothing here. Canary falls back to setpriority on the kernel tid and maps back in priority(). Audio and GPU threads get real priority. | threading_posix.cc | 300 | conflict :18 | our priority mapping (b3d8a21b72 partly present)
17 | 72ce130971 | 2026-06-14 | cpu/ppc | Xbox360 cache line is always 128 bytes | Our dcbz zeroes 32 bytes. Xenon has no 32-byte line; dcbz clears 128. Code that relies on dcbz to zero a full line sees garbage. Also one 128-byte zva instead of four memsets. | ppc_emit_memory.cc | 72 | clean | none
18 | 577fb8ee17 | 2026-09-04 | cpu/ppc | Fix mcrf: copy the CR field | Our mcrf is `StoreCR(crfD, LoadCR(crfS))`. LoadCR(n) and StoreCR(n) bake field n's bit position into the word, so when crfD != crfS the stored field is zero. Fix copies the four bits with LoadCRField/StoreCRField. Compilers emit mcrf between different fields. | ppc_emit_control.cc (+test) | 187 | conflict :422 (our text differs) | none
19 | 7d8ac86f6c | 2025-10-24 | cpu/ppc | Do not return error code from unimplemented overflow checks | Ours returns 1 from five OE-form ALU emitters. A return of 1 fails translation of the whole function and the title aborts on an unimplemented instruction. Canary ignores OV instead. | ppc_emit_alu.cc | 193 | conflict :335 | none
20 | 9369464396 | 2026-02-14 | cpu/ppc | Enable vsubcuw | Emitter is implemented but returns 1 (translation failure). | ppc_emit_altivec.cc | 116 | clean | none
21 | fb76cbb291 | 2026-02-14 | cpu/ppc | Implement mcrxr | Unimplemented here; hitting it aborts translation. | ppc_emit_control.cc | 187 | clean | none
22 | f4af1e2a77 | 2026-02-14 | cpu/ppc | Implement mcrfs | mcrfs is XEINSTRNOTIMPLEMENTED here. Take only the mcrfs hunk; the fcfidx Rc hunk is cosmetic and ours differs. | ppc_emit_fpu.cc | 201 | conflict :237 | none
23 | 5845f3437b + ccf8fb66f5 + 64e51c544e + 1b242658e5 | 2026-03-10 / 2026-09-15 | gpu/vulkan | Async shader compilation, compile thread pool, shader disk storage with startup translation, lock on binding layout tables | Pipelines compile off the GPU thread; the draw uses a placeholder pixel shader until ready; stored shaders are translated in parallel at startup. Largest stutter reduction available for Adreno. 1b242658e5 fixes a data race in the parallel startup translation. About 2,600 lines over 14+3+16 files; our vulkan_command_processor.cc has 9,019 changed lines, so this is a manual port. | vulkan_command_processor.cc, vulkan_pipeline_cache.cc, shader storage, emulator.cc | 9019 / 803 | 1b242658e5 clean; others conflict | series, in that order; 1b242658e5 needs 64e51c544e
24 | 92ada8ebc0 | 2026-08-13 | gpu/shared | Fix scalar ALU swizzles with three-source vector ops | Scalar op paired with a 3-operand vector op reads .wz not .wx for its second source. Wrong shading in both the translator and interpreter. | shader_translator.cc, shader_interpreter.cc, ucode.h | 453 / 27 / 0 | clean | none
25 | 9e9d3cdd3f | 2026-08-18 | gpu/spirv | Clamp out-of-bounds vertex fetch words to 0 | Hardware returns 0 past the end of a vertex buffer; games rely on that to make unwritten vertices degenerate. Our SPIR-V fetch reads whatever guest memory follows. Take the SPIR-V half; the DXBC half is D3D12. | spirv_shader_translator_fetch.cc/.h | 602 / 251 | conflict :43 | none
26 | 878de4c352 | 2026-02-17 | gpu/vulkan | Fix sampler params | Ours sets minFilter and mipmapMode from mag_linear. Fix uses min_linear and mip_linear. Two lines. | vulkan_texture_cache.cc | 341 | conflict :791 (context) | none
27 | e519d59e40 | 2026-08-04 | gpu/spirv | Fix stacked-texture inter-layer lerp base | Ours adds lerp_difference to sign_result instead of lerp_first. One line; bump our kVersion (6) separately. | spirv_shader_translator_fetch.cc, .h | 602 / 251 | conflict on kVersion line only | none
28 | 2d5b41080c | 2026-08-18 | gpu/spirv | Clamp stacked-texture layer index for Inf/NaN coords | Inf/NaN z selected an undefined array layer. | spirv_shader_translator_fetch.cc | 602 | clean | none
29 | e6bdb0fdf7 | 2026-08-18 | gpu/vulkan | Skip draws with surface_pitch==0 | Matches D3D12; avoids setting up a render target with zero pitch. | vulkan_command_processor.cc | 9019 | conflict :2392 (context) | none
30 | 77597d62ae | 2026-08-18 | gpu/vulkan | Back-face stencil ref/mask only when culling front and not back | One-line condition fix in dynamic state. | vulkan_command_processor.cc | 9019 | clean | none
31 | 53061c63fe | 2026-08-18 | gpu/vulkan | Dedicated alpha blend factor map for alpha srcb/dest | A _COLOR factor in the alpha slot must act as the _ALPHA factor. | vulkan_pipeline_cache.cc | 803 | clean | none
32 | 0d38781760 | 2026-08-18 | gpu/vulkan | Apply float controls to geometry shaders | GS lacked the NaN/denorm/RTE execution modes the VS and PS use; Turnip then flushes differently between stages. | vulkan_pipeline_cache.cc/.h | 803 | conflict :83 | none
33 | 24f90c0efc | 2026-02-17 | gpu/vulkan | Stencil bit transfer shaders discard | Stencil-bit transfers never discarded. | vulkan_render_target_cache.cc | 7371 | clean | none
34 | fdd583eced | 2026-03-15 | gpu/shaders | Fix resolve_fast_32bpp_4xmsaa sample addressing | Samples 2/3 of a 32bpp 4xMSAA resolve read the wrong dword. The .xesli is shared; Vulkan compiles it to SPIR-V. Ours still has the sample_select branch. Rebuild shaders. | resolve_fast_32bpp_4xmsaa.xesli | 0 | clean | shader rebuild
35 | e20f26963f | 2026-07-14 | gpu/shared | Initialize GPU registers to hardware reset defaults | A title that reads a context register before writing it gets 0 here and the hardware default on console. 32 lines. | register_file.cc | 0 | conflict :15 (canary-side base differs) | none
36 | f3e42609a2 | 2026-08-04 | gpu/shared | Fix mantissa placement in CPU Float7e3To32 | mantissa << 3 should be << 16. CPU-side unpack used by texture conversion. | xenos.cc | 0 | clean | none
37 | ec5e0f40e7 | 2026-08-28 | gpu/shared | Do not drop swizzles from dummy texture headers | 6 lines in texture_cache.cc. | texture_cache.cc | 8 | conflict :999 | none
38 | 3eab2b8b39 | 2026-08-18 | gpu/shared | Handle tessellated triangle strip and fan draws | Strips and fans are converted to triangle lists instead of being rejected. Sampler found 7 of 11 lines, but apply is clean, so verify before porting. | primitive_processor.cc/.h | 92 | clean | none
39 | ac00d09244 | 2025-10-24 | memory | Ensure writecombine buffers are writable | QueryRangeAccess treats WriteCombine pages as writable. Needed by rank 7. | memory.cc | 318 | conflict :83 | none
40 | 7ef873b0d5 | 2026-08-03 | apu | Mutex for each audio client | Worker thread and UnregisterClient race on clients_[i].driver. Our worker reads the client under the global lock; verify it null-checks driver after the lock is released, and port the null check if not. | audio_system.cc/.h | 136 / 20 | conflict :176 | none
41 | 3ff230d23b | 2026-08-18 | gpu/shared | Emulate extended-range float16 in RT pack/unpack | Exp 31 is a large finite value on Xenos, not Inf; packing clamped at 65504 and unpacking gave Inf. 236 lines, part DXBC. | dxbc/spirv translators, RT caches | 251 (spirv .h) | conflict dxbc_shader_translator.h:816 | none
42 | 9da693480d | 2026-09-10 | gpu/shared | EDRAM bits respected for color/depth aliases | Sprites showing through geometry in 4D530A26. 204 lines across RT caches. | render_target_cache.cc, vulkan RT cache | 305 / 7371 | conflict :206 | none
43 | 2eea146b1b | 2026-03-03 | gpu/shaders | k_16_16 and k_16_16_16_16 EDRAM packing clamping | Adds XePackR16G16Edram helpers with the [-32,32] clamp in pixel_formats.xesli; ours lacks them. 20 of 21 files are D3D12 bytecode; take the .xesli only. | pixel_formats.xesli | 0 | conflict (bytecode) | shader rebuild
44 | 8f55b4abf7 | 2026-07-24 | gpu/vulkan | Store Vulkan gamma RTs as linear UNORM16 | Matches D3D12 gamma handling. 223 lines, touches spirv_builder and the RT cache. Correctness only. | spirv_builder.cc, spirv translator, vulkan RT cache | 0 / 834 / 7371 | conflict spirv_builder.cc:100 | none
45 | 067641668f | 2026-02-25 | gpu/shared | Fix min/max blend operations to apply factors | 717 lines across translators and RT caches. Correctness for MIN/MAX blends. | 6 files | large | conflict | none
46 | d2c8ca675e | 2026-03-31 | cpu/a64 | ARM64 bitmask immediates for AND/OR/XOR constants | Our const path does mov+and; one and_imm helper exists. Removes one instruction per masked op. | a64_sequences.cc | 7475 (donor) | conflict :1486 | none
47 | a1e333223e | 2026-03-24 | cpu/a64 | 16-byte NEON stores in MEMSET fallback loop | Fallback loop stores bytes. Rare path once dcbz uses zva. | a64_seq_memory.cc | 1725 (donor) | conflict :839 | none
48 | 7101021150 | 2026-08-18 | gpu/vulkan | Bind shared memory persistently for texture loads and resolves | Fewer descriptor set rebinds per load/resolve. 243 lines in a file with 7,371 changed lines here. | vulkan_render_target_cache.cc, texture cache | 7371 | conflict :1184 | none
49 | 1a53f261f7 | 2026-03-28 | base | Arena chunk 4 MiB -> 8 MiB | Fewer chunk allocations during JIT. One line. | arena.h | 0 | clean | none
50 | 9467c77f08 | 2026-05-02 | gpu | Fix crash from uninitialized PacketAction in disassemble_pm4 | Only with the debug cvar. One line. | packet_disassembler.cc | 0 | conflict :304 | none
51 | b0a1f40b41 | 2026-09-03 | kernel/crypt | Leave the padded block in the SHA context | Second XeCryptShaFinal on the same context. | xboxkrnl_crypt.cc | 882 | clean | none
52 | 6e5b8324f4 | 2026-07-08 | apu | Pace audio subsystem | Pumps clients on a 5.333 ms deadline instead of the host mixer cadence and raises the worker priority. Our audio_system.cc is master-lineage with 136 changed lines and an Android driver; behaviour change, test on device before keeping. | audio_system.cc/.h, threading | 136 / 20 / 300 | conflict :9 | rank 16 for the priority call

## Considered and skipped

hash | subject | reason
--- | --- | ---
907d92bf8c | Fixed potential OOB memory write in copy utils | already in tree (ported in August)
7887efa69f | fix potential semaphore leak on invalid client index | already in tree (our port of canary 64c59d3d0)
9371e73d92 | Fix audio crashes during shutdown | already in tree (our port of canary 1662c7570)
3b8debcf5b | Fix watch race condition causing crashes on Linux | already in tree (our own variant in PhysicalHeap::TriggerCallbacks)
6f18c98502 | Fix thread suspend deadlock | already in tree (ported edge 6f18c9850)
61639c8906 | Fix nested suspend | already in tree (our Suspend returns early when already frozen)
4f1394bbaa | Fix some kernel threading issues | already in tree (our atomic_exchange fix and store_release exist)
f45a254774, 92b07391df, bc3585d0ef | self-suspend handling | already in tree (SelfSuspend)
9f8961c185 | Fix TLS pointer bug | already in tree
22f21fada9 | setjmp/longjmp guest reentry | already in tree
61c8eb0707 | same-CPU spinlock contention | already in tree
65b74819aa | priority boost on wake | already in tree
b3d8a21b72 | priority mapping with quantum decay | mapping present; timer-driven decay absent; skipped as behaviour change without a crash or speed case
c17a3b19fb | per-process TLS allocation | already in tree
50ef5c2691, 341d3b66a8, 137baa2b9f | XFile thread safety | already in tree (atomic position, ReadInternal, lock init)
b0a387c6ea | Do not crash host on guest trap | already in tree
6203d382e2 | Clear handles in ObjectTable::Reset | already in tree
d42411ec2a | NtReleaseSemaphore status | already in tree
022a29fe58 | UAF on profile login | already in tree
dcf2994ea1 | Reconcile guest dispatch header on native lookup | XEvent part already in tree; remaining xobject/xsemaphore hunks are the X_OBJECT_TYPES refactor line we do not have
67d80958c9 | Remove 10ms delay in thread start | already in tree
7e1506ff23 | XThread set_name mutex | already in tree
dfa70b3677 | _vsnprintf overflow | already in tree
f85f4c3ca1 | Live disconnected error code | already in tree
6b5c3233d5 | error table swap | already in tree (our table has 0x4D3 at 0xC0000240)
ed4540a641 | Remove XMP polling sleeps | already in tree
23b79f2a97 | XamUserCreateAchievementEnumerator empty check | already in tree
28e48410c5 | posix QueryProtect buffer overflow | already in tree
de63bb18a6 | function-local static global_mutex | already in tree
54be3c57a0, d4b295a578, dd585ca0c8, cebbdb6cac, 9bfaff5040, b2b1307822, a7f1a3f18a | XAM/crypt fixes from 2026-02/03 | already in tree by sampler
84b05bd20a, 59f9fb9964, 904c6c8b1c, e54a053eca, dd3ffeb9cb, 6e557abb0d, be64fb1b70, 02d2cb5cc4, 756ae7a558, bdf64db79e, cfc1e4d069, 095bbbbda1, f2a90ce86d, 45cf464f63, 2fc5aca628, b6ad99ee21, 680aeaf52b, ef13662234, 3906ff11ef, 5b0b15676c, 8911a3b7d8, 2fe20d8d4f, 664b77f38e, 2d2105cfd8, 06f56a20ea, 02ea71ed51, bf83c4e9f5 | A64 backend series | already in tree (canary a64 donor of 2026-05-18)
5b9c13b23f | raw clock with CNTVCT_EL0 | already in tree in our own form (23 CNTVCT uses in a64)
6a47370219 | ATOMIC_EXCHANGE swp operand order | our ATOMIC_EXCHANGE has no swp path
4acda223db | Remove ATOMIC_EXCHANGE opcode | dead-code removal; no stability value
920f35c938 | HIR float vector equality fold | already in tree
c383d049ec | AND+NOT folding | already in tree (SimplifyAndNot)
643c13668d, 4a78428c6b, 1548b4e11c, b7add3e0ec, 74ab7339a3 | misc CPU | already in tree
63ea6e2725 | vperm half in int16 fold | fold does not exist in our Value::Permute
5ce7c336df, ca9e079051 | constant Extract fold lane flip; x64 EXTRACT | HIR half needs our tree to have the fold; sampler found 3 of 60 lines; x64 half out of scope. Re-check if extract folds are added
b5d2eea07b | mfvscr/mtvscr format | both instructions are unimplemented here; needs canary's earlier vscr_vec work that is not in this list
83ff0c501b | VSCR NJ bit | our context has vscr_sat, not vscr_vec
049a55f036, 73e3caa5e1 | XexInfoCache mmap crash; infocache cvar | our tree has no XexInfoCache
10e8224a63, dad5f327bf | GuestTrampolineGroup RAII; x64 bsearch | not present or x64-only here
9588ce244d, 7ff152a5a7, ec5c875122, 66af56ed7a, 9363df7d8f, 14c2814654, 702fbc8adb, bc37068f90, 7801d53842, 3747f5e282, 30ac9d7be6 | x64 backend | out of scope (ARM64 fcvt keeps NaN payloads natively; MAX/MIN ppc part present)
a769d70761 / 9190ce3e36, 01dc16a604 / 0e1307bd2e, 0c6dc9b628 / 7010c86fb1 / 12a0558b8f, 68b3490c8b / 09b9e37c4f | reverted pairs | net zero or feature
cfb81bfe54, 15483b02af | memfd / shm cleanup | Linux-only paths; ours uses ASharedMemory
cf013fdc15, 19e9d0c2de, 3efc88abbb, ecae4b6a0b, 9fbcdae5dc, 3bf6823e5b | macOS/portability | ours already builds on ARM64; fstat64/ftruncate64 exist in bionic
bf6e1a81eb | DeallocFixed crashes | our callers differ; only a64 guest trampoline call remains and passes a size
ae1706d1dc | PosixWaitHandle vtable | no matching declaration here
909008870f | non-ANSI launch argument | Windows main path
1e7c1f6773, 4b6ac45650, 56697f1be6, b8b33ecabf, 60f77c3622 / 6dca89834e / e91faca1c4 | VFS | in tree or net-zero after the later revert
acb215696f | host page sizes larger than 4KB | only matters on 16 KB page kernels; Thor runs 4 KB. Keep in mind for Android 15 16K builds
844cc6e2ed, 7e39a7018f, 11ad6b0a87, 87c2411270, 4fcb8e4498, c28019e333, ea02e8d317 | AllocRange, alignment guards, free block tracker, carve-out | our memory.cc has none of the canary carve-out or guard code; free block tracker is a 320-line canary-side change with tests we lack; no crash case on device
beb52110e3 | randomized scribble | debug feature
9b1ebffd27, aed81ca93a, 22708301ba | GetPhysicalAddress case, guest access rework, XPS | feature or large rework tied to canary memory.cc
5d4dc8a88a, 2226749150, b11458e49b, 1e834f8a8c, 8d8ba9b6f7 | content packages | depend on b11458e49b (49 files, absent)
8ae1c14856 | XamContentDelete size check | wrong-size read only; no crash
269ee61845, ccd5c50c74 | XamLoaderGetLaunchData behaviour | behaviour change without a crash case
658bd5db70 | KeSetAffinityThread null return | ours already guards null; only the return code differs
fe40c0d68f, 889d93e134, 59c08cd462, 1807da06db, dc4db67f9c, e4f28f6984 | X_OBJECT_HEADER / dispatcher refactors | refactor; our struct is the master layout
9c8e34b29e, b268292046, 99ea6da18a, cdd60494d7, 131503f68d, 3585b6e592, 80f2b535e9 | kernel features | feature or already present
XAM stubs and implementations (64 commits, e.g. d9bf601e1c, d633c5e784, 80679bcadd, 51322122a5, 573cce0fe2) | XAM | feature additions; most already in tree by sampler
aee0871dd7, 6bef1fbed1, 7015473a77, ade7e610bb, b575c68418, 10cecae4c2, d9747704be, 2d6b7c6ca1, 6e5b2a95b0, 7213f7e69b, ea056849fa, bc54720559, 337afcf318 .. 2c75055caf, 7e98ae6de3, 505697f980, 09dbe2cd36, 4494e40c71, dd29365970, 4ebf8994de, 0efd3a9610 | XMA new decoder series | target files xma_context_new.cc etc. do not exist here; port by idea only (see notes)
29311ddcdd, 547226fc0b, 0af253e230 | APU cleanups | no behaviour change
cd5df622a9, 4757ab0931, a26bbaa674 | FFmpeg update | third_party bump
5be503da77 | no GS for rectangle list fallback | already in tree (kRectangleListAsTriangleStrip guard)
125b4c8c05, 6de637b794, 16d2cc05c5, 32889f51be, 19c5401eda, dc66d67a31, 1a4b78e377, a716dc6fac | Vulkan/GPU fixes | already in tree
2ad412a141 | vkPipelineCache | VkPipelineCache already used here (5 hits); verify disk persistence only
654a8cacf5, d1ad597939, 5d74d69aa8, 792e61f6e2, cba315cf38, b15fcc73e6 | FSI / alpha-to-coverage | Turnip has no fragment shader interlock; FSI path is not used on the device; FBO alpha-to-coverage is a feature
83d776fa39, 66779fb873, f13ee6ec38, 75589de0a4, 2b071d9b09, 562aa0dd91, 090cecd1b8 | Vulkan tessellation, user clip planes, vertex kill | features (28 missing files for tessellation)
d430342d93, 0b7abf0241, cd7b47235d, 55f0333182, 5897de3eaf, 31168682b0, f25003c0ea, 74db632ab3, a635ac64f5, c42aeacf25, 4cbcae5b64, fcebdfb255 | resolution scaling on Vulkan | feature; partly present; no stability case at native resolution
4644657e83, e2c33686cc, 90a7cc4fee | readback_resolve on Vulkan | feature; default is none
fbd620c22b, 73945c06d7, d55670e40b, 08c0cf5ad0, 8a49c0380f, 9c00ce9366 | ZPD occlusion queries | feature series (7 missing files)
3a44f20c7b, 0c843efb32, 052cb95f24, 6a45452087, d119505289, 2ddc5ef737, 0f2980de44, 8486e97a06, 7cd47947b0, fbdb1f2817, 947075f880, 4a863a0e1a | texture fetch scaling and 1D layouts | correctness series with large diffs; 4a863a0e1a fixes a regression from master 2606fa5 which needs checking against our base
3254ac20f8, 658cdbb5de, d0dd989238, 084f14ec41, cde5d85ec9, 2a6e9f4e2a, 25597a5465 | Vulkan sampling and depth features | features or D3D12 parity without a crash case
437a7280cf, 16e1eb8e28, e87321b067, 61a8aa3607, 0d395ce9ab, fc48d37cdc | EDRAM layout rework, cvar moves | large rework or cvar-only
c2674b19d2, 3e1b0c715a, 6057b0a7c7, 4d15f0afea, ef505fbe3d, 20b226dbdf, f722b6b85f | Vulkan residency cache add/remove, upload pool, glslang 16 compat | add-then-remove pair; build/glslang version bound (our glslang is f4f1d8a3, canary a57276bf; f722b6b85f only matters after a glslang bump)
37fabb9346 | vsync sleep on Linux | our graphics_system.cc vblank code differs; re-evaluate with our ADPF pacing
966d8f0925, 9c37af4621, 74ce1c812f, bf880fde0c, d505b6b53a, 223aa70171, aaf284a4b0 | Vulkan presenter / validation | ui/vulkan presenter differs on Android; validation-only
1a9613705d, ac6fd65385, 78e753ef52, 51414f3e1e, 2d7ca4fb39, a00fd0eb12 | cvars and log spam | no stability value (78e753ef52 is pulled in by rank 7)
44cb87328c, 2590f03bc1, 016797d19c, 6027113924, c2a8dc419c, c651641c2d, a1710699cd, dfd450fcbb, 60b7ea98cf | desktop UI/app | GTK/ImGui desktop paths; our Android app has no disc picker or emulator_window
9d08d64b5d | plugin config path separator | src/xenia/patcher/plugin_loader.cc does not exist here
0d0fadb5e1, f9ed4cd520, 1da37db584, d37c22aad0, cff0773e00 | HID keyboard/portal | desktop input
e31142bd79, 81871e7dc8, 4af2fd6513, 53c0178766 | sockets | netplay
5cf409d6e5, 74f34818e7, 492fa7c632, 4182852a4b, b864149575, 3eef564ff8, 4cb783bf22 | C++20, lint, header cleanups | churn only
71c5702ee8 and all [Build]/[CI]/[Cmake]/[xb]/[3PP]/[Docs]/[macOS] commits | build system | out of scope (we are on premake)

## Notes

1. Series that must go together:
   - 52297ea8f8 then f21ebd49e9 (fast mutex, then pthread_self owner id). de63bb18a6 is already here.
   - 5845f3437b, ccf8fb66f5, 64e51c544e in that order, then 1b242658e5. 1b242658e5 alone has no target (no TranslateShadersForStorage here).
   - a092551735 needs the gpu_allow_invalid_upload_range cvar from 78e753ef52 (or a hardcoded false) and ac00d09244 so WriteCombine pages are not reported as no-access.
   - 9781a75a22 and 1e23aaf4fc both edit shared_memory.cc; apply 1e23aaf4fc first.
   - 577fb8ee17, 7d8ac86f6c, 9369464396, fb76cbb291, f4af1e2a77 are independent but all change PPC emitters; run the PPC test corpus once after the set.
   - fdd583eced and 2eea146b1b change .xesli sources; regenerate SPIR-V after both.
2. Base/POSIX ports (ranks 4, 9, 12, 13, 16) all land in our reworked threading_posix.cc (300 changed lines). Port by hand, one at a time, and test suspend/resume and save-state after each.
3. XMA: canary's fixes target xma_context_new.cc. Ideas worth checking against our master-lineage decoder: drain the current frame before Work() exits when both inputs go invalid (6bef1fbed1, NBA Live 06 deadlock); count a frame whose 15-bit header crosses the packet boundary (aee0871dd7); follow the sub-stream skip chain instead of the next sequential packet (7015473a77, LEGO cutscene freezes); gate output space on subframe_decode_count blocks not a full frame (10cecae4c2).
4. Our a64 donor predates these three A64 commits only: d2c8ca675e, a1e333223e (both kept, low rank) and 6a47370219 (no target).
5. acb215696f becomes relevant if the device moves to a 16 KB page kernel (Android 15+ builds); mprotect on 4 KB guest boundaries then fails with EINVAL.


---

# Appendix C: open pull requests report

# Open xenia-canary PRs ranked for xenia-thor (AYN Thor, Adreno 740, Android ARM64, Vulkan/Turnip)

Date: 2026-09-18. Base for diffs: canary/canary_experimental = aee0871dd7. Fork base: 95a5c3ee2. Fork tip: master = b52bb612bf.

## Count

Total 60. Out of scope by title or platform 26. Evaluated 34. Already in tree 3 (1187, 1205, 748). Kept and ranked 16. Evaluated and skipped 15.

## Method notes

- All 60 heads fetched to refs/remotes/canary/pr/N.
- `apply --check` was run against master (working tree) and, with a temporary index in the scratchpad, against the fork base 95a5c3ee2. Only 1077 and 1025 apply to the fork base. Every other failure exists before our changes: the PRs are built on canary commits newer than 95a5c3ee2. A port to master is a manual port in every case marked FAIL, whatever our churn is.
- "Our churn" is `git diff --stat 95a5c3ee2 master -- <PR files>`.
- Updated date 2026-08-25 on 27 PRs is a label sweep, not activity. Those PRs are listed as stale.
- `guest_scheduler` is off by default in master (preempt_check_injection_pass.cc:20). Guest threads on the Thor are host OS threads. The POSIX threading fixes and the XAM race fix apply in the default configuration.

## Ranked table

| Rank | PR | Author | Title | What it fixes and why it matters on the Thor | Commits | Files | Our churn | apply --check | Notes |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 1183 | shempman828 | [Base] POSIX: interrupt an in-progress alertable wait on a queued user callback | Rubric A. On POSIX a callback queued to a thread in an alertable wait never wakes the wait. The signal handler runs the callback and `pthread_cond_wait` resumes. Master's `xeNtQueueApcThread` (xboxkrnl_threading.cc:2133) calls `QueueUserCallback([]{})` and says it "breaks the wait"; on Android it does not. The APC is delivered only when the wait ends on its own. Guest threads that wait alertable for I/O or timer APCs stall or hang. The PR adds a per-thread `callback_pending` flag and a `blocked_on` pointer, and the wait returns `kUserCallback` so xobject.cc delivers the APC. | 1 | threading_posix.cc | 276+/24- | FAIL (master and base) | Fresh (2026-08-27). Not present: `callback_pending` absent from master. Master's `Wait` uses one shared `mutex_` and a per-object `local_cond_`, canary's uses a per-object mutex. Port by hand: about 100 lines, the predicate change and the notify in `QueueUserCallback`. Test with a title that uses NtQueueApcThread. |
| 2 | 1226 | peerloomllc | [VFS] Bounds check the STFS reader | Rubric A. `StfsContainerDevice::GetBlockHash` indexes the mapped package with offsets from its own headers and never checks them. An incomplete or damaged DLC or save package reads past the mapping and the emulator crashes in memcpy during mount. The PR makes `GetBlockHash` return null when the hash table is outside the file, stops both callers, and refuses a package whose `content_size` exceeds the data present. | 1 | stfs_container_device.cc/.h, xcontent_package_container.cc | 406+/0- on the 2 files that exist | FAIL (master and base) | Fresh (2026-09-12). Not present: master's `GetBlockHash` (stfs_container_device.cc:270) is identical to canary before the PR. Master has its own skip for incomplete saved-game packages in content_manager.cc:274, which does not cover DLC or the block chain. `xcontent_package_container.cc` does not exist in master; the `content_size` check belongs in vfs/devices/xcontent_container_device.cc:55. STFS part is 50 lines and ports by hand. |
| 3 | 981 | AdrianCassar | [XAM] Add mutex for properties vector | Rubric A. `UserProfile::properties_` is read and pushed from several guest threads with no lock (user_tracker.cc:477-691). A push during a find is a use-after-realloc. Reported crash in Poker Smash. The PR adds `prop_mutex` and a `lock_guard` in the 7 accessors. | 1 | user_profile.h, user_tracker.cc | 1042+/185- | clean on master | Stale (2026-08-25). Not present: `prop_mutex` absent from master. Applies clean. 16 lines. Lowest cost of any A item. |
| 4 | 1182 | shempman828 | [Memory] Fix large-alignment physical allocs through offset-translated heaps | Rubric A. `PhysicalHeap::Alloc`/`AllocRange` allocate in the parent heap and subtract the heap's physical offset (0x1000 for 0xE0000000, master memory.cc:2083). The parent search returns addresses 0 mod alignment, so the child address is misaligned by 0x1000 for any alignment above 4 KiB. `MmAllocatePhysicalMemoryEx` with 32 KiB alignment through the 4 KiB-page heap fails every time in title 4D5307F1; the guest gets null and faults. The PR adds an `alignment_phase` argument to `BaseHeap::AllocRange`. | 1 | memory.cc, memory.h, physical_heap_test.cc | 374+/36- | FAIL (master and base) | Fresh (2026-08-27). Not present: `alignment_phase` absent from master. Master still has the old loop-style `AllocRange` (memory.cc:1076-1138), not canary's free-block tracker, so the diff does not map. Port by hand: add the phase to the round-down at 1077/1105 and the round-up at 1137, and pass `parent_heap_start % alignment` from the two `PhysicalHeap` callers (1560, 1636). Test file does not exist in master. |
| 5 | 1225 | peerloomllc | [Kernel] Take back the signature a dying object left in guest memory | Rubric A/C. `GetNativeObject` stamps a signature and handle into guest memory. Nothing clears it when the object dies, and the object table reissues the handle. A later read follows the stale handle to an unrelated object. Guitar Hero 5 with DLC: `ObDereferenceObject` released a thread, which closed the file being read, and the title showed a disc read error. The PR clears the stamp in `~XObject` when the memory still holds this object's handle, and keeps a read-side check. | 1 | xobject.cc, xobject.h | 847+/123- | FAIL (master and base) | Fresh (2026-09-12). Partly present: master already has the read-side check `object->guest_object() == guest_ptr` in both the lock-free fast path and the locked path (xobject.cc:940-975). Master lacks the destructor clear and `stashed_handle_`. Without the clear, a stale stamp makes master's locked path create a second object for the same memory. Port the destructor part: about 20 lines. |
| 6 | 1232 | peerloomllc | [Base/POSIX] Classify directory entries from stat, not d_type | Rubric C. `ListFiles` ignores the `stat` return and classifies from `dirent::d_type`. `d_type` is DT_UNKNOWN on FUSE mounts, which is what Android's /storage/emulated is, and DT_LNK for symlinked directories. On such a mount every entry is reported as a file with a garbage size and directories never appear. The PR checks `stat`, falls back to `lstat`, zeroes on failure, classifies from `st_mode`, and adds a symlink-loop guard in `HostPathDevice::PopulateEntry`. | 1 | filesystem_posix.cc, host_path_device.cc/.h, filesystem_test.cc | 128+/4- | FAIL (master and base) | Fresh (2026-09-12). Not present: master filesystem_posix.cc:235 still uses `ent->d_type == DT_DIR`. Master's `PopulateEntry` (host_path_device.cc:131) carries thor's `collapse_content_packages_` logic, so the `ancestors` parameter must be merged by hand. Value depends on whether the device content path is on FUSE; if it is, this is rubric A. |
| 7 | 1015 | Gliniak | [Kernel] XFile: Added check for FILE_NO_INTERMEDIATE_BUFFERING | Rubric C. `NtReadFile` on an asynchronous file always returns PENDING unless EOF. Real hardware returns SUCCESS when buffering is allowed. The PR reads `FILE_NO_INTERMEDIATE_BUFFERING` at `NtCreateFile`, stores it on `XFile`, and returns PENDING only when unbuffered or when the file is on a package mount. Fixes titles that spin on a completion that already happened. | 1 | xboxkrnl_io.cc, xfile.cc/.h, content_manager.cc, host_path_device.cc/.h, host_path_file.cc | 1410+/199- | FAIL (master and base) | Stale (2026-08-25, opened 2026-07). Not present: `FILE_NO_INTERMEDIATE_BUFFERING` absent from master. Master has its own `xboxkrnl_ntreadfile_force_complete` cvar for the Banjo case (xboxkrnl_io.cc NtReadFile). This PR is the principled form of that cvar. Conflicts with master's NtReadFile tracing. Author marks it ad hoc. |
| 8 | 1201 | Gliniak | [Kernel] Always select most compatible and newest TU. | Rubric C. Master's `ApplyTitleUpdate` (kernel_state.cc:709-716) takes `title_updates.front()`. With several title updates installed, an older or wrong-media-ID update is applied, and the title crashes or misbehaves. The PR reads each package's `execution_info` and `installer_version`, skips media-ID mismatches, and picks the highest version. | 1 | kernel_state.cc/.h, xcontent.h, emulator.cc/.h, byte_order.h, app/*, xam/ui/title_update_selector_dialog.* | 2665+/290- on the 5 files that exist | FAIL (master and base) | Fresh (2026-09-17). Not present: `SearchAndMountTitleUpdate` absent from master. Take only the kernel_state.cc `latest` branch (about 60 lines) and the `extra_fields` union in xcontent.h. Skip the `select` mode, the ImGui dialog, and the app/ files; `content_list_dialog.cc` does not exist in master. Depends on master's `XContentContainerHeader` matching canary's; verify before porting. |
| 9 | 1109 | jaypfe | [XAM] Resolve directory-backed package payloads | Rubric C. Title 4343081F creates same-named files inside a saved-game package and opens the path from `XamContentResolve` as a file. Xenia stores packages as host directories, so the open fails. The PR mounts the package and returns a guest path to the payload; it also builds `XCONTENT_AGGREGATE_DATA` from `XCONTENT_DATA_INTERNAL` properly. | 1 | content_manager.cc/.h, xam_content.cc | 1351+/210- | FAIL (master and base) | Stale (2026-08-25, opened 2026-07). Not present: `ResolvePackagePayloadPath` absent. Master already solves the same problem at the VFS layer: `collapse_content_packages_` and `ResolveSinglePackagePayload` in host_path_device.cc:134-143. Check whether 4343081F already works on master before porting. Master xam_content.cc:98-101 still has the `XCONTENT_DATA_INTERNAL` cast the PR fixes; that 5-line part is worth taking. |
| 10 | 858 | The-Little-Wolf | [XEnumerator] - add missing extra sizes & structs | Rubric C. Titles that call `XamGetPrivateEnumStructureFromHandle` read fields after `X_KENUMERATOR`. Master allocates only `sizeof(T)` extra and fills nothing. The PR sizes the private area per enumerator (0x248 aggregate, 0x8 device, 0x28 achievements, `n*0x8C+0x20` stats) and writes `title_id`, `xuid`, `user_index`, `flags`, and the XMP playlist enumerator header. Games named: 494707D4. | 1 | xenumerator.cc/.h, xam_user.cc, xam_content_aggregate.cc, xam_content_device.cc, xam_enum.cc, xmp_app.cc/.h | 2166+/832- | FAIL (master and base) | Fresh (2026-09-05). Not present: `private_enum_size_` absent. Master's `XEnumerator::Initialize` already has an `extra_size` overload (xenumerator.h:68-77), so the API part is close. Heavy conflict with our kernel churn. |
| 11 | 1084 | Gliniak | [XAM] Added global variable storage with automatic management | Rubric D. Replaces three fixed `AllocFixed` calls at 0x80D00000/0x80D10000/0x80D20000 with one `GuestArena` (a `monotonic_buffer_resource` over a 1 MiB fixed block). Adds a null check in `XamIptvGetServiceName` so a failed allocation no longer translates address 0. | 1 | guest_arena.cc/.h (new), xam_info.cc, xam_state.cc/.h | 904+/103- | clean on master | Fresh (2026-08-26). Not present: `GuestArena` absent. Applies clean. Small robustness gain; also fixes an off-by-one where `LoadLanguageTypefacePatch` skipped index 0. |
| 12 | 1210 | peerloomllc | [GPU] Pace the vblank heartbeat on a deadline on Linux | Rubric D here. Canary's Linux branch slept a flat period after each vblank, so wake latency accumulated and beats were lost against audio. The PR paces to a deadline, replays missed beats up to 1 s, and moves the thread from `kLowest` to `kNormal` priority. | 1 | graphics_system.cc | 429+/17- | FAIL (master and base) | Fresh (2026-09-04). Not present, and not applicable as written: master's vsync worker (graphics_system.cc:215-245) is a different design: 1 ms poll of the guest tick count plus swap-driven early vblank. Master sets `last_frame_time = current_time`, so lateness is still not made up. The idea ports in 3 lines (`last_frame_time += period` with a cap). Master does not set a priority on the vsync thread. |
| 13 | 888 | Gliniak | [Kernel] Added XeKeysAesCbc & XeKeysSetKey | Rubric D. Implements two key-vault exports and turns the key table into a mutable map so `XeKeysSetKey` can store keys. Few titles call these. | 1 | xboxkrnl_crypt.cc | 736+/146- | FAIL (master and base) | Stale (2026-08-25). Not present: `XeKeysAesCbc_entry` absent. Conflicts with master's crypt churn. |
| 14 | 1021 | Gliniak | Added support for VdEnumerateVideoModes and VdGetDisplayDiscoveryData | Rubric D. Implements two display-query exports and makes `VdSetDisplayModeOverride` return a mode index. Titles that enumerate video modes get a list instead of nothing. | 1 | xboxkrnl_video.cc, xconfig.h | 782+/23- | FAIL (master and base) | Stale (2026-08-25). Not present: `VdEnumerateVideoModes` absent. `VdEnumerateVideoModes` writes the entry on the guest stack below r1 and calls back into guest code; check the a64 backend's guest-call thunk supports this. |
| 15 | 1144 | The-Little-Wolf | [XAM/NET] - Stub NetDll_XHttpStartup & NetDll_XHttpOpen | Rubric D. Two stubs that return success-shaped values. Unimplemented exports already return 0 without crashing, so the gain is only for titles that check the handle. | 1 | xam_net.cc | 1+/0- | FAIL (master and base) | Fresh (2026-09-05). Not present. Conflict is upstream drift only; our churn is 1 line. |
| 16 | 863 | The-Little-Wolf | [XAM/XGI] - Clean Up | Rubric D. Renames `XGI_XUSER_*` structs to `XGI_USER_*`, moves `X_USER_ACHIEVEMENT` to the header, no behavior change. | 1 | xgi_app.cc, xsession.h | 341+/42- | clean on master | Stale (2026-08-25). Not present: `XGI_USER_GET_ACHIEVEMENT` absent. Applies clean. Take only if we want to track canary naming. |

## Already in tree (3)

| PR | Title | Where it is in master |
|---|---|---|
| 1187 | [Base] Fix a lost resume of a thread created suspended on POSIX | Commit 16096098f4 publishes `suspend_count_ = 1` in the same critical section as `state_` (threading_posix.cc:1324-1348). `PosixEvent::Query` is implemented at threading_posix.cc:1136. Same fix, different wait mechanism (state_signal_ instead of the semaphore). |
| 1205 | [Base/POSIX] Block a multi-object wait instead of polling it | Master never had the polling loop this PR replaces. Master's `WaitMultiple` blocks on the shared condvar under the shared mutex, with a `multi_wait_refs_` gate so signals skip the futex when no multi-wait is registered (commits 84fff766c4, aedff75c95; threading_posix.cc:280-380). The PR's 21% cycle saving in Lost Odyssey is already realized by a different design. |
| 748 | Fix audio quality: Use proper rounding in float to int16 conversion | The PR changes the scalar `#else` path only. On ARM64 master runs a NEON path (xma_context.cc:1138-1177) whose `ConvertToInt32X64` uses `vcvtnq_s32_f32` (round to nearest, xma_context.cc:1075). The scalar path does not run on the device. |

## Considered and skipped (15 evaluated, 26 out of scope)

| PR | Title | Reason |
|---|---|---|
| 1218 | [GPU] ZPD as a running sample counter; QueryBatch support | Rewrites `d3d12_zpd_query_pool` and `vulkan_zpd_query_pool`, which master does not have; master fakes ZPD with `query_occlusion_fake_sample_count` (command_processor.cc:2717). 1940+/1924- over 25 files; our churn on those files is 16550+/408-. Needs the whole canary ZPD stack first. |
| 1111 | [GPU] VIZ_QUERY predication | Builds on the ZPD query pools that master lacks, plus VK_EXT_conditional_rendering. 1862+/205- over 26 files; our churn 14827+/317-. Occlusion readback on a tiler is a perf risk. Same dependency as 1218. |
| 1077 | [GPU] Clamp depth to valid value if Inf is provided | Touches only dxbc_shader_translator_fetch.cc (D3D12). Applies clean but is dead code on the device. The SPIR-V stacked-Z path (spirv_shader_translator_fetch.cc:1073) is untouched. The DXBC change also drops the denormalize MAd, which looks incomplete. |
| 1025 | [Kernel] Fixed use-after-free on title threads termination | Master's `TerminateTitle` stops the scheduler, flushes the log, and calls `std::quick_exit` (kernel_state.cc). The thread-kill loop this PR fixes does not exist in our tree. Applies to the fork base only. |
| 1032 | [Emulator] Auto disc swapping using path prediction | Master has its own mechanism: `disc_playlist` cvar and `Emulator::GetDiscPathForNumber` (emulator.cc:1119). The PR also comments out the `info->disc_number == disc_number` early return. Different design. |
| 546 | [Kernel] Separate async NtReadFile from sync part | Stale (opened 2026-02). Superseded by the same author's 1015 and by master's NtReadFile changes (`xboxkrnl_ntreadfile_force_complete`, tracing). Changes when the event is signalled with no test evidence. |
| 710 | [XboxKrnl] Skip runtime cache file deletion | Ignores delete-on-close for any path containing `\CACHE`. Applies clean, but it is a hack the author says should be inverted, and master has its own cache handling in xboxkrnl_io_info.cc:246-308. Risk of stale cache files. |
| 844 | [Config] Do not overwrite base config if game specific config is loaded | After a game config loads, every `SaveConfig` goes to the per-title file and writes all cvars there. Fixes one problem by creating another. Master's config.cc:240 has the original behavior; low relevance on device. |
| 934 | [Xboxkrnl/Crypt] - Add more keys | Key table expansion. Master already has `X_CONSOLE_PUBLIC_KEY` (xboxkrnl_crypt.cc:356) from a prior port, so it is partly present. 478+/116- against 736+ of our churn on the same file. Rubric D. |
| 768 | [XAM] Implemented Avatar Manifest Parser | Feature, not stability. Applies clean. 606+/85- over 7 files. |
| 1160 | [XEX] - Expand User Module log | Logging only. |
| 1139 | [XAM/XMP] - Stub XMPGetMediaSource | Stub. Master already handles the neighbouring `XMPGetMediaSources` (xmp_app.cc:474). Conflicts with our xmp churn (630+/342-). |
| 876 | [XAM/XLIVE] - stubbed message Ids | Stubs, 401+ over 9 kernel files, conflicts with our kernel churn (1927+/571-). |
| 740 | [XAM/NUI] - Improve NUI Handling | Kinect stubs. 257+/112- over 9 files, conflicts with kernel_state.cc churn. |
| 591 | [XAM/XMP] - XMP Improvements | Media-player behavior. `audio_media_player.h` does not exist in master. |
| 1230 | [HID/SDL] controller_subtypes, and a guitar's whammy | SDL HID. The device uses hid/android. Out of scope. |
| 1221 | [RPC] Add per-game image support | Discord RPC. Out of scope. |
| 1204 | [UI] Skip CJK fonts stb_truetype cannot load | Linux fontconfig path. Master's imgui_drawer.cc has no fontconfig code. Out of scope. |
| 1197 | [CPU/X64] Inline unsigned PACK 8_IN_16 paths | x64 backend only. Out of scope. |
| 1184 | [UI] Add 360-style achievement notifications | Desktop UI. Out of scope. |
| 1172 | [APU] Fix 5.1 channel order in native ALSA audio driver | ALSA files do not exist in master. Out of scope. |
| 1157 | [HID] Fix support for Xbox 360 Skylanders Traptanium Portal | Portal hardware. Out of scope. |
| 1120 | [VFS] Add physical disk drive support | Optical drive passthrough, 5278+ lines. Out of scope. |
| 1105 | Add opt-in UI localization system + Italian translation | UI translation. Out of scope. |
| 1091 | fontfix | CI YAML and dashboard fonts. Out of scope. |
| 1050, 388 | GDBStub | Windows-only debug stub. Out of scope. |
| 1047 | docs: update building.md | Docs. Out of scope. |
| 967 | [UI] Convert UI to wxWidgets | Desktop UI, 48 files. Out of scope. |
| 945 | Add CCI (compressed xbox disc) support | Disc format we do not use. Out of scope. |
| 944, 943 | cmake --install, version.h from CMake | Desktop build system. Out of scope. |
| 938 | [CI] Add Windows ARM 64 GitHub Build Action | CI. Out of scope. |
| 909 | [HID] Added support for Portal on Linux | Portal hardware. Out of scope. |
| 879 | Linux: Write xenia.log to XDG state dir | Desktop Linux path. Out of scope. |
| 839 | [Build/macOS] Initial port to Apple Silicon macOS | macOS, 931 commits. Out of scope. |
| 815 | Disable forceinline and noinline macro | MSVC branch only. Master's platform.h:117-165 has a different macro layout. Out of scope. |
| 718 | [UI] Set default font for Linux | Desktop font. Out of scope. |
| 634 | [Kernel] Implemented XamUpdateGetBaseSystemVersion/GetCurrentSystemVersion | Branch is 7641 commits behind a 2024 merge base. Unmergeable as a PR. Out of scope. |
| 510 | [cpu] Implement stack walker for POSIX systems | Branch is 7516 commits behind a 2024 merge base. The idea (POSIX stack walker for crash dumps) is worth a fresh look, but not from this PR. Out of scope. |
| 494 | [XB] Build scripts directory and binary search logic correction | Build scripts. Out of scope. |

## Suggested order of work

1. 981 (applies clean, 16 lines).
2. 1183 (hand port, about 100 lines, highest stability value).
3. 1226 STFS part (hand port, about 50 lines).
4. 1225 destructor part (hand port, about 20 lines).
5. 1182 (hand port into the loop-style AllocRange).
6. 1232 (hand port; verify FUSE d_type on the device first with a 5-line test).
7. 1201 kernel `latest` branch, 1015, 1109 (verify overlap with `collapse_content_packages_` first).


---

# Appendix D: forks report

# Fork triage: Wunkolo, xenia-mac, IhorHarayda arm64, XenDroid VRS / multi-controller

Date: 2026-09-18. Repo: F:\Projects\xenia-thor-workspace\xenia-thor, branch master. Read-only.
Base: canary 95a5c3ee2 (2026-02-18). Our a64 backend: 23 files, 27,792 lines, all added after the base (commit f3b96a7e8b, 2026-05-18, "Swap Android ARM64 to donor A64 backend").

## Key structural fact

Our a64 backend is NOT Wunkolo's oaknut backend. It is the canary port by "Herman S." (canary 883c2030d0, 2026-03-22, "[ARM64] Initial commit for arm64 backend"), which uses xbyak_aarch64 (lowercase mnemonics, `VReg(d).s4`, `e.ldaxr`), `a64_seq_util.h`, `code_cache_base.h`, and `a64_code_cache_posix.cc`. Wunkolo's tree uses oaknut (`e.USHR(...B16())`, `Q0`, `a64_util.h`) and has no POSIX code cache. Diff between wunkolo tip and our a64 dir: 24,302 insertions / 7,531 deletions. Diff between canary tip and our a64 dir: 15,728 / 1,017.

Consequence: `git apply --check` fails for every Wunkolo and xenia-mac a64 commit (different assembler API and file layout). Line-level "already present" scores are near zero for the same reason. All a64 judgments below were made by function name, not by line.

## Per-source counts

1. wunkolo/canary-arm64-backend: scanned 161, out of scope 34 (11 CMake/build, 7 Windows/MSVC-only, 15 debugger/capstone/tests/UI, 1 `vastcpy` for a function our tree does not have), already in tree by function 125, kept 2.
2. xeniamac/arm64-all: scanned 191. 141 are Wunkolo's 2024 originals (same subjects as source 1, older hashes) and were not re-triaged. Of the 50 wmarti commits: out of scope 41 (macOS, premake, Metal, cleanup, oaknut-specific mac indirection table), already in tree by function 9, kept 0.
   xeniamac/xenia-canary-mac-rebase: scanned 34, out of scope 25 (Metal, macOS UI, build, mac-only guards, and 87360cc7a1 which touches XexInfoCache, absent from our tree), already in tree by function 7, kept 2.
3. arm64fork/arm64: scanned 3, out of scope 1 (binary upload), already in tree 1, kept 1.
4. XenDroid feat/vrs (rfandango): scanned 8, out of scope 1 (Kotlin settings), already in tree by function 5, kept 2.
   XenDroid feat/multi-controller-slots (rfandango): scanned 11, out of scope 11 (Kotlin/Compose app and XenDroid's own JNI shell; the only src/xenia change is a 5-line cvar declaration read by the frontend), kept 0.

Total kept: 7.

## Ranked table

Rubric: A crash/hang/memory-safety/race on Android ARM64; B measurable speed on ARM64 or Adreno/Vulkan; C PPC correctness; D nice-to-have.
"our churn" = `git diff --stat 95a5c3ee2 master -- <file>` (insertions/deletions).
"apply" = `git show <hash> | git apply --check` result.

| rank | source | hash | date | area | subject | what it fixes and why it matters on the Thor | files | our churn | apply | notes |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 (B) | XenDroid vrs | 773efcdd05 | 2026-08-04 | gpu/vulkan | Select coarse shading draws by scope and pixel shader cost, default off | Two things we lack. (a) Rate dedupe: XenDroid tracks `current_shading_rate_`, resets it to `UINT32_MAX` at `EndRenderPass`, and calls `vkCmdSetFragmentShadingRateKHR` only when the rate changes. Our block at vulkan_command_processor.cc:8606-8690 calls `CmdVkSetFragmentShadingRate` on every draw when `GpuVrsPathEnabled()`; no last-rate compare exists (grep for last/prev/current_shading_rate: none). BD's two heavy passes issue ~890 draws each, so that is ~890 redundant dynamic-state records per pass on the recording thread and in the driver. (b) Draw selection by `pixel_shader->ucode_dword_count() >= 48` and by depth-test enable with `depth_compare_op != ALWAYS`; ours selects by alpha-test or blend state only. Their filter keeps text/UI sharp without a per-title table. | vulkan_command_processor.cc (+95/-68), .h, vulkan_pipeline_cache.cc | 8781/238, 1811/8, 744/59 | fails: path prefix `emulator-core/src/main/cpp/xenia/` and our block is rewritten | Manual port. ~60 lines for dedupe + shader-size gate. Hook dedupe into the existing `rt_pass_draws_` reset points. |
| 2 (B) | XenDroid vrs | 81cfbe17c9 | 2026-08-03 | gpu/vulkan | Clamp the coarse shading rate to what the device supports at the current sample count | Queries `vkGetPhysicalDeviceFragmentShadingRatesKHR` once, stores a `sampleCounts` mask per rate (1x1, 2x1, 2x2, 4x2, 4x4), and steps the requested rate down until the mask includes the pass's MSAA count. Their measurement on Adreno: 4x4 and 4x2 are single-sample only. Our gpu_flags.cc:639 text says "The rate is clamped to what the device reports for the current sample count", but no such code exists in our tree (grep `sampleCounts|vkGetPhysicalDeviceFragmentShadingRatesKHR|ClampShadingRate`: none). Setting an unsupported rate is a Vulkan valid-usage violation; Turnip may drop it or misrender. | vulkan_command_processor.cc (+45/-1), .h (+12) | 8781/238, 1811/8 | fails: path prefix | Manual port, ~50 lines. Also needs `vkGetPhysicalDeviceFragmentShadingRatesKHR` in our instance function table. |
| 3 (A, low) | xeniamac mac-rebase | 8e54f33d2b | 2026-01-18 | kernel/memory | Guard temp RW protection during virtual alloc | `NtAllocateVirtualMemory` temporarily sets RW on a non-writable commit, zeroes, then restores. Our version at xboxkrnl_memory.cc:695-704 ignores the return value of `heap->Protect(...)`. If that `mprotect` fails, `Zero()` writes to a read-only page and the host process takes SIGSEGV. The patch only zeroes and restores when the RW protect succeeded, and logs failure. | xboxkrnl_memory.cc (+11/-5) | 842/112 | fails: our block was rewritten (uses `size_to_zero`, `IsWritableProtect`) | Manual port, ~8 lines. Low frequency, but it turns a silent fault into a logged skip. |
| 4 (D) | arm64fork | 67cdaad18d | 2026-03-29 | cpu/a64 code cache | Add runtime fallback for dynamic code cache allocation | If the fixed 0x80000000-0xBFFFFFFF indirection mapping fails, falls back to OS-chosen addresses with code-cache-relative 32-bit table entries (bit 31 tags an external 64-bit trampoline table). Our `A64CodeCache::Initialize()` (a64_code_cache.cc:24) just returns `CodeCacheBase::Initialize()`; no fallback. The fixed mapping works on the Thor today, so this only helps on other devices or after an address-space regression. | a64_code_cache.cc (+159), .h (+48), a64_emitter.cc (+44), a64_backend.cc, a64_assembler.cc, code_cache_base.h; 312 insertions | a64 dir all new; code_cache_base.h new | fails at a64_assembler.cc:92 (same xbyak base, but our files diverged) | Same-family backend, so a port is plausible. Not a Thor stability lever. |
| 5 (D) | xeniamac mac-rebase | 566e6c5866 | 2026-01-18 | memory | Fix heap release sizing and macOS protect alignment | `BaseHeap::Dispose()` calls `DeallocFixed(addr, 0, kRelease)`. Our memory_posix.cc:197 does `munmap(base, 0)`, which fails with EINVAL, so nothing is released. Ours still passes 0 (memory.cc:752). Runs only at heap teardown (emulator shutdown / title restart). The second hunk is mac-only. | memory.cc (+9/-1) | 284/34 | clean | Take only the first hunk (1 line: pass `page_entry.region_page_count * page_size_`). Skip the `XE_PLATFORM_MAC` hunk. |
| 6 (D) | wunkolo | 00ad1909c5 | 2026-03-19 | third_party/microprofile | Fix divide-by-zero hazard | `MICROPROFILE_CONTEXT_SWITCH_BUFFER_SIZE / S.nContextSwitchUsage` with usage 0. Not in our microprofileui.h. The line sits under `#if MICROPROFILE_CONTEXT_SWITCH_TRACE`, which is only enabled with Windows ETW. Inert on Android. | microprofileui.h (+1/-1) | 0 | clean | Free to take, no Thor effect. |
| 7 (D) | wunkolo | e644b0e6c9 (+ b317e61998, 7dbc255268) | 2026-03-19 | cpu/a64 vector | Implement OPCODE_VECTOR_{SHR,SHA} with constant all-same USHR/SSHR path | Wunkolo emits one `USHR #imm` when the shift vector is a splat constant. Our VECTOR_SHR_V128 / VECTOR_SHA_V128 (a64_seq_vector.cc:708-775) always emit `movi + and + neg + ushl/sshl` (4 instructions) and never look at `i.src2.is_constant`. PPC `vsrw`/`vsraw` with a constant shift is common in game math. The mask (`& 0x1F` etc.) from 7dbc255268 must be kept. | a64_seq_vector.cc | all new | fails (oaknut API) | Function-level port, ~30 lines. Small, unmeasured gain. |

## Considered and skipped

| source | hash | subject | reason |
|---|---|---|---|
| wunkolo | fede085c8d | Fix function resolving indirection | Oaknut thunk ABI. Our ResolveFunction path (a64_emitter.cc:2335) is the canary design. |
| wunkolo | 0680329cd7 | Label lookups via GetIdString | Our emitter has no `label_lookup_`; uses xbyak cached labels. |
| wunkolo | 2f2b0e6a8d | Support for large stack sizes | Ours already handles stack_size > 4095 with `mov x17` + `sub sp, sp, x17` (a64_emitter.cc:4064-4068). |
| wunkolo | 273e0495bb | Fix EmulateFLOAT16_{2,4} | Ours already uses `xenos_half_to_float` (a64_seq_vector.cc:1645, 1669). |
| wunkolo | bca862d00e | Fix constexpr neon-register | Ours already has `const uint8x16_t tbl_idx` (base/memory.cc:240, 274, 306). |
| wunkolo | 9236890fdb | Generic memory::vastcpy | No `vastcpy` symbol anywhere in our src. |
| wunkolo | 5949263525 | Remove DOT_PRODUCT lane isolation | Our DOT_PRODUCT_3/4 (a64_sequences.cc:6158) is a different implementation with a fast path; the removed sequence does not exist in ours. |
| wunkolo | 787c75b9d9, fac8425ced, f87991ffd3, 9ed5d270de, 43770dcf9e, cf63a80bb0, 7136bb0d15 | SPLAT / constant MOVI-FMOV optimizations | Ours routes all vector constants through `LoadV128Const` (a64_seq_util.h:145) which has 13 `movi` fast paths. |
| wunkolo | a79266c056, 628712b652, 3552d6973b, 56994d51db, 007527eacd | armv8.0 atomics, LSE detection, ATOMIC_CAS | Ours has LSE `casal` path with `ldaxr/stlxr` fallback (a64_seq_memory.cc:1565-1700). |
| wunkolo | 9693418e09, f73d438e02 | Raw clock source | Ours reads `cntvct_el0` directly (clock_posix.cc:46). |
| wunkolo | 6f29db2e7a, 466fb471ee, 2774aff8d0, 6746419640, 79c0424ef2, eec59e816c, 72a5eed678, a3c7f8322e, e94aa9fceb, 8a1f6fe4ef, b64b216fb0 and the other 2024-05 opcode fixes | Opcode implementations and fixes | All present in ours by opcode (CACHE_CONTROL, DID_SATURATE, VECTOR_AVERAGE, PERMUTE int16 `EmitByInt16`, CNTLZ, SET_ROUNDING_MODE, etc.). The canary donor incorporated Wunkolo's fixes. |
| wunkolo | 51b3690249, 31ac054683, 1c773a4191, 3798861c9b, 8e08220d28, edc52562d4, a30614d76f, db3b238eed, 19b905f7f9, 7155f5d6f9, e69ad8ef4e | CMake / build | We do not use Wunkolo's CMake tree. |
| wunkolo | 07172c5107, 7dfc6f141d, 0a24ac3d63, b5b024b988, 1f570b3273, 56318ae2fb, c6e7bf9d6f | Windows ARM64 / MSVC | Windows only. |
| wunkolo | a99aca4e89, ed9e4408ca, e37b3ad90e, e27512b177, 8acebda91c, 5995633358, 0604ab5410, 0cea8ffe7b, 59b4dcbecf, 63465c1277, cc067570f7, af01f138bc, 15ecd293ae, 3b07819a4c | Debugger, capstone, tracing, tests | Debug tooling, not runtime stability or speed. |
| xeniamac | f0de3a648b | PACK/UNPACK_FLOAT16_2 sentinel | Ours saturates to 0x7FFF (a64_seq_vector.cc:1284-1322). |
| xeniamac | 7d5b737e4b, f559c185bd, 371829822a | D3DCOLOR NaN via FMAXNM/FMINNM | Ours uses `fmaxnm/fminnm` (a64_seq_vector.cc:1237-1274, 1390-1446). |
| xeniamac | 7b49b1884c, 4e8aa29f0b, 114fe514dc | PACK 8_IN_16 / 16_IN_32 source ordering | Ours narrows src1 to low, src2 to high (a64_seq_vector.cc:1478-1533), which is the non-Windows branch of 7b49b1884c. The `#ifdef _WIN32` swap in 7b49b1884c is a test-harness artifact, not an ISA difference. |
| xeniamac | 4f240879e7 | vmrglh and mulhdu | `Value::MulHi` INT64 `>> 64` fix already in ours (value.cc:390-428, ported from xenia-edge 643c13668). PERMUTE int16 present. |
| xeniamac | 114fe514dc (MUL_ADD_V128 part) | FMLA -> FMUL+FADD | Ours keeps single-rounding `fmla` on purpose (a64_sequences.cc:5630-5639). |
| xeniamac | e6b539f2ae | Linux ARM64 build, SCHED_FIFO fallback | Ours already tolerates `pthread_setschedparam` EPERM (threading_posix.cc:806-812). Rest is premake/docs. |
| xeniamac | 1380e02d03, 12b471158f, 923309ce4b, ccabb8266f, 1f06ca48f6, d42c583d23, ec635175f5 | mac indirection table, MAP_JIT, memory_mac | macOS address-space and oaknut code cache. Our posix code cache (632 lines) is a different design. |
| xeniamac | a558779537, 8f05f62f9b | TBL constants 0xFF -> 0x10 | No-op: ARM64 TBL zeroes any index >= table size, so 0xFF already yields 0. |
| xeniamac | 24496c931a, 1dc851ce60 | clang warnings, clang-format | Casts and `[[maybe_unused]]` only; no behavior change. |
| xeniamac | remaining 26 of the 50 | macOS files, premake, cleanup, stdout, docs | macOS only. |
| mac-rebase | bffc4dcf56 | Large stack local offsets | Ours uses `PrepareLocalBase` / `PrepareLocalImm` (a64_sequences.cc:4424-4429). |
| mac-rebase | 7dbc255268 | Mask vector SHR immediates | Ours masks unconditionally (a64_seq_vector.cc:717-735). |
| mac-rebase | 27ba593b09 | Constant F64<-F32 | Ours handles `is_constant` (a64_sequences.cc:5013-5021). |
| mac-rebase | ee7ca234f0 | vE offset on >4K granularity | Present (ppc_context.h:458-483). |
| mac-rebase | 69729ef711 | POSIX signal handler tracking `!= -1` | Present (threading_posix.cc:146). |
| mac-rebase | fac8dc3b4e | Harden POSIX mapping | Ours has `MAP_FAILED` check, `ftruncate64` check, and `Protect` return status (mapped_memory_posix.cc:69-78, memory_posix.cc:205-213, 322). |
| mac-rebase | 04b926c090 | Guard physical page conversion | Our `SystemPageGuestAccess` (memory.h:301-312) bounds-checks; the patched `SystemPagenumToGuestPagenum` does not exist in ours. |
| mac-rebase | 87360cc7a1 | Size infocache mapping | No `XexInfoCache` in our xex_module.cc. |
| mac-rebase | 0ef09abd61, 468bcd6854 | Thread handle release, thread name | `XE_PLATFORM_MAC` guarded; no Android path. |
| mac-rebase | the 22 Metal / macOS UI / build / import commits | | macOS only. |
| arm64fork | 16d02de18d | RESERVED_STORE LSE path leaving labels unbound | Already fixed in ours: LSE branch ends with `e.b(done); } else { ... }` (a64_seq_memory.cc:1565-1700). This was a real infinite-loop bug on LSE hardware; ours does not have it. |
| arm64fork | 6b111de79f | Add files via upload | 19 MB binary. |
| XenDroid vrs | a979f7cf69, f1ecbf7783, 23830e49a4, 1a51d62bc0, 4eb8eafd2b | Extension enable, per-draw rate, dynamic state, rates to 4x4, counters | Present in ours since 39e185ec06 (2026-06-19): `gpu_vrs_foliage_rate`, `gpu_vrs_heavy_pass_rate`, `kVrsRates` 1x1..4x4, `VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR` (vulkan_pipeline_cache.cc:2765), `vrs_base_draws_` / `vrs_escalated_draws_` counters. |
| XenDroid vrs | 7fe6c0f01a | Expose coarse shading in settings | Kotlin/Compose. Our shell is jp.xenia.emulator. |
| XenDroid mc | all 11 | Multi-controller slots, per-pad keymap, rumble | XenDroid app and its own `emulator-core` JNI driver. Feature work, not stability or speed. The 5-line cvar `ask_players_on_start` in profile_manager.cc has no kernel effect. |

## XenDroid VRS verdict

What it does: per-draw variable rate shading. When a draw is in scope (blended, depth-tested, or all draws, by `vulkan_vrs_scope`) and its pixel shader has at least `vulkan_vrs_min_shader_dwords` (48) microcode dwords, the draw is shaded at 2x1, 2x2, 4x2, or 4x4 fragments per shader invocation. Default is off (`vulkan_vrs_blended = 0` after 773efcdd05; it was 2x1 on blended draws before).

Extension: `VK_KHR_fragment_shading_rate`, pipeline rate only (`pipelineFragmentShadingRate` feature), set with `vkCmdSetFragmentShadingRateKHR` under `VK_DYNAMIC_STATE_FRAGMENT_SHADING_RATE_KHR`. Supported rates are read with `vkGetPhysicalDeviceFragmentShadingRatesKHR`.

Turnip on Adreno 740: our own cvar text (gpu_flags.cc:639) records the extension as "confirmed present on both Thor drivers". XenDroid's clamp commit records that on Adreno the 4x rates are single-sample only and 2x2 is the ceiling on any multisampled target.

Size: 8 commits, about 300 inserted lines of C++ across vulkan_command_processor.cc/.h, vulkan_pipeline_cache.cc, deferred_command_buffer.cc/.h, vulkan_device.cc/.h, plus Kotlin settings.

Is it a speed lever we lack: no. We already have the lever, with more selection modes (alpha-test, blend, per-pass escalation after N draws, guest-uptime gate, A/B alternation). What we lack are three details from 773efcdd05 and 81cfbe17c9: (1) the sample-count clamp table, which our cvar text claims but our code does not do; (2) emitting the rate only on change per render pass instead of on every draw; (3) the pixel-shader-size and depth-test gates as an alternative to our blend-state gate. Those three are rows 1 and 2 above, about 110 lines total.
