/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/arm64/arm64_code_cache.h"

#include <algorithm>
#include <cstring>
#include <limits>

#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
#include <sys/mman.h>
#include <unistd.h>
#endif

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#endif

#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/platform.h"
#include "xenia/cpu/function.h"

#if XE_PLATFORM_ANDROID
#define XENIA_ARM64_DEFAULT_CODE_CACHE_MODE "rwx_debug"
#else
#define XENIA_ARM64_DEFAULT_CODE_CACHE_MODE "wx_flip"
#endif

DEFINE_string(
    arm64_jit_code_cache_mode, XENIA_ARM64_DEFAULT_CODE_CACHE_MODE,
    "ARM64 JIT code cache permission mode. Values: wx_flip, rwx_debug. "
    "rwx_debug is a research-only Android bring-up mode that avoids toggling "
    "execute permission while other guest threads may be running generated "
    "code.",
    "CPU");
DEFINE_uint32(arm64_jit_code_cache_mb, 128,
              "ARM64 JIT generated code cache size in MiB. This is oversized "
              "for research bring-up so helper-heavy mini-JIT output can keep "
              "running long captures without falling back due to cache "
              "exhaustion.",
              "CPU");
DECLARE_uint32(mmap_address_high);

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {
namespace {

constexpr uint32_t kArm64Brk0 = 0xD4200000u;
constexpr uintptr_t kAx360eCodeCacheBaseLow = 0xA0000000u;

uintptr_t GetAx360eCodeCacheHighBias() {
  if (!cvars::mmap_address_high || cvars::mmap_address_high > 124) {
    return 0;
  }
  return uint64_t(cvars::mmap_address_high + 2) << 32;
}

void* AllocateCodeMemory(size_t size, void* desired_base) {
#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_FIXED_NOREPLACE
  if (desired_base) {
    void* fixed_result =
        mmap(desired_base, size, PROT_READ | PROT_WRITE,
             flags | MAP_FIXED_NOREPLACE, -1, 0);
    if (fixed_result == desired_base) {
      return fixed_result;
    }
    if (fixed_result != MAP_FAILED) {
      munmap(fixed_result, size);
    }
  }
#endif
  void* result =
      mmap(desired_base, size, PROT_READ | PROT_WRITE, flags, -1, 0);
  if (desired_base && result != desired_base) {
    if (result != MAP_FAILED) {
      munmap(result, size);
    }
    return nullptr;
  }
  return result == MAP_FAILED ? nullptr : result;
#elif XE_PLATFORM_WIN32
  return xe::memory::AllocFixed(desired_base, size,
                                xe::memory::AllocationType::kReserveCommit,
                                xe::memory::PageAccess::kReadWrite);
#else
  return nullptr;
#endif
}

void* ReserveIndirectionTableMemory(size_t size, void* desired_base) {
#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_NORESERVE
  flags |= MAP_NORESERVE;
#endif
#ifdef MAP_FIXED_NOREPLACE
  if (desired_base) {
    void* fixed_result =
        mmap(desired_base, size, PROT_NONE, flags | MAP_FIXED_NOREPLACE, -1,
             0);
    if (fixed_result == desired_base) {
      return fixed_result;
    }
    if (fixed_result != MAP_FAILED) {
      munmap(fixed_result, size);
    }
  }
#endif
  void* result = mmap(desired_base, size, PROT_NONE, flags, -1, 0);
  if (desired_base && result != desired_base) {
    if (result != MAP_FAILED) {
      munmap(result, size);
    }
    return nullptr;
  }
  return result == MAP_FAILED ? nullptr : result;
#elif XE_PLATFORM_WIN32
  return xe::memory::AllocFixed(desired_base, size,
                                xe::memory::AllocationType::kReserve,
                                xe::memory::PageAccess::kNoAccess);
#else
  return nullptr;
#endif
}

void FreeCodeMemory(void* address, size_t size) {
  if (!address) {
    return;
  }
#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
  munmap(address, size);
#elif XE_PLATFORM_WIN32
  xe::memory::DeallocFixed(address, 0,
                           xe::memory::DeallocationType::kRelease);
#endif
}

void FreeIndirectionTableMemory(void* address, size_t size) {
  if (!address) {
    return;
  }
#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
  munmap(address, size);
#elif XE_PLATFORM_WIN32
  xe::memory::DeallocFixed(address, 0,
                           xe::memory::DeallocationType::kRelease);
#endif
}

void FlushHostInstructionCache(void* address, size_t size) {
#if XE_PLATFORM_WIN32
  ::FlushInstructionCache(::GetCurrentProcess(), address, size);
#else
  auto start = reinterpret_cast<char*>(address);
  __builtin___clear_cache(start, start + size);
#endif
}

void FillUnwrittenCodeWithBreakpoints(void* address, size_t size) {
  // Xenia Edge's A64 cache fills unwritten code with BRK #0. Keep the same
  // guard behavior here so bad jumps trap instead of executing zeroed memory.
  auto* instruction = reinterpret_cast<uint32_t*>(address);
  auto instruction_count = size / sizeof(uint32_t);
  std::fill_n(instruction, instruction_count, kArm64Brk0);
}

}  // namespace

Arm64CodeCache::Arm64CodeCache() = default;

Arm64CodeCache::~Arm64CodeCache() {
  FreeIndirectionTableMemory(indirection_table_base_, kIndirectionTableSize);
  FreeCodeMemory(generated_code_base_, generated_code_size_);
}

std::unique_ptr<Arm64CodeCache> Arm64CodeCache::Create() {
  return std::unique_ptr<Arm64CodeCache>(new Arm64CodeCache());
}

bool Arm64CodeCache::Initialize() {
  file_name_ = "xenia_arm64_code_cache_" +
               std::to_string(Clock::QueryHostTickCount());
  generated_code_size_ =
      size_t(std::max<uint32_t>(1, cvars::arm64_jit_code_cache_mb)) * 1024 *
      1024;

  if (cvars::arm64_jit_code_cache_mode == "rwx_debug") {
    code_cache_mode_ = CodeCacheMode::kRwxDebug;
  } else if (cvars::arm64_jit_code_cache_mode == "wx_flip") {
    code_cache_mode_ = CodeCacheMode::kWxFlip;
  } else {
    XELOGW(
        "Unknown ARM64 JIT code cache mode '{}', falling back to wx_flip",
        cvars::arm64_jit_code_cache_mode);
    code_cache_mode_ = CodeCacheMode::kWxFlip;
  }

  uintptr_t ax360e_high_bias = GetAx360eCodeCacheHighBias();
  void* fixed_indirection_base =
      ax360e_high_bias ? reinterpret_cast<void*>(
                             ax360e_high_bias | kIndirectionTableBase)
                       : nullptr;
  indirection_table_base_ =
      reinterpret_cast<uint8_t*>(ReserveIndirectionTableMemory(
          kIndirectionTableSize, fixed_indirection_base));
  if (!indirection_table_base_ && fixed_indirection_base) {
    XELOGW(
        "ARM64 code cache failed to reserve aX360e-style indirection table "
        "at {:016X}; falling back to dynamic reservation",
        static_cast<uint64_t>(
            reinterpret_cast<uintptr_t>(fixed_indirection_base)));
    indirection_table_base_ =
        reinterpret_cast<uint8_t*>(ReserveIndirectionTableMemory(
            kIndirectionTableSize, nullptr));
  }
  if (!indirection_table_base_) {
    XELOGE("ARM64 code cache failed to reserve indirection table");
    return false;
  }
  indirection_table_base_bias_ =
      reinterpret_cast<uintptr_t>(indirection_table_base_) -
      static_cast<uintptr_t>(kIndirectionTableBase);
  external_indirection_targets_ =
      std::make_unique<uint64_t[]>(kIndirectionExternalCapacity);
  external_indirection_target_count_.store(0, std::memory_order_relaxed);

  void* fixed_code_base =
      ax360e_high_bias
          ? reinterpret_cast<void*>(ax360e_high_bias | kAx360eCodeCacheBaseLow)
          : nullptr;
  generated_code_base_ = reinterpret_cast<uint8_t*>(
      AllocateCodeMemory(generated_code_size_, fixed_code_base));
  if (!generated_code_base_ && fixed_code_base) {
    XELOGW(
        "ARM64 code cache failed to allocate aX360e-style code base at "
        "{:016X}; falling back to dynamic allocation",
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(fixed_code_base)));
    generated_code_base_ =
        reinterpret_cast<uint8_t*>(AllocateCodeMemory(generated_code_size_,
                                                      nullptr));
  }
  if (!generated_code_base_) {
    XELOGE("ARM64 code cache failed to allocate {} bytes",
           generated_code_size_);
    return false;
  }
  FillUnwrittenCodeWithBreakpoints(generated_code_base_, generated_code_size_);

  if (code_cache_mode_ == CodeCacheMode::kRwxDebug) {
    if (!xe::memory::Protect(generated_code_base_, generated_code_size_,
                             xe::memory::PageAccess::kExecuteReadWrite)) {
      XELOGE("ARM64 code cache failed to switch to RWX debug pages");
      return false;
    }
    executable_ = true;
  }

  code_ranges_.reserve(100000);
  XELOGI(
      "ARM64 code cache allocated {} bytes at {:016X} mode={} "
      "indirection={:016X}",
         generated_code_size_,
         static_cast<uint64_t>(
             reinterpret_cast<uintptr_t>(generated_code_base_)),
         code_cache_mode_ == CodeCacheMode::kRwxDebug ? "rwx_debug"
                                                      : "wx_flip",
         static_cast<uint64_t>(
             reinterpret_cast<uintptr_t>(indirection_table_base_)));
  return true;
}

