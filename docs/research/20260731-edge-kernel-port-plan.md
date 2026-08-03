# Edge kernel+vfs port — migration plan (scoped 2026-07-31)

User decision: XenDroid (vendors xenia-edge) is more stable; stop porting
kernel fixes one at a time and take Edge's whole kernel layer, keeping our
CPU/GPU. This is the scoping result driving tasks #13-#16.

## Size
Merge-base of master and edge/edge: **95a5c3ee2** (same as canary).
`git diff --stat master edge/edge -- src/xenia/kernel src/xenia/vfs`:
**253 files, +35,576/-10,762** (kernel 189 files; xam 80 = +16,851 —
profile/XDBF/achievements is the biggest cluster; xboxkrnl 34; vfs 64).
110 files Edge-only (profile_manager/user_tracker/xdbf/*, xam/ui/* ImGui
dialogs, xconfig/smc/kernel.h, xcontent_devices/*, zarchive device).
10 files ours-only: premake5.lua files (KEEP - recursive_platform_files
auto-globs new sources), gameinfo_utils/xdbf_utils (superseded),
stfs_container_device.* (renamed into xcontent_devices/),
**xboxkrnl_cpp_eh.h (OURS - preserve; 4 cpu tests pin it)**.

## Our delta to re-apply
`git diff 95a5c3ee2 master -- src/xenia/kernel src/xenia/vfs` =
**53 files, +6,286/-238** — almost purely additive, so `git apply -3` of the
snapshot patch should mostly land. Headline: **guest scheduler is
Edge-origin; our guest_scheduler.h is byte-identical to Edge's** — taking
Edge UPGRADES it (only re-apply: Win32-native-fiber backend,
guest_scheduler_jit_safepoints cvar, Fence::WaitFor dep).
**All 8 hand-ported canary-parity exports already exist in Edge**
(XeCryptBnQwBeSigVerify, XeCryptRotSumSha, XeKeysGetKey, HalGetCurrentAVPack,
RtlCompareString/N, ExAllocatePoolWithTag, XamShowMessageBoxUIEx, MmIsAddress
Valid + XamSwapDisc/XamLoaderGetDvdTrayState/VdIsHSIOTrainingSucceeded) —
they evaporate on port; the empirical case for wholesale.
Re-apply list (full table in the scoping session): trainers (no Edge equiv),
disc-swap replumb onto GetDiscPathForNumber, ntreadfile_force_complete, io
trace cvars, kernel_call_log_skip_discarded (~26% of a BD frame - hook point
moves, Edge rewrote shim_utils.h to PPCContext::TranslateVirtual style),
handle cache (Edge rewrote object_table +346 AND added guest_object_table),
guest C++ EH (+564 in xboxkrnl_debug.cc which Edge NET-DELETED 607 lines
from), a64 APC fastpath 4-liner in xthread (CHECKLIST - silently degrades
codegen if forgotten), thor_guest_thread_affinity_mask, Android IME/SAF
gates (coexist with Edge's different pthread_cleanup Android blocks), video
cvar reconciliation (ours kernel_video_widescreen vs Edge widescreen/
interlaced/avpack), xam UI cvars, font-cache redirect, memory diag cvars.
Hardest three: **xboxkrnl_memory.cc** (Edge -980 rewrite, logic moved into
memory.cc +1,424; MmIsAddressValid commit semantics feed the trainers),
**guest C++ EH**, **object-table handle cache**.

## Interface surface Phase 0 must add (additive on master, tree green)
- src/xenia/guest_pointers.h (new) + PPCContext::TranslateVirtual/BE/GPR
  helpers (~35 lines). DO NOT take Edge's PPCContext field reorder/alignas -
  a64_emitter hardcodes offsetof (4439/4463).
- base: pe_image.h, png_utils.h; threading NanoSleep/NanoSleepPrecise,
  Event::Query()->EventInfo, Event::SetBoostPriority(). KEEP ours:
  Fence::WaitFor + probe, Thread::WaitForSuspendAcknowledged, thor_topology,
  native-fiber backends (Edge uses boost_context - do not take).
- Memory: GetPhysicalPageWindowAccess, GetHeapsPageStatsSummary,
  SetPhysicalAliasSkipHostProtect + BaseHeap::skip_host_protect,
  Register/UnregisterPhysicalMemoryReadCallback, BaseHeap::AllocSystemHeap/
  GetPageProtect/GetPageAccess/page counts, SystemHeapFree out-size,
  TranslateVirtual(TypedGuestPointer), IsWritableProtect.
- Processor: GuestAtomic{Increment,Decrement,Or,Xor,And,CAS}32, RemoveModule,
  RemoveFunctionByAddress, LookupModule. KEEP RaiseIrql/LowerIrql/
  raw_irql_ptr_for_a64/ExecuteInterrupt dual-path until Phase 4.
- GraphicsSystem: GetInternalDisplayResolution (static) +
  SetScaledAspectRatio (~20 lines; the ENTIRE gpu surface).
- apu: audio_media_player.{cc,h} + xmp_state.h (21 kernel call sites) +
  Emulator::audio_media_player().
- ui (ImGui-only, do NOT take wxWidgets): imgui_notification,
  imgui_guest_notification, imgui_host_notification, imgui_gamepad_dialog,
  audio_helper.
- third_party: pugixml (+premake wrapper) required (xlast). Defer zarchive
  (stub the .zar device), zlib-ng, tomlplusplus (keep our cpptoml patcher;
  stub Emulator::plugin_loader() -> nullptr).
- xbox.h: take Edge's wholesale (+610: XNotificationID etc).
- Emulator adds: audio_media_player(), content_root() exists?, game_info_
  database(), on_launch_new_title, on_disc_swap/RecordDisc/GetNewDiscPath/
  MountPath/is_title_open, LaunchDiscArchive/MountStandardDrives/
  CreateVfsDevice/GetFileSignature/ContentInstallEntry. KEEP ours:
  GetDiscPathForNumber, LoadTrainersForTitle, GameConfigLoadCallback.

## Strategy: (b') git-assisted wholesale, staged
Phase 0 foundations (above) -> Phase 1 snapshot patch -> Phase 2 branch
kernel-port + read-tree path-scoped 3-way merge vs 95a5c3ee2 (real conflict
markers, NOT checkout overwrite) -> Phase 3 re-apply cheapest-first ->
Phase 4 IRQL unification LAST (only change reaching the a64 backend:
a64_backend.cc:3207 processor_irql -> KPCR-resident IRQL via xeKfRaiseIrql).
Oracles: 4 cpu EH tests, base+ppc suites, BD desktop boot, APK build.
Risk is asymmetric: GPU surface ~free, CPU surface is the expensive one.

## EXECUTION LOG (2026-08-03) — Phase 2 complete on desktop

Branch `kernel-port`. Sequence that worked:
1. `git checkout edge/edge -- src/xenia/kernel src/xenia/vfs`; `git rm` the 6
   superseded files; `git apply -3 <delta.patch>` EXACTLY ONCE (it is NOT
   idempotent - a second run duplicates insert hunks; and `--stat` on
   `git apply` is not a dry run, it applies).
2. 173 conflict markers / 48 files resolved by three parallel agents on
   disjoint file groups. **All three independently found the merge's
   conflict labels were INVERTED** (`<<<<<<< ours` held EDGE content).
   Resolve by content, never by label.
3. ~1100 compile errors -> 0. The big root causes, each worth hundreds:
   - **C++17 -> C++20** (Edge headers use concepts/requires/span). This one
     switch removed the majority of the initial error count.
   - **XE_RESTRICT / XE_FORCEINLINE / XE_LIKELY_IF undefined in our tree**:
     shim_utils.h (included by every kernel TU) used them, so its
     ContextParam member decl was garbage and everything after derailed.
     MSVC blamed unrelated headers (audio_driver.h, DXGI client.h).
   - **ui/imgui_drawer.h forward-declared ImGui instead of including it** -
     ~500 errors across the byte-identical xam/ui/*.cc dialogs.
   - **xthread.h tested XE_PLATFORM_WIN32 before including platform.h**, so
     <csetjmp> was skipped while the same #if 620 lines later was true.
   TECHNIQUE: when MSVC blames a system/3rd-party header, compile the
   suspect header ALONE in a minimal TU - it points at the real line
   immediately. (cl.exe must be run via PowerShell; MSYS mangles /flags.)
4. Oracles (all pass): base 3508 assertions, cpu 1605, **ppc 1481/1481**,
   guest-EH subset 83. Test tools needed kernel/apu/hid/vfs/gpu/ui/imgui/
   pugixml links added, and their pre-existing GPU link stubs removed or
   gated to !AMD64 (now duplicate definitions).
5. Android (NDK clang, -std=c++20 -Werror) surfaces MSVC-vs-clang gaps, not
   merge damage: -Wdeprecated-volatile (++ and |= on volatile members),
   std::ranges absent from NDK libc++, mixed-enum arithmetic, constexpr
   returning std::string, -Wswitch. Fix portably (both toolchains), never
   by disabling warnings. ALWAYS dedupe the gradle log - each error repeats
   per TU and buries the distinct ones.

Deliberate omissions (all NOTE(kernel-port) with re-enable paths): XMP host
decode (needs FFmpeg bump + Edge audio_driver split), achievement sound
(miniaudio not vendored), zarchive .zar discs, plugin_loader (we keep the
cpptoml patcher), Processor::RemoveModule/RemoveFunctionByAddress (our
EntryTable + LLVM resolve caches assume no deletion - needs an invalidation
design first), Emulator::RelaunchTitle/DataMigration, in-process
title-relaunch defaults FALSE.

## EXECUTION LOG (2026-08-03, later) — device bring-up of the merged kernel

The merge compiled and passed every desktop oracle while being **unable to boot
at all on the device**. Five distinct defects, found in order, each hidden
behind the previous one. Two general lessons first:

1. **Desktop oracles cannot validate this port.** Everything that broke was in
   memory_posix / threading_posix / Android-only call paths. 1481/1481 PPC tests
   passed against a binary that segfaulted 300ms into startup.
2. **The merge scope (kernel + vfs) was too narrow.** Edge also changed how code
   OUTSIDE those directories calls into the kernel. Three of the five defects
   were missing caller-side changes in gpu/, apu/ and emulator.cc. When porting
   a subsystem wholesale, diff the CALLERS too — `git show edge/edge:<file>` on
   every file that touches the ported API.

### The defects
- **MAP_FIXED_NOREPLACE in MapFileView** (mine, b11dadbd8): guest views map INTO
  the reserved region, so NOREPLACE always fails. Memory init failed → crash
  loop. Reverted in 4d78e1055.
- **AllocFixed commit via mprotect unconditionally** (same batch): BaseHeap
  tolerates a commit on an unreserved page ("attempting commit on unreserved
  page" — it logs and continues) and the original anonymous MAP_FIXED made that
  work. mprotect on unmapped memory returns ENOMEM → nullptr to callers that
  don't check. Now gated on IsInsideMappedFileRange.
- **Host threads created before title load** (missing Edge call-site change):
  XThread::Create inserts into the process's guest thread_list; the default
  TITLE process is not initialized until SetExecutableModule, so blink_ptr == 0
  and the insert wrote through guest address 0. Edge names GetIdleProcess() /
  GetSystemProcess() at all four sites (command_processor, graphics_system,
  audio_system, xma_decoder).
- **FinishLoadingUserModule never called** (missing Edge call-site change): Edge
  split module loading in two; CompleteLaunch only called the first half, so
  guest_xex_header_ stayed 0 and GetOptHeader read membase+0x14. Its guard
  null-checked the TRANSLATED pointer, and TranslateVirtual(0) is the membase —
  never null. Check guest pointers, not translated ones.
- **Main guest thread never started**: Edge's CompleteLaunch ends with
  main_thread_->Resume() (missing here), AND PosixThread's create-suspended
  handshake had a lost-wakeup race — state_ = kSuspended was published before
  suspend_count_ = 1, so a Resume in that window decremented a zero count,
  wrapped it to ~4 billion, and never signalled. Would strand ANY
  create-suspended thread on POSIX.

### Diagnosis infrastructure added (d087989ea) — this is what made it tractable
Every one of these died with NO diagnostic: no handler log, no crash_log, no
tombstone. Causes, all fixed: XELOGE from a fault handler is lost because the
logger is asynchronous (use FaultLog → __android_log_vprint, synchronous);
dereferencing mcontext.pc faults again inside the handler when the pc is what's
unmapped (read via pread on /proc/self/mem); no SA_ONSTACK/sigaltstack so a
stack-overflow fault could not run the handler at all. Plus a symbolized
_Unwind_Backtrace on the fatal path. Symbolize with
`llvm-addr2line -f -C -e <intermediates>/obj/local/arm64-v8a/libxenia-app.so <offset>`
using the `libxenia-app.so+OFFSET` values the backtrace prints.

### State at end of session
Burnout Revenge: loads, creates its thread pool, main XThread runs at ~92% CPU,
GPU temp climbs under load. **Screen still black — no frame presented yet.**
Note this was measured via `am start`, which does NOT install Turnip (the log
shows the Qualcomm proprietary driver, AdrenoVK-0) — CLAUDE.md requires Turnip,
so re-test from the in-app GUI launch before drawing any conclusion about the
black screen.
