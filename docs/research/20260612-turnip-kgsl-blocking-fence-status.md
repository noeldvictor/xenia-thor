# Turnip/KGSL: fence STATUS queries block until GPU completion (root cause + driver patch)

**Date:** 2026-06-12. **Found via** the xenia-thor B85/B86 frame-serialization investigation
(worklog `2026-05-30-uma-and-cross-game-3d.md` B85-B86f).

## The bug

`vkGetFenceStatus` (and any `vkWaitForFences` with a sub-1ms timeout) on a **submitted,
not-yet-retired** fence on Turnip-over-KGSL **blocks until the fence's submission retires**,
instead of returning `VK_NOT_READY`/`VK_TIMEOUT` immediately.

Chain (all source-verified):
1. Mesa's common Vulkan runtime implements `vkGetFenceStatus` as `vk_sync_wait` with
   `abs_timeout_ns = 0` (there is no separate get-status op in `vk_kgsl_sync_type`).
2. The KGSL backend (`src/freedreno/vulkan/tu_knl_kgsl.cc`, `kgsl_syncobj_wait`) handles the
   timeout-0 poll early ONLY for the `KGSL_SYNCOBJ_STATE_UNSIGNALED` state (not yet submitted).
   For `KGSL_SYNCOBJ_STATE_TS` (submitted) it calls `wait_timestamp_safe`, which converts the
   absolute timeout to **relative milliseconds** (`get_relative_ms(0) == 0`) and issues
   `IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID` with `.timeout = 0`.
3. The downstream KGSL kernel (sm8550 `graphics-kernel/adreno_drawctxt.c`,
   `adreno_drawctxt_wait`) documents and implements: **"If timeout is 0, wait forever"**.

Consequence for apps: any per-frame fence-status poll on in-flight work serializes the CPU to
the GPU. In xenia this cost a full GPU frame of CPU block per frame (Burnout Revenge: 46.9 ms
of an 82 ms frame) plus the submit-time remainder; removing the polls app-side
(`vulkan_lazy_completion_polls`) yielded +46% (Burnout) / +78% (BTTF) / +30% (Blue Dragon).
Note the same trap applies to ANY `vkWaitForFences` timeout below 1 ms (rounds down to 0 ms
relative → wait-forever).

## The driver-side fix (for our shipped Turnip build, and upstreamable)

For a timeout-0 poll on a `STATE_TS` syncobj, do a true non-blocking query of the RETIRED
timestamp instead of the wait ioctl. All primitives already exist in the file:

```c
   case KGSL_SYNCOBJ_STATE_TS: {
      if (abs_timeout_ns == 0) {
         /* Status poll. The wait ioctl must not be used here: the downstream
          * KGSL kernel treats timeout==0 as an infinite wait
          * (adreno_drawctxt.c: "If timeout is 0, wait forever"), turning
          * vkGetFenceStatus on an in-flight fence into a blocking wait.
          * Query the retired timestamp non-blockingly instead.
          */
         struct kgsl_cmdstream_readtimestamp_ctxtid read = {
            .context_id = s->queue->msm_queue_id,
            .type = KGSL_TIMESTAMP_RETIRED,
         };
         int ret = safe_ioctl(device->fd,
                              IOCTL_KGSL_CMDSTREAM_READTIMESTAMP_CTXTID, &read);
         if (ret == 0 && timestamp_cmp(read.timestamp, s->timestamp))
            return VK_SUCCESS;
         return VK_TIMEOUT;
      }
      return wait_timestamp_safe(device->fd, s->queue->msm_queue_id,
                                 s->timestamp, abs_timeout_ns);
   }
```

Also worth auditing in the same pass (same trap, lower frequency):
- `kgsl_syncobj_wait_any` timeout-0 path (poll loop over fds + the TS branch).
- `get_relative_ms` rounding any timeout < 1 ms down to the dangerous 0 (round UP to 1 ms for
  nonzero timeouts as a belt-and-braces fix).

## Status

- **xenia-side mitigation SHIPPED and device-validated** (`vulkan_lazy_completion_polls`,
  default-ON launcher toggle "Non-blocking GPU completion checks"): never query fence status
  beyond what an await strictly requires. See worklog B86-B86f.