void Arm64CodeCache::set_indirection_default_64(uint64_t default_value) {
  indirection_default_value_ = EncodeIndirectionTarget(default_value);
}

void Arm64CodeCache::AddIndirection(uint32_t guest_address,
                                    uint32_t host_address) {
  AddIndirection64(guest_address, static_cast<uint64_t>(host_address));
}

void Arm64CodeCache::AddIndirection64(uint32_t guest_address,
                                      uint64_t host_address) {
  if (!indirection_table_base_ || guest_address < kIndirectionTableBase) {
    return;
  }

  uint64_t guest_delta = guest_address - kIndirectionTableBase;
  size_t slot_offset = static_cast<size_t>((guest_delta / 4) * 4);
  if (slot_offset + sizeof(uint32_t) > kIndirectionTableSize) {
    return;
  }

  if (!CommitIndirectionTableRange(slot_offset, sizeof(uint32_t))) {
    return;
  }

  auto* slot =
      reinterpret_cast<uint32_t*>(indirection_table_base_ + slot_offset);
  *slot = EncodeIndirectionTarget(host_address);
}

uint32_t Arm64CodeCache::LookupIndirection(uint32_t guest_address) const {
  if (!indirection_table_base_ || guest_address < kIndirectionTableBase) {
    return indirection_default_value_;
  }
  uint64_t guest_delta = guest_address - kIndirectionTableBase;
  size_t slot_offset = static_cast<size_t>((guest_delta / 4) * 4);
  if (slot_offset + sizeof(uint32_t) > kIndirectionTableSize) {
    return indirection_default_value_;
  }
  if (!IsIndirectionTableRangeCommitted(slot_offset, sizeof(uint32_t))) {
    return indirection_default_value_;
  }
  return *reinterpret_cast<const uint32_t*>(indirection_table_base_ +
                                            slot_offset);
}

