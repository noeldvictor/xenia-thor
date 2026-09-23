/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_GPU_SHARED_MEMORY_H_
#define XENIA_GPU_SHARED_MEMORY_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/memory.h"

namespace xe {
namespace gpu {

// Manages memory for unconverted textures, resolve targets, vertex and index
// buffers that can be accessed from shaders with Xenon physical addresses, with
// system page size granularity.
class SharedMemory {
 public:
  // Per-frame counters of the vertex/index residency machinery, read and
  // reset by the command processor's per-frame CPU line (2026-09-22: Gears
  // spent ~30 ms per frame in RequestRange on the device, 4.8 ms in the PC
  // replay; these name the part - the lock, the copy, or the watch).
  struct Stats {
    uint64_t request_calls = 0;
    uint64_t request_fast = 0;     // answered by the lock-free valid check
    uint64_t upload_calls = 0;
    uint64_t upload_pages = 0;
    uint64_t upload_ns = 0;        // UploadRanges, copy + MakeRangeValid
    uint64_t protect_ns = 0;       // re-arming the write watch
    uint64_t lock_ns = 0;          // waiting for the global lock in RequestRange
    uint64_t invalidations = 0;    // write-watch hits (any thread)
    uint64_t request_zero_copy = 0;  // buffer requests with nothing to do
    // gpu_uma_hazard_check: CPU writes to a page that a GPU submission still
    // reads. [0] vertex/index buffers, [1] everything else (textures,
    // resolves, memexport). "definite": the submission was not even
    // submitted yet; "possible": submitted, completion not seen yet.
    uint64_t hazard_definite[2] = {};
    uint64_t hazard_possible[2] = {};
  };
  Stats TakeStats() {
    Stats s = stats_;
    s.invalidations = stat_invalidations_.exchange(0, std::memory_order_relaxed);
    for (uint32_t i = 0; i < 2; ++i) {
      s.hazard_definite[i] =
          stat_hazard_definite_[i].exchange(0, std::memory_order_relaxed);
      s.hazard_possible[i] =
          stat_hazard_possible_[i].exchange(0, std::memory_order_relaxed);
    }
    stats_ = Stats();
    return s;
  }
  static bool StatsEnabled();

  static constexpr uint32_t kBufferSizeLog2 = 29;
  static constexpr uint32_t kBufferSize = 1 << kBufferSizeLog2;

  virtual ~SharedMemory();
  // Call in the implementation-specific ClearCache.
  virtual void ClearCache();
  virtual void SetSystemPageBlocksValidWithGpuDataWritten();

  // Call when a new GPU submission starts recording - resets the per-submission
  // invalidation tracking used to decide which uploads may be hoisted to the
  // submission head. See SystemPageFlagsBlock::invalidated_in_submission.
  void OnGpuSubmissionOpened();

  // True if ANY page in the given ranges lost validity since the current GPU
  // submission opened. Ranges are (first_page, last_page), inclusive. When this
  // is false for an upload's pages, that upload may be moved to the head of the
  // submission without breaking the current render pass.
  bool AnyPageInvalidatedSinceSubmissionOpen(
      const std::pair<uint32_t, uint32_t>* page_ranges, uint32_t count) const;