- **Driver patch: NOT yet built** — requires a Mesa Android cross-build of our shipped Turnip
  (v26.0.0_R7 for Adreno 740, see [[turnip-driver-integration]]). With the xenia-side fix in
  place the patch is belt-and-braces for xenia but fixes the entire class for anything else
  running on this driver (RetroArch cores, other emulators, the presenter paint path's rare
  eager polls).
- **Upstream:** report to Mesa (gitlab.freedesktop.org/mesa/mesa) — the kgsl backend's
  assumption that ioctl timeout=0 means poll is wrong against the production downstream kernel.

## BUILT + DEVICE-VALIDATED (2026-06-15)

The patch above was applied to upstream Mesa and cross-compiled to a working Turnip ICD for the Thor.

**Build recipe (WSL/Linux; ~20 min):**
1. Linux Android NDK r27c (the local Windows NDK is unusable from WSL): download
   android-ndk-r27c-linux.zip; extract with a SYMLINK+PERM-PRESERVING extractor (python's
   `-m zipfile` corrupts the clang->clang-18 symlinks - use `scratch/turnip-build/unzip_symlinks.py`).
2. `git clone --depth 1 https://gitlab.freedesktop.org/mesa/mesa.git` (was 26.2.0-devel; the patch site
   `src/freedreno/vulkan/tu_knl_kgsl.cc` kgsl_syncobj_wait STATE_TS case + all primitives in msm_kgsl.h
   matched verbatim).
3. Apply: `scratch/turnip-build/apply_turnip_patch.py` (run from the mesa root).
4. Meson cross-file: NDK linux-x86_64 clang/clang++/llvm-ar/llvm-strip + lld, host_machine=android/aarch64.
   SINGLE quotes (meson rejects double).
5. `meson setup build-android --cross-file <cross> -Dplatforms=android -Dplatform-sdk-version=33
   -Dandroid-stub=true -Dgallium-drivers= -Dvulkan-drivers=freedreno -Dfreedreno-kmds=kgsl -Dopengl=false
   -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dglx=disabled -Dglvnd=disabled -Dbuildtype=release
   -Dcpp_rtti=false -Db_lto=false` (Vulkan-only: GL/EGL must be disabled or it errors on glapi_xml_py_deps;
   LLVM not needed).
6. `ninja -C build-android src/freedreno/vulkan/libvulkan_freedreno.so` -> a 16.8MB ICD.

**Deploy (non-root, debuggable app):** the driver MUST live in the app's private files dir for libadrenotools'
dlopen namespace (run-as can't read /data/local/tmp or /sdcard, and adb shell mangles binary stdin) -> push
via `base64 file | adb shell "run-as <pkg> sh -c 'base64 -d > files/turnip-patched/libvulkan_freedreno.so'"`.
Load with `--es gpu_vulkan_driver turnip --es gpu_vulkan_driver_path /data/data/<pkg>/files/turnip-patched/
--es gpu_vulkan_driver_lib libvulkan_freedreno.so --es gpu_vulkan_driver_hooks_path <app>/lib/arm64`.

**libc++ gotcha:** the build dynamically links libc++_shared.so, which is NOT in the driver's adreno
namespace -> first load failed `dlopen: library "libc++_shared.so" not found`. Workaround used: ship the
NDK's libc++_shared.so alongside in the same dir (the namespace searches it). CLEANER (matches the existing
static vulkan.ad07xx.so): rebuild with `-static-libstdc++` so the ICD is self-contained (TODO for a
packageable ADPKG).

**Validation (Burnout Revenge):** the patched ICD LOADS + is ACTIVE ("Turnip Adreno (TM) 740", driverID
MesaTurnip), renders the race PIXEL-correct at 61.7fps (= the 60fps frame cap), no crash, with the app-side
`vulkan_lazy_completion_polls` workaround OFF. This validates the build + the patch's CORRECTNESS (functional,
no regression). The fence-fix's PERF delta is NOT isolated on this cap-bound light highway (the bug only
drops fps on sub-60fps heavy scenes; the app-side workaround already captures that win). Next: a heavy
traffic-scene A/B (or uncapped) to quantify the driver patch's incremental fence-wait reduction vs the
workaround; and a -static-libstdc++ rebuild for a self-contained shippable driver. Artifacts:
scratch/turnip-build/ (the patched .so, the patch+extract scripts).