void Arm64CodeCache::CommitExecutableRange(uint32_t guest_low,
                                           uint32_t guest_high) {
  if (!indirection_table_base_ || guest_high <= kIndirectionTableBase ||
      guest_high <= guest_low) {
    return;
  }

  guest_low = std::max<uint32_t>(guest_low, kIndirectionTableBase);
  size_t start_offset = static_cast<size_t>(guest_low - kIndirectionTableBase);
  size_t end_offset = static_cast<size_t>(guest_high - kIndirectionTableBase);
  start_offset = (start_offset / sizeof(uint32_t)) * sizeof(uint32_t);
  end_offset = xe::round_up(end_offset, sizeof(uint32_t));
  if (end_offset > kIndirectionTableSize) {
    XELOGE("ARM64 CommitExecutableRange {:08X}-{:08X} exceeds table",
           guest_low, guest_high);
    end_offset = kIndirectionTableSize;
  }
  if (end_offset <= start_offset) {
    return;
  }

  size_t size = end_offset - start_offset;
  if (!CommitIndirectionTableRange(start_offset, size)) {
    return;
  }

  auto* slot =
      reinterpret_cast<uint32_t*>(indirection_table_base_ + start_offset);
  std::fill_n(slot, size / sizeof(uint32_t), indirection_default_value_);
}