  typedef void (*GlobalWatchCallback)(
      const std::unique_lock<std::recursive_mutex>& global_lock, void* context,
      uint32_t address_first, uint32_t address_last, bool invalidated_by_gpu);
  typedef void* GlobalWatchHandle;
  // Registers a callback invoked when something is invalidated in the GPU
  // memory copy by the CPU or (if triggered explicitly - such as by a resolve)
  // by the GPU. It will be fired for writes to pages previously requested, but
  // may also be fired regardless of whether it was used by GPU emulation - for
  // example, if the game changes protection level of a memory range containing
  // the watched range.
  //
  // The callback is called within the global critical region.
  GlobalWatchHandle RegisterGlobalWatch(GlobalWatchCallback callback,
                                        void* callback_context);
  void UnregisterGlobalWatch(GlobalWatchHandle handle);
  typedef void (*WatchCallback)(
      const std::unique_lock<std::recursive_mutex>& global_lock, void* context,
      void* data, uint64_t argument, bool invalidated_by_gpu);
  typedef void* WatchHandle;
  // Registers a callback invoked when the specified memory range is invalidated
  // in the GPU memory copy by the CPU or (if triggered explicitly - such as by
  // a resolve) by the GPU. It will be fired for writes to pages previously
  // requested, but may also be fired regardless of whether it was used by GPU
  // emulation - for example, if the game changes protection level of a memory
  // range containing the watched range.
  //
  // Generally the context is the subsystem pointer (for example, the texture
  // cache), the data is the object (such as a texture), and the argument is
  // additional subsystem/object-specific data (such as whether the range
  // belongs to the base mip level or to the rest of the mips).
  //
  // Called with the global critical region locked. Do NOT watch or unwatch
  // ranges from within it! The watch for the callback is cancelled after the
  // callback - the handle becomes invalid.
  WatchHandle WatchMemoryRange(uint32_t start, uint32_t length,
                               WatchCallback callback, void* callback_context,
                               void* callback_data, uint64_t callback_argument);
  // Unregisters previously registered watched memory range.
  void UnwatchMemoryRange(WatchHandle handle);

  // Checks if the range has been updated, uploads new data if needed and
  // ensures the host GPU memory backing the range are resident. Returns true if
  // the range has been fully updated and is usable.
  bool RequestRange(uint32_t start, uint32_t length);
  // For a range the GPU only reads as a buffer (vertex fetch, guest indices).
  // With unified-memory zero-copy the GPU buffer is the guest memory and is
  // always current: no residency bookkeeping, no lock and no write watch, so
  // the next CPU write to the page does not fault
  // (gpu_uma_skip_buffer_watches). Otherwise the same as RequestRange.
  bool RequestBufferRange(uint32_t start, uint32_t length);
  // True when the GPU buffer is the guest's physical memory (gpu_uma_zero_copy).
  bool is_zero_copy() const { return zero_copy_; }
  // Returns whether every page in the range is currently valid in the host GPU
  // memory copy. Hold the global critical region if relying on this for state
  // transitions such as watch installation.
  bool IsRangeValid(uint32_t start, uint32_t length) const;

  // Marks the range and, if not exact_range, potentially its surroundings
  // (to up to the first GPU-written page, as an access violation exception
  // count optimization) as modified by the CPU, also invalidating GPU-written
  // pages directly in the range.
  std::pair<uint32_t, uint32_t> MemoryInvalidationCallback(
      uint32_t physical_address_start, uint32_t length, bool exact_range);

  // Marks the range as containing GPU-generated data (such as resolves),
  // triggering modification callbacks, making it valid (so pages are not
  // copied from the main memory until they're modified by the CPU) and
  // protecting it. Before writing anything from the GPU side, RequestRange must
  // be called, to make sure, if the GPU writes don't overwrite *everything* in
  // the pages they touch, the CPU data is properly loaded to the unmodified
  // regions in those pages.
  void RangeWrittenByGpu(uint32_t start, uint32_t length);

 protected:
  SharedMemory(Memory& memory);
  // For gpu_uma_hazard_check: the submission being recorded now, and the last
  // one seen complete. The base class knows no submissions (0 = off).
  virtual uint64_t HazardCurrentSubmission() const { return 0; }
  virtual uint64_t HazardCompletedSubmission() const { return 0; }
  // Set by the implementation when its GPU buffer is bound to the guest's
  // physical memory itself (gpu_uma_zero_copy): an upload copies nothing.
  bool zero_copy_ = false;
  // Call in implementation-specific initialization.
  bool InitializeCommon();
  void InitializeSparseHostGpuMemory(uint32_t granularity_log2);
  // Call last in implementation-specific shutdown, also callable from the
  // destructor.
  void ShutdownCommon();

  // Sparse allocations are 4 MB, so not too many of them are allocated, but
  // also not to waste too much memory for padding (with 16 MB there's too
  // much).
  static constexpr uint32_t kHostGpuMemoryOptimalSparseAllocationLog2 = 22;
  static_assert(kHostGpuMemoryOptimalSparseAllocationLog2 <= kBufferSizeLog2);

