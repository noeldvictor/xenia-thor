/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_ARM64_ARM64_CODE_CACHE_H_
#define XENIA_CPU_BACKEND_ARM64_ARM64_CODE_CACHE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/cpu/backend/code_cache.h"

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {

struct Arm64EmitFunctionInfo {
  struct CodeSize {
    size_t prolog = 0;
    size_t body = 0;
    size_t epilog = 0;
    size_t tail = 0;
    size_t total = 0;
  } code_size;
  size_t prolog_stack_alloc_offset = 0;
  size_t stack_size = 0;
  size_t lr_save_offset = 0;
};

class Arm64CodeCache : public CodeCache {
 public:
  ~Arm64CodeCache() override;

  static std::unique_ptr<Arm64CodeCache> Create();

  bool Initialize();

  const std::filesystem::path& file_name() const override {
    return file_name_;
  }
  uintptr_t execute_base_address() const override {
    return reinterpret_cast<uintptr_t>(generated_code_base_);
  }
  size_t total_size() const override { return generated_code_size_; }

  bool has_indirection_table() const { return indirection_table_base_ != nullptr; }
  bool encoded_indirection() const { return true; }
  uintptr_t indirection_table_base_address() const {
    return reinterpret_cast<uintptr_t>(indirection_table_base_);
  }
  uintptr_t indirection_table_base_bias() const {
    return indirection_table_base_bias_;
  }
  uintptr_t external_indirection_table_base_address() const {
    return reinterpret_cast<uintptr_t>(external_indirection_targets_.get());
  }

  static constexpr uint32_t kIndirectionExternalTag = 0x80000000u;
  static constexpr uint32_t kIndirectionExternalIndexMask = 0x7FFFFFFFu;
  static constexpr uint32_t kIndirectionExternalCapacity = 0x00010000u;

  void set_indirection_default_64(uint64_t default_value);
  void AddIndirection(uint32_t guest_address, uint32_t host_address);
  void AddIndirection64(uint32_t guest_address, uint64_t host_address);
  uint32_t LookupIndirection(uint32_t guest_address) const;

  void CommitExecutableRange(uint32_t guest_low, uint32_t guest_high);

  bool PlaceHostCode(const void* machine_code, size_t code_size,
                     void** code_address_out);
  bool PlaceGuestCode(uint32_t guest_address, const void* machine_code,
                      size_t code_size, GuestFunction* function,
                      void** code_address_out);
  void PlaceHostCode(uint32_t guest_address, void* machine_code,
                     const Arm64EmitFunctionInfo& func_info,
                     void*& code_execute_address_out,
                     void*& code_write_address_out);
  void PlaceGuestCode(uint32_t guest_address, void* machine_code,
                      const Arm64EmitFunctionInfo& func_info,
                      GuestFunction* function,
                      void*& code_execute_address_out,
                      void*& code_write_address_out);
  uint32_t PlaceData(const void* data, size_t length);

  bool RunSmokeTest();

  GuestFunction* LookupFunction(uint64_t host_pc) override;
  void* LookupUnwindInfo(uint64_t host_pc) override { return nullptr; }
  size_t generated_code_commit_mark() const {
    return generated_code_commit_mark_.load(std::memory_order_relaxed);
  }

 private:
  enum class CodeCacheMode {
    kWxFlip,
    kRwxDebug,
  };

  struct CodeRange {
    uintptr_t start = 0;
    uintptr_t end = 0;
    GuestFunction* function = nullptr;
  };

  struct UnwindReservation {
    size_t data_size = 0;
    size_t table_slot = 0;
    uint8_t* entry_address = nullptr;
  };

  static constexpr size_t kIndirectionTableSize = 0x1FFFFFFF;
  static constexpr uintptr_t kIndirectionTableBase = 0x80000000;

  Arm64CodeCache();

  bool SetWritable();
  bool SetExecutable();
  bool CommitIndirectionTableRange(size_t start_offset, size_t size);
  bool IsIndirectionTableRangeCommitted(size_t start_offset,
                                        size_t size) const;
  uint32_t EncodeIndirectionTarget(uint64_t host_address);
  void EnsureGeneratedCodeCommitMark(size_t high_mark);

  std::filesystem::path file_name_;
  uint8_t* indirection_table_base_ = nullptr;
  uintptr_t indirection_table_base_bias_ = 0;
  std::unique_ptr<uint64_t[]> external_indirection_targets_;
  std::atomic<uint32_t> external_indirection_target_count_{0};
  mutable std::mutex external_indirection_mutex_;
  uint32_t indirection_default_value_ = 0xFEEDF00D;
  std::vector<std::pair<size_t, size_t>> committed_indirection_ranges_;

  uint8_t* generated_code_base_ = nullptr;
  size_t generated_code_size_ = 16 * 1024 * 1024;
  size_t generated_code_offset_ = 0;
  std::atomic<size_t> generated_code_commit_mark_{0};
  CodeCacheMode code_cache_mode_ = CodeCacheMode::kWxFlip;
  bool executable_ = false;

  xe::global_critical_region global_critical_region_;
  std::vector<CodeRange> code_ranges_;
};

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_ARM64_ARM64_CODE_CACHE_H_