bool Arm64CodeCache::SetWritable() {
  if (code_cache_mode_ == CodeCacheMode::kRwxDebug) {
    return true;
  }

  if (!generated_code_base_ || !executable_) {
    executable_ = false;
    return true;
  }

  if (!xe::memory::Protect(generated_code_base_, generated_code_size_,
                           xe::memory::PageAccess::kReadWrite)) {
    XELOGE("ARM64 code cache failed to switch to writable pages");
    return false;
  }
  executable_ = false;
  return true;
}

bool Arm64CodeCache::SetExecutable() {
  if (code_cache_mode_ == CodeCacheMode::kRwxDebug) {
    executable_ = true;
    return true;
  }

  if (!generated_code_base_ || executable_) {
    executable_ = true;
    return true;
  }

  if (!xe::memory::Protect(generated_code_base_, generated_code_size_,
                           xe::memory::PageAccess::kExecuteReadOnly)) {
    XELOGE("ARM64 code cache failed to switch to executable pages");
    return false;
  }
  executable_ = true;
  return true;
}

bool Arm64CodeCache::PlaceHostCode(const void* machine_code, size_t code_size,
                                   void** code_address_out) {
  return PlaceGuestCode(0, machine_code, code_size, nullptr,
                        code_address_out);
}

bool Arm64CodeCache::PlaceGuestCode(uint32_t guest_address,
                                    const void* machine_code, size_t code_size,
                                    GuestFunction* function,
                                    void** code_address_out) {
  if (!machine_code || !code_size || !code_address_out) {
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();

  if (!SetWritable()) {
    return false;
  }

  generated_code_offset_ = xe::round_up(generated_code_offset_, size_t(16));
  if (generated_code_offset_ + code_size > generated_code_size_) {
    XELOGE("ARM64 code cache exhausted while placing guest {:08X}",
           guest_address);
    return false;
  }

  uint8_t* code_address = generated_code_base_ + generated_code_offset_;
  std::memcpy(code_address, machine_code, code_size);
  FlushHostInstructionCache(code_address, code_size);

  auto start = reinterpret_cast<uintptr_t>(code_address);
  code_ranges_.push_back({start, start + code_size, function});

  generated_code_offset_ += xe::round_up(code_size, size_t(16));
  EnsureGeneratedCodeCommitMark(generated_code_offset_);
  *code_address_out = code_address;

  bool executable = SetExecutable();
  if (executable && guest_address) {
    AddIndirection64(guest_address, reinterpret_cast<uint64_t>(code_address));
  }
  return executable;
}

void Arm64CodeCache::PlaceHostCode(
    uint32_t guest_address, void* machine_code,
    const Arm64EmitFunctionInfo& func_info, void*& code_execute_address_out,
    void*& code_write_address_out) {
  PlaceGuestCode(guest_address, machine_code, func_info, nullptr,
                 code_execute_address_out, code_write_address_out);
}

void Arm64CodeCache::PlaceGuestCode(
    uint32_t guest_address, void* machine_code,
    const Arm64EmitFunctionInfo& func_info, GuestFunction* function,
    void*& code_execute_address_out, void*& code_write_address_out) {
  void* code_address = nullptr;
  if (!PlaceGuestCode(guest_address, machine_code, func_info.code_size.total,
                      function, &code_address)) {
    code_execute_address_out = nullptr;
    code_write_address_out = nullptr;
    return;
  }
  code_execute_address_out = code_address;
  code_write_address_out = code_address;
}

uint32_t Arm64CodeCache::PlaceData(const void* data, size_t length) {
  if (!data || !length) {
    return 0;
  }

  auto global_lock = global_critical_region_.Acquire();

  if (!SetWritable()) {
    return 0;
  }

  generated_code_offset_ = xe::round_up(generated_code_offset_, size_t(16));
  if (generated_code_offset_ + length > generated_code_size_) {
    XELOGE("ARM64 code cache exhausted while placing data");
    return 0;
  }

  uint8_t* data_address = generated_code_base_ + generated_code_offset_;
  std::memcpy(data_address, data, length);
  generated_code_offset_ += xe::round_up(length, size_t(16));
  EnsureGeneratedCodeCommitMark(generated_code_offset_);
  if (!SetExecutable()) {
    return 0;
  }

  auto address = reinterpret_cast<uintptr_t>(data_address);
  if (address > std::numeric_limits<uint32_t>::max()) {
    XELOGW("ARM64 PlaceData address {:016X} does not fit in 32 bits",
           static_cast<uint64_t>(address));
    return 0;
  }
  return static_cast<uint32_t>(address);
}

bool Arm64CodeCache::RunSmokeTest() {
#if XE_ARCH_ARM64
  // mov w0, #42 ; ret
  const uint32_t code[] = {0x52800540u, 0xD65F03C0u};
  void* code_address = nullptr;
  if (!PlaceHostCode(code, sizeof(code), &code_address)) {
    return false;
  }

  using SmokeFn = uint32_t (*)();
  uint32_t result = reinterpret_cast<SmokeFn>(code_address)();
  if (result != 42) {
    XELOGE("ARM64 code cache smoke test returned {}, expected 42", result);
    return false;
  }

  XELOGI("ARM64 code cache smoke test passed at {:016X}",
         static_cast<uint64_t>(reinterpret_cast<uintptr_t>(code_address)));
#else
  XELOGI("ARM64 code cache smoke test skipped on non-AArch64 host");
#endif
  return true;
}

GuestFunction* Arm64CodeCache::LookupFunction(uint64_t host_pc) {
  auto pc = static_cast<uintptr_t>(host_pc);
  auto global_lock = global_critical_region_.Acquire();
  for (const auto& range : code_ranges_) {
    if (pc >= range.start && pc < range.end) {
      return range.function;
    }
  }
  return nullptr;
}

bool Arm64CodeCache::CommitIndirectionTableRange(size_t start_offset,
                                                 size_t size) {
  if (!indirection_table_base_ || start_offset >= kIndirectionTableSize ||
      !size) {
    return false;
  }

  size_t table_end = std::min(kIndirectionTableSize, start_offset + size);
  size_t page_size = xe::memory::page_size();
  size_t page_start = (start_offset / page_size) * page_size;
  size_t page_end = xe::round_up(table_end, page_size);
  size_t page_size_to_commit = page_end - page_start;
  void* page_address = indirection_table_base_ + page_start;

  bool committed = false;
#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
  committed =
      mprotect(page_address, page_size_to_commit, PROT_READ | PROT_WRITE) == 0;
  if (!committed) {
    XELOGE("ARM64 indirection table mprotect failed at {:016X}",
           static_cast<uint64_t>(reinterpret_cast<uintptr_t>(page_address)));
  }
#elif XE_PLATFORM_WIN32
  committed = xe::memory::AllocFixed(page_address, page_size_to_commit,
                                     xe::memory::AllocationType::kCommit,
                                     xe::memory::PageAccess::kReadWrite) !=
              nullptr;
#else
  committed = false;
#endif
  if (committed) {
    committed_indirection_ranges_.emplace_back(page_start, page_end);
  }
  return committed;
}

bool Arm64CodeCache::IsIndirectionTableRangeCommitted(size_t start_offset,
                                                      size_t size) const {
  size_t end_offset = start_offset + size;
  for (const auto& range : committed_indirection_ranges_) {
    if (start_offset >= range.first && end_offset <= range.second) {
      return true;
    }
  }
  return false;
}

uint32_t Arm64CodeCache::EncodeIndirectionTarget(uint64_t host_address) {
  auto code_base = execute_base_address();
  auto code_end = code_base + generated_code_size_;
  if (host_address >= code_base && host_address < code_end) {
    return static_cast<uint32_t>(host_address - code_base);
  }

  std::lock_guard<std::mutex> lock(external_indirection_mutex_);
  uint32_t current_count =
      external_indirection_target_count_.load(std::memory_order_relaxed);
  for (uint32_t i = 0; i < current_count; ++i) {
    if (external_indirection_targets_[i] == host_address) {
      return kIndirectionExternalTag | i;
    }
  }
  if (current_count >= kIndirectionExternalCapacity) {
    XELOGE("ARM64 external indirection table overflow");
    return indirection_default_value_;
  }
  external_indirection_targets_[current_count] = host_address;
  external_indirection_target_count_.store(current_count + 1,
                                           std::memory_order_release);
  return kIndirectionExternalTag | current_count;
}

void Arm64CodeCache::EnsureGeneratedCodeCommitMark(size_t high_mark) {
  size_t old_mark = generated_code_commit_mark_.load(std::memory_order_relaxed);
  while (high_mark > old_mark &&
         !generated_code_commit_mark_.compare_exchange_weak(
             old_mark, high_mark, std::memory_order_release,
             std::memory_order_relaxed)) {
  }
}

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
