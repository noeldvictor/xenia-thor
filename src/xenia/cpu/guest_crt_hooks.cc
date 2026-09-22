/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/guest_crt_hooks.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"
#include "xenia/memory.h"

DEFINE_bool(
    cpu_guest_crt_hooks, true,
    "Replace a title's statically linked C runtime (its own heap, memcpy, "
    "memset) with host code at the guest addresses the title's static "
    "recompilation names (reNut for Banjo-Kazooie: Nuts & Bolts). The heap "
    "becomes one thread-safe host allocator inside guest memory; the copies "
    "run as host NEON/SSE. A title without a table is unchanged.",
    "CPU");

namespace xe {
namespace cpu {

namespace {

// A small-block heap inside guest memory for the title's RtlAllocateHeap
// family. Blocks carry a 16-byte header (the requested size, the size class,
// a magic) so RtlSizeHeap and RtlFreeHeap need no lookup; small classes are
// carved from 1 MB pages of the emulator's system heap and recycled through
// per-class free lists; blocks over the largest class get their own pages.
// One mutex, like the critical section the game's heap was written for.
class GuestSmallHeap {
 public:
  static constexpr uint32_t kHeaderSize = 16;
  static constexpr uint32_t kMagic = 0x58454850;  // 'XEHP'
  static constexpr uint32_t kChunkSize = 1 << 20;
  static constexpr uint32_t kLargeClass = 0xFFFF;

  uint32_t Alloc(Memory* memory, uint32_t size, bool zero) {
    memory_ = memory;
    uint32_t class_index = ClassFor(size);
    if (class_index == kLargeClass) {
      uint32_t total = xe::round_up(size + kHeaderSize, 4096u);
      uint32_t base = memory->SystemHeapAlloc(total, 4096);
      if (!base) {
        return 0;
      }
      // SystemHeapAlloc zeroes.
      WriteHeader(base, size, kLargeClass);
      std::lock_guard<std::mutex> lock(mutex_);
      ++stats_large_;
      return base + kHeaderSize;
    }
    uint32_t block_size = ClassSize(class_index);
    uint32_t header;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      header = PopFree(class_index);
      bool recycled = header != 0;
      if (!header) {
        header = Carve(kHeaderSize + block_size);
        if (!header) {
          return 0;
        }
      }
      ++stats_small_;
      if (recycled) {
        ++stats_recycled_;
      }
    }
    WriteHeader(header, size, class_index);
    uint32_t block = header + kHeaderSize;
    if (zero) {
      // A fresh carve is already zero (system heap pages); a recycled block
      // is not.
      std::memset(memory->TranslateVirtual(block), 0, block_size);
    }
    return block;
  }