  Memory& memory() const { return memory_; }

  uint32_t page_size_log2() const { return page_size_log2_; }

  uint32_t host_gpu_memory_sparse_granularity_log2() const {
    return host_gpu_memory_sparse_granularity_log2_;
  }

  // Allocations in the host buffer are aligned the same way as in the guest
  // physical memory (for instance, if an allocation is 64 KB, it can represent
  // 0-64 KB, 64-128 KB, 128-192 KB in the guest memory, and so on, but not
  // something like 16-80 KB. This is assumed by the rules for texture data
  // access in the texture cache.
  virtual bool AllocateSparseHostGpuMemoryRange(uint32_t offset_allocations,
                                                uint32_t length_allocations);

  // Mark the memory range as updated and protect it.
  void MakeRangeValid(uint32_t start, uint32_t length, bool written_by_gpu);

  // Uploads a range of host pages - only called if host GPU sparse memory
  // allocation succeeded if needed. While uploading, MakeRangeValid must be
  // called for each successfully uploaded range as early as possible, before
  // the memcpy, to make sure invalidation that happened during the CPU -> GPU
  // memcpy isn't missed (upload_page_ranges is in pages because of this -
  // MakeRangeValid has page granularity). upload_page_ranges are sorted in
  // ascending address order, so front and back can be used to determine the
  // overall bounds of pages to be uploaded.
  virtual bool UploadRanges(
      const std::vector<std::pair<uint32_t, uint32_t>>& upload_page_ranges) = 0;

  const std::vector<std::pair<uint32_t, uint32_t>>& trace_download_ranges() {
    return trace_download_ranges_;
  }
  uint32_t trace_download_page_count() const {
    return trace_download_page_count_;
  }
  // Fills trace_download_ranges() and trace_download_page_count() with
  // GPU-written ranges that need to be downloaded, and also invalidates
  // non-GPU-written ranges so only the needed data - not the all the collected
  // data - will be written in the trace. trace_download_page_count() will be 0
  // if nothing to download.
  void PrepareForTraceDownload();
  // Release memory used for trace download ranges, to be called after
  // downloading or in cases when download is dropped.
  void ReleaseTraceDownloadRanges();

 private:
  Memory& memory_;

  // Log2 of invalidation granularity (the system page size, but the dependency
  // on it is not hard - the access callback takes a range as an argument, and
  // touched pages of the buffer of this size will be invalidated).
  uint32_t page_size_log2_;

  bool EnsureHostGpuMemoryAllocated(uint32_t start, uint32_t length);
  uint32_t host_gpu_memory_sparse_granularity_log2_ = UINT32_MAX;
  std::vector<uint64_t> host_gpu_memory_sparse_allocated_;
  uint32_t host_gpu_memory_sparse_allocations_ = 0;
  uint32_t host_gpu_memory_sparse_used_bytes_ = 0;

  void* memory_invalidation_callback_handle_ = nullptr;
  void* memory_data_provider_handle_ = nullptr;

  // Ranges that need to be uploaded, generated by GetRangesToUpload (a
  // persistently allocated vector).
  std::vector<std::pair<uint32_t, uint32_t>> upload_ranges_;

  // GPU-written memory downloading for traces. <Start address, length>.
  std::vector<std::pair<uint32_t, uint32_t>> trace_download_ranges_;
  uint32_t trace_download_page_count_ = 0;

  // Mutex between the guest memory subsystem and the command processor, to be
  // locked when checking or updating validity of pages/ranges and when firing
  // watches.
  xe::global_critical_region global_critical_region_;

  // ***************************************************************************
  // Things below should be fully protected by global_critical_region.
  // ***************************************************************************

