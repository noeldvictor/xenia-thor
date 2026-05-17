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

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "xenia/base/mutex.h"
#include "xenia/cpu/backend/code_cache.h"

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {

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

  bool PlaceHostCode(const void* machine_code, size_t code_size,
                     void** code_address_out);
  bool PlaceGuestCode(uint32_t guest_address, const void* machine_code,
                      size_t code_size, GuestFunction* function,
                      void** code_address_out);

  bool RunSmokeTest();

  GuestFunction* LookupFunction(uint64_t host_pc) override;
  void* LookupUnwindInfo(uint64_t host_pc) override { return nullptr; }

 private:
  struct CodeRange {
    uintptr_t start = 0;
    uintptr_t end = 0;
    GuestFunction* function = nullptr;
  };

  Arm64CodeCache();

  bool SetWritable();
  bool SetExecutable();

  std::filesystem::path file_name_;
  uint8_t* generated_code_base_ = nullptr;
  size_t generated_code_size_ = 16 * 1024 * 1024;
  size_t generated_code_offset_ = 0;
  bool executable_ = false;

  xe::global_critical_region global_critical_region_;
  std::vector<CodeRange> code_ranges_;
};

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_ARM64_ARM64_CODE_CACHE_H_