  // Returns false when the address is not a block of this heap.
  bool Free(Memory* memory, uint32_t block) {
    if (!block || block < kHeaderSize) {
      return false;
    }
    uint32_t header = block - kHeaderSize;
    uint32_t requested, class_index;
    if (!ReadHeader(header, &requested, &class_index)) {
      return false;
    }
    // Poison the magic so a double free is refused.
    xe::store_and_swap<uint32_t>(memory->TranslateVirtual(header + 8), 0);
    if (class_index == kLargeClass) {
      memory->SystemHeapFree(header);
      return true;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    PushFree(class_index, header);
    ++stats_freed_;
    return true;
  }

  // The requested size of a block, or UINT32_MAX when unknown.
  uint32_t Size(uint32_t block) {
    if (!block || block < kHeaderSize) {
      return UINT32_MAX;
    }
    uint32_t requested, class_index;
    if (!ReadHeader(block - kHeaderSize, &requested, &class_index)) {
      return UINT32_MAX;
    }
    return requested;
  }

  // The usable capacity of a block (its class size), 0 when unknown.
  uint32_t Capacity(uint32_t block) {
    uint32_t requested, class_index;
    if (!block || block < kHeaderSize ||
        !ReadHeader(block - kHeaderSize, &requested, &class_index)) {
      return 0;
    }
    return class_index == kLargeClass ? requested : ClassSize(class_index);
  }

  void LogStats(const char* reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    XELOGI(
        "guest CRT heap ({}): small={} recycled={} freed={} large={} "
        "chunks={} ({} MB)",
        reason, stats_small_, stats_recycled_, stats_freed_, stats_large_,
        chunks_, (uint64_t(chunks_) * kChunkSize) >> 20);
  }

 private:
  // Classes: 16-byte steps to 512 (32 classes), then powers of two to 64 KB
  // (7 classes).
  static constexpr uint32_t kSmallClasses = 32;
  static constexpr uint32_t kPowerClasses = 7;
  static constexpr uint32_t kClassCount = kSmallClasses + kPowerClasses;

  static uint32_t ClassFor(uint32_t size) {
    if (size <= 512) {
      return size == 0 ? 0 : (size - 1) / 16;
    }
    uint32_t class_size = 1024;
    for (uint32_t i = 0; i < kPowerClasses; ++i, class_size <<= 1) {
      if (size <= class_size) {
        return kSmallClasses + i;
      }
    }
    return kLargeClass;
  }
  static uint32_t ClassSize(uint32_t class_index) {
    if (class_index < kSmallClasses) {
      return (class_index + 1) * 16;
    }
    return 1024u << (class_index - kSmallClasses);
  }

  void WriteHeader(uint32_t header, uint32_t requested, uint32_t class_index) {
    uint8_t* p = memory_->TranslateVirtual(header);
    xe::store_and_swap<uint32_t>(p, requested);
    xe::store_and_swap<uint32_t>(p + 4, class_index);
    xe::store_and_swap<uint32_t>(p + 8, kMagic);
    xe::store_and_swap<uint32_t>(p + 12, 0);
  }
  bool ReadHeader(uint32_t header, uint32_t* requested,
                  uint32_t* class_index) {
    if (!memory_) {
      return false;
    }
    const uint8_t* p = memory_->TranslateVirtual(header);
    if (xe::load_and_swap<uint32_t>(p + 8) != kMagic) {
      return false;
    }
    *requested = xe::load_and_swap<uint32_t>(p);
    *class_index = xe::load_and_swap<uint32_t>(p + 4);
    return *class_index == kLargeClass || *class_index < kClassCount;
  }

  // Free lists are threaded through the first dword of the block body.
  uint32_t PopFree(uint32_t class_index) {
    uint32_t header = free_heads_[class_index];
    if (header) {
      free_heads_[class_index] = xe::load_and_swap<uint32_t>(
          memory_->TranslateVirtual(header + kHeaderSize));
    }
    return header;
  }
  void PushFree(uint32_t class_index, uint32_t header) {
    xe::store_and_swap<uint32_t>(memory_->TranslateVirtual(header + kHeaderSize),
                                 free_heads_[class_index]);
    free_heads_[class_index] = header;
  }

  // Bump allocation from the current chunk; a new chunk when it runs out.
  uint32_t Carve(uint32_t bytes) {
    bytes = xe::round_up(bytes, 16u);
    if (chunk_cursor_ + bytes > chunk_end_) {
      uint32_t chunk = memory_->SystemHeapAlloc(kChunkSize, 4096);
      if (!chunk) {
        XELOGE("guest CRT heap: out of system heap after {} chunks", chunks_);
        return 0;
      }
      ++chunks_;
      chunk_cursor_ = chunk;
      chunk_end_ = chunk + kChunkSize;
    }
    uint32_t header = chunk_cursor_;
    chunk_cursor_ += bytes;
    return header;
  }

  Memory* memory_ = nullptr;
  std::mutex mutex_;
  uint32_t free_heads_[kClassCount] = {};
  uint32_t chunk_cursor_ = 0;
  uint32_t chunk_end_ = 0;
  uint32_t chunks_ = 0;
  uint64_t stats_small_ = 0;
  uint64_t stats_recycled_ = 0;
  uint64_t stats_freed_ = 0;
  uint64_t stats_large_ = 0;
};

GuestSmallHeap g_heap;
std::atomic<uint32_t> g_log_budget{16};

// NT heap flags the titles use.
constexpr uint32_t kHeapZeroMemory = 0x00000008;
constexpr uint32_t kHeapReallocInPlaceOnly = 0x00000010;

// PVOID RtlAllocateHeap(HANDLE heap, ULONG flags, SIZE_T size)
void HookRtlAllocateHeap(ppc::PPCContext* ctx, kernel::KernelState*) {
  uint32_t flags = uint32_t(ctx->r[4]);
  uint32_t size = uint32_t(ctx->r[5]);
  Memory* memory = ctx->processor->memory();
  uint32_t block = g_heap.Alloc(memory, size, (flags & kHeapZeroMemory) != 0);
  if (!block && g_log_budget.load() > 0) {
    g_log_budget.fetch_sub(1);
    XELOGE("guest CRT heap: RtlAllocateHeap({}) failed", size);
  }
  ctx->r[3] = block;
}

// BOOLEAN RtlFreeHeap(HANDLE heap, ULONG flags, PVOID block)
void HookRtlFreeHeap(ppc::PPCContext* ctx, kernel::KernelState*) {
  uint32_t block = uint32_t(ctx->r[5]);
  Memory* memory = ctx->processor->memory();
  bool ok = block == 0 || g_heap.Free(memory, block);
  if (!ok && g_log_budget.load() > 0) {
    g_log_budget.fetch_sub(1);
    XELOGW("guest CRT heap: RtlFreeHeap({:08X}) is not a block of this heap "
           "(lr={:08X})",
           block, uint32_t(ctx->lr));
  }
  ctx->r[3] = ok ? 1 : 0;
}

// SIZE_T RtlSizeHeap(HANDLE heap, ULONG flags, PVOID block)
void HookRtlSizeHeap(ppc::PPCContext* ctx, kernel::KernelState*) {
  uint32_t block = uint32_t(ctx->r[5]);
  ctx->r[3] = g_heap.Size(block);
}

// PVOID RtlReAllocateHeap(HANDLE heap, ULONG flags, PVOID block, SIZE_T size)
void HookRtlReAllocateHeap(ppc::PPCContext* ctx, kernel::KernelState*) {
  uint32_t flags = uint32_t(ctx->r[4]);
  uint32_t block = uint32_t(ctx->r[5]);
  uint32_t size = uint32_t(ctx->r[6]);
  Memory* memory = ctx->processor->memory();
  if (!block) {
    ctx->r[3] = g_heap.Alloc(memory, size, (flags & kHeapZeroMemory) != 0);
    return;
  }
  uint32_t old_size = g_heap.Size(block);
  if (old_size == UINT32_MAX) {
    if (g_log_budget.load() > 0) {
      g_log_budget.fetch_sub(1);
      XELOGW("guest CRT heap: RtlReAllocateHeap({:08X}) of a foreign block",
             block);
    }
    ctx->r[3] = 0;
    return;
  }
  uint32_t capacity = g_heap.Capacity(block);
  if (size <= capacity) {
    // Fits in place. Keep the class; refresh the requested size for
    // RtlSizeHeap and zero the growth when asked.
    if ((flags & kHeapZeroMemory) && size > old_size) {
      std::memset(memory->TranslateVirtual(block + old_size), 0,
                  size - old_size);
    }
    xe::store_and_swap<uint32_t>(
        memory->TranslateVirtual(block - GuestSmallHeap::kHeaderSize), size);
    ctx->r[3] = block;
    return;
  }
  if (flags & kHeapReallocInPlaceOnly) {
    ctx->r[3] = 0;
    return;
  }
  uint32_t fresh = g_heap.Alloc(memory, size, (flags & kHeapZeroMemory) != 0);
  if (!fresh) {
    ctx->r[3] = 0;
    return;
  }
  std::memcpy(memory->TranslateVirtual(fresh), memory->TranslateVirtual(block),
              std::min(old_size, size));
  g_heap.Free(memory, block);
  ctx->r[3] = fresh;
}

// void* memcpy(void* dst, const void* src, size_t n); XMemCpy is the same.
void HookMemcpy(ppc::PPCContext* ctx, kernel::KernelState*) {
  uint32_t dst = uint32_t(ctx->r[3]);
  uint32_t src = uint32_t(ctx->r[4]);
  uint32_t n = uint32_t(ctx->r[5]);
  if (n && dst && src) {
    Memory* memory = ctx->processor->memory();
    // memmove: a title that overlaps gets the safe result either way.
    std::memmove(memory->TranslateVirtual(dst), memory->TranslateVirtual(src),
                 n);
  }
  ctx->r[3] = dst;
}

// void* memset(void* dst, int c, size_t n); XMemSet is the same.
void HookMemset(ppc::PPCContext* ctx, kernel::KernelState*) {
  uint32_t dst = uint32_t(ctx->r[3]);
  int c = int(ctx->r[4]);
  uint32_t n = uint32_t(ctx->r[5]);
  if (n && dst) {
    Memory* memory = ctx->processor->memory();
    std::memset(memory->TranslateVirtual(dst), c & 0xFF, n);
  }
  ctx->r[3] = dst;
}

// Banjo-Kazooie: Nuts & Bolts (USA), code hash C03916823ADAC91B. Addresses
// from reNut config/renut_crt.toml (the recomp's [rexcrt] table), read from
// the device on 2026-09-21.
const GuestCrtHook kBanjoNutsBoltsHooks[] = {
    {0x8223F478, "RtlAllocateHeap", HookRtlAllocateHeap},
    {0x8223FD48, "RtlFreeHeap", HookRtlFreeHeap},
    {0x82240200, "RtlSizeHeap", HookRtlSizeHeap},
    {0x82717428, "RtlReAllocateHeap", HookRtlReAllocateHeap},
    {0x822A2930, "XMemCpy", HookMemcpy},
    {0x82BB4510, "memcpy", HookMemcpy},
    {0x8224B658, "XMemSet", HookMemset},
    {0x82BB4A40, "memset", HookMemset},
};

struct TitleTable {
  uint64_t code_hash;
  const GuestCrtHook* hooks;
  size_t count;
};

const TitleTable kTables[] = {
    {0xC03916823ADAC91Bull, kBanjoNutsBoltsHooks,
     sizeof(kBanjoNutsBoltsHooks) / sizeof(kBanjoNutsBoltsHooks[0])},
};

}  // namespace

bool HasGuestCrtHookTable(uint64_t code_hash) {
  for (const TitleTable& table : kTables) {
    if (table.code_hash == code_hash) {
      return true;
    }
  }
  return false;
}

const GuestCrtHook* LookupGuestCrtHook(uint64_t code_hash, uint32_t address) {
  if (!cvars::cpu_guest_crt_hooks || !code_hash) {
    return nullptr;
  }
  for (const TitleTable& table : kTables) {
    if (table.code_hash != code_hash) {
      continue;
    }
    for (size_t i = 0; i < table.count; ++i) {
      if (table.hooks[i].address == address) {
        return &table.hooks[i];
      }
    }
  }
  return nullptr;
}

}  // namespace cpu
}  // namespace xe