  struct SystemPageFlagsBlock {
    // Whether each page is up to date in the GPU buffer.
    uint64_t valid;
    // Subset of valid pages - whether each page in the GPU buffer contains data
    // that was written on the GPU, thus should not be invalidated spuriously.
    uint64_t valid_and_gpu_written;
    // Whether each page lost validity since the CURRENT GPU submission opened.
    //
    // XenDroid port (904374971), tracking half. A page never invalidated while
    // the submission was recording cannot have been legitimately read by an
    // already-recorded command, so its upload is safe to reorder to the START
    // of the submission - letting it run before any render pass begins instead
    // of forcing the pass to end. On a TBDR every such pass break is a GMEM
    // store+reload, i.e. the same root cause the in-pass resolve work attacks,
    // approached from the other side.
    //
    // Cleared wholesale by OnGpuSubmissionOpened.
    uint64_t invalidated_in_submission;
  };
  // Flags for each 64 system pages, interleaved as blocks, so bit scan can be
  // used to quickly extract ranges.
  std::vector<SystemPageFlagsBlock> system_page_flags_;
  // Updated on the command processor thread only, except the atomic.
  Stats stats_;
  std::atomic<uint64_t> stat_invalidations_{0};
  // gpu_uma_hazard_check: per page, the submission that last requested it and
  // the kind of request (1 buffer, 2 other). Made on the first use.
  std::unique_ptr<uint64_t[]> hazard_page_use_;
  std::unique_ptr<uint8_t[]> hazard_page_kind_;
  uint8_t hazard_kind_override_ = 0;
  std::atomic<uint64_t> stat_hazard_definite_[2]{};
  std::atomic<uint64_t> stat_hazard_possible_[2]{};
  std::atomic<uint32_t> hazard_logged_{0};
  void HazardNoteUse(uint32_t start, uint32_t length, uint8_t kind);
  void HazardCheckWrite(uint32_t page_first, uint32_t page_last);

  static std::pair<uint32_t, uint32_t> MemoryInvalidationCallbackThunk(
      void* context_ptr, uint32_t physical_address_start, uint32_t length,
      bool exact_range);

  struct GlobalWatch {
    GlobalWatchCallback callback;
    void* callback_context;
  };
  std::vector<GlobalWatch*> global_watches_;
  struct WatchNode;
  // Watched range placed by other GPU subsystems.
  struct WatchRange {
    union {
      struct {
        WatchCallback callback;
        void* callback_context;
        void* callback_data;
        uint64_t callback_argument;
        WatchNode* node_first;
        uint32_t page_first;
        uint32_t page_last;
      };
      WatchRange* next_free;
    };
  };
  // Node for faster checking of watches when pages have been written to - all
  // 512 MB are split into smaller equally sized buckets, and then ranges are
  // linearly checked.
  struct WatchNode {
    union {
      struct {
        WatchRange* range;
        // Link to another node of this watched range in the next bucket.
        WatchNode* range_node_next;
        // Links to nodes belonging to other watched ranges in the bucket.
        WatchNode* bucket_node_previous;
        WatchNode* bucket_node_next;
      };
      WatchNode* next_free;
    };
  };
  static constexpr uint32_t kWatchBucketSizeLog2 = 22;
  static constexpr uint32_t kWatchBucketCount =
      1 << (kBufferSizeLog2 - kWatchBucketSizeLog2);
  WatchNode* watch_buckets_[kWatchBucketCount] = {};
  // Allocation from pools - taking new WatchRanges and WatchNodes from the free
  // list, and if there are none, creating a pool if the current one is fully
  // used, and linearly allocating from the current pool.
  static constexpr uint32_t kWatchRangePoolSize = 8192;
  static constexpr uint32_t kWatchNodePoolSize = 8192;
  std::vector<WatchRange*> watch_range_pools_;
  std::vector<WatchNode*> watch_node_pools_;
  uint32_t watch_range_current_pool_allocated_ = 0;
  uint32_t watch_node_current_pool_allocated_ = 0;
  WatchRange* watch_range_first_free_ = nullptr;
  WatchNode* watch_node_first_free_ = nullptr;
  // Triggers the watches (global and per-range), removing triggered range
  // watches.
  void FireWatches(uint32_t page_first, uint32_t page_last,
                   bool invalidated_by_gpu);
  // Unlinks and frees the range and its nodes. Call this in the global critical
  // region.
  void UnlinkWatchRange(WatchRange* range);
};

}  // namespace gpu
}  // namespace xe

#endif  // XENIA_GPU_SHARED_MEMORY_H_
