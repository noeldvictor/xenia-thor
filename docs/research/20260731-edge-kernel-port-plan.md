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
