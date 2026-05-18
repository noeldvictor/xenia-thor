/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/arm64/arm64_code_cache.h"

#include <cstring>

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

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {
namespace {

void* AllocateCodeMemory(size_t size) {
#if XE_PLATFORM_LINUX || XE_PLATFORM_MAC
  void* result = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return result == MAP_FAILED ? nullptr : result;
#elif XE_PLATFORM_WIN32
  return xe::memory::AllocFixed(nullptr, size,
                                xe::memory::AllocationType::kReserveCommit,
                                xe::memory::PageAccess::kReadWrite);
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

void FlushHostInstructionCache(void* address, size_t size) {
#if XE_PLATFORM_WIN32
  ::FlushInstructionCache(::GetCurrentProcess(), address, size);
#else
  auto start = reinterpret_cast<char*>(address);
  __builtin___clear_cache(start, start + size);
#endif
}

}  // namespace

Arm64CodeCache::Arm64CodeCache() = default;

Arm64CodeCache::~Arm64CodeCache() {
  FreeCodeMemory(generated_code_base_, generated_code_size_);
}

std::unique_ptr<Arm64CodeCache> Arm64CodeCache::Create() {
  return std::unique_ptr<Arm64CodeCache>(new Arm64CodeCache());
}

bool Arm64CodeCache::Initialize() {
  file_name_ = "xenia_arm64_code_cache_" +
               std::to_string(Clock::QueryHostTickCount());

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

  generated_code_base_ =
      reinterpret_cast<uint8_t*>(AllocateCodeMemory(generated_code_size_));
  if (!generated_code_base_) {
    XELOGE("ARM64 code cache failed to allocate {} bytes",
           generated_code_size_);
    return false;
  }

  if (code_cache_mode_ == CodeCacheMode::kRwxDebug) {
    if (!xe::memory::Protect(generated_code_base_, generated_code_size_,
                             xe::memory::PageAccess::kExecuteReadWrite)) {
      XELOGE("ARM64 code cache failed to switch to RWX debug pages");
      return false;
    }
    executable_ = true;
  }

  code_ranges_.reserve(100000);
  XELOGI("ARM64 code cache allocated {} bytes at {:016X} mode={}",
         generated_code_size_,
         static_cast<uint64_t>(
             reinterpret_cast<uintptr_t>(generated_code_base_)),
         code_cache_mode_ == CodeCacheMode::kRwxDebug ? "rwx_debug"
                                                      : "wx_flip");
  return true;
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
  *code_address_out = code_address;

  return SetExecutable();
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

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
