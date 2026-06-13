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
