/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_XEX_MODULE_H_
#define XENIA_CPU_XEX_MODULE_H_

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "xenia/cpu/module.h"
#include "xenia/kernel/util/xex2_info.h"

namespace xe {
namespace kernel {
class KernelState;
}  // namespace kernel
}  // namespace xe

namespace xe {
namespace cpu {

constexpr fourcc_t kXEX1Signature = make_fourcc("XEX1");
constexpr fourcc_t kXEX2Signature = make_fourcc("XEX2");
constexpr fourcc_t kElfSignature = make_fourcc(0x7F, 'E', 'L', 'F');

class Runtime;

class XexModule : public xe::cpu::Module {
 public:
  struct ImportLibraryFn {
   public:
    uint32_t ordinal;
    uint32_t value_address;
    uint32_t thunk_address;
  };
  struct ImportLibrary {
   public:
    std::string name;
    uint32_t id;
    xe_xex2_version_t version;
    xe_xex2_version_t min_version;
    std::vector<ImportLibraryFn> imports;
  };
  struct SecurityInfoContext {
    const char* rsa_signature;
    const char* aes_key;
    uint32_t image_size;
    uint32_t image_flags;
    uint32_t export_table;
    uint32_t load_address;
    uint32_t page_descriptor_count;
    const xex2_page_descriptor* page_descriptors;
  };
  enum XexFormat {
    kFormatUnknown,
    kFormatXex1,
    kFormatXex2,
  };

  XexModule(Processor* processor, kernel::KernelState* kernel_state);
  virtual ~XexModule();

  bool loaded() const { return loaded_; }
  const xex2_header* xex_header() const {
    return reinterpret_cast<const xex2_header*>(xex_header_mem_.data());
  }
  const SecurityInfoContext* xex_security_info() const {
    return &security_info_;
  }

  uint32_t image_size() const {
    assert_not_zero(base_address_);

    // Calculate the new total size of the XEX image from its headers.
    auto heap = memory()->LookupHeap(base_address_);
    uint32_t total_size = 0;
    for (uint32_t i = 0; i < xex_security_info()->page_descriptor_count; i++) {
      // Byteswap the bitfield manually.
      xex2_page_descriptor desc;
      desc.value =
          xe::byte_swap(xex_security_info()->page_descriptors[i].value);

      total_size += desc.page_count * heap->page_size();
    }
    return total_size;
  }

  const std::vector<ImportLibrary>* import_libraries() const {
    return &import_libs_;
  }

  const xex2_opt_execution_info* opt_execution_info() const {
    xex2_opt_execution_info* retval = nullptr;
    GetOptHeader(XEX_HEADER_EXECUTION_INFO, &retval);
    return retval;
  }

  const xex2_opt_file_format_info* opt_file_format_info() const {
    xex2_opt_file_format_info* retval = nullptr;
    GetOptHeader(XEX_HEADER_FILE_FORMAT_INFO, &retval);
    return retval;
  }

  std::vector<uint32_t> opt_alternate_title_ids() const {
    return opt_alternate_title_ids_;
  }

  const uint32_t base_address() const { return base_address_; }
  const bool is_dev_kit() const { return is_dev_kit_; }

  // One decoded entry of the XEX PE exception directory
  // (IMAGE_CE_RUNTIME_FUNCTION_ENTRY[]), used by guest C++ exception dispatch
  // (cvar guest_cpp_exception_dispatch). On Xbox 360 the on-disk entries are
  // packed 8-byte {u32 FuncStart; u32 bits}, stored BIG-ENDIAN, FuncStart is a
  // full guest VA, and bit31 of bits = the exception-handler flag. The packed
  // length subfields are unreliable, so the function's range is bounded by the
  // next sorted entry's func_start (the function's EH FuncInfo, when present,
  // lives at the word [func_start - 4]).
  struct GuestRuntimeFunction {
    uint32_t func_start;          // guest VA of the function start
    uint32_t end_address;         // exclusive upper bound (next func_start)
    bool has_exception_handler;   // ExceptionFlag (bit31)
    uint32_t pdata_bits;          // raw 2nd pdata word (bit31=flag; low bits may
                                  // encode the xdata/FuncInfo RVA -- under study)
  };
  // Map a guest PC to its runtime-function entry, or nullptr. Requires the
  // exception directory to have been parsed (guest_cpp_exception_dispatch on).
  const GuestRuntimeFunction* FindRuntimeFunction(uint32_t guest_pc) const;
  // Testable core: search a sorted-by-func_start table for the entry whose
  // [func_start, end_address) contains pc.
  static const GuestRuntimeFunction* FindRuntimeFunction(
      const std::vector<GuestRuntimeFunction>& table, uint32_t guest_pc);

  // Multicore JIT: precompile this module's guest functions (from the parsed
  // runtime-function table) on background threads, ahead of execution, to use
  // the otherwise-idle cores and remove first-encounter compile stutter.
  // No-op unless cpu_precompile_guest_functions is set. Safe to race executors
  // (the compile path is thread-safe). StopPrecompile joins the workers and is
  // called before Unload deallocates the guest code, and from the destructor.
  void PrecompileGuestFunctions();
  void StopPrecompile();

  // Gets an optional header. Returns NULL if not found.
  // Special case: if key & 0xFF == 0x00, this function will return the value,
  // not a pointer! This assumes out_ptr points to uint32_t.
  static bool GetOptHeader(const xex2_header* header, xex2_header_keys key,
                           void** out_ptr);
  bool GetOptHeader(xex2_header_keys key, void** out_ptr) const;

  // Ultra-cool templated version
  // Special case: if key & 0xFF == 0x00, this function will return the value,
  // not a pointer! This assumes out_ptr points to uint32_t.
  template <typename T>
  static bool GetOptHeader(const xex2_header* header, xex2_header_keys key,
                           T* out_ptr) {
    return GetOptHeader(header, key, reinterpret_cast<void**>(out_ptr));
  }

  template <typename T>
  bool GetOptHeader(xex2_header_keys key, T* out_ptr) const {
    return GetOptHeader(key, reinterpret_cast<void**>(out_ptr));
  }

  static const void* GetSecurityInfo(const xex2_header* header);

  const PESection* GetPESection(const char* name);

  uint32_t GetProcAddress(uint16_t ordinal) const;
  uint32_t GetProcAddress(const std::string_view name) const;

  int ApplyPatch(XexModule* module);
  bool Load(const std::string_view name, const std::string_view path,
            const void* xex_addr, size_t xex_length);
  bool LoadContinue();
  bool Unload();

  bool ContainsAddress(uint32_t address) override;

  const std::string& name() const override { return name_; }
  bool is_executable() const override {
    return (xex_header()->module_flags & XEX_MODULE_TITLE) != 0;
  }

  bool is_valid_executable() const {
    assert_not_zero(base_address_);
    if (!base_address_) {
      return false;
    }
    uint8_t* buffer = memory()->TranslateVirtual(base_address_);
    return *(uint32_t*)buffer == 0x905A4D;
  }

  bool is_patch() const {
    assert_not_null(xex_header());
    if (!xex_header()) {
      return false;
    }
    return (xex_header()->module_flags &
            (XEX_MODULE_MODULE_PATCH | XEX_MODULE_PATCH_DELTA |
             XEX_MODULE_PATCH_FULL));
  }

 protected:
  std::unique_ptr<Function> CreateFunction(uint32_t address) override;

 private:
  void ReadSecurityInfo();

  int ReadImage(const void* xex_addr, size_t xex_length, bool use_dev_key);
  int ReadImageUncompressed(const void* xex_addr, size_t xex_length);
  int ReadImageBasicCompressed(const void* xex_addr, size_t xex_length);
  int ReadImageCompressed(const void* xex_addr, size_t xex_length);

  int ReadPEHeaders();

  bool SetupLibraryImports(const std::string_view name,
                           const xex2_import_library* library);
  bool FindSaveRest();

  Processor* processor_ = nullptr;
  kernel::KernelState* kernel_state_ = nullptr;
  std::string name_;
  std::string path_;
  std::vector<uint8_t> xex_header_mem_;  // Holds the xex header
  std::vector<uint8_t> xexp_data_mem_;   // Holds XEXP patch data

  std::vector<ImportLibrary>
      import_libs_;  // pre-loaded import libraries for ease of use
  std::vector<PESection> pe_sections_;
  // Parsed XEX PE exception directory, sorted by func_start (empty unless
  // guest_cpp_exception_dispatch was on at load). See GuestRuntimeFunction.
  std::vector<GuestRuntimeFunction> guest_runtime_functions_;

  // Multicore JIT precompiler state (cpu_precompile_guest_functions).
  // RefillPrecompileWork() scans the module's declared-but-undefined function
  // frontier (functions xenia declared as direct-call targets while compiling
  // their callers, but hasn't codegen'd yet) and the parsed pdata entry points,
  // appending fresh addresses to precompile_work_. Workers pull the next index
  // (precompile_cursor_) and ResolveFunction() it off-lock, codegen'ing it
  // ahead of the guest reaching it. precompile_stop_ + join in StopPrecompile()
  // drains them before Unload deallocates guest code. All non-atomic members
  // are guarded by precompile_mutex_; the work scan follows the call graph so
  // it is independent of whether the XEX has a pdata exception directory.
  std::vector<std::thread> precompile_threads_;
  std::atomic<bool> precompile_stop_{false};
  std::mutex precompile_mutex_;       // guards work_/queued_/cursor_
  std::mutex precompile_scan_mutex_;  // serializes the frontier scan (try_lock)
  std::vector<uint32_t> precompile_work_;
  std::unordered_set<uint32_t> precompile_queued_;
  size_t precompile_cursor_ = 0;
  void RefillPrecompileWork();

  // XEX_HEADER_ALTERNATE_TITLE_IDS loaded into a safe std::vector
  std::vector<uint32_t> opt_alternate_title_ids_;

  uint8_t session_key_[0x10];
  bool is_dev_kit_ = false;

  bool loaded_ = false;         // Loaded into memory?
  bool finished_load_ = false;  // PE/imports/symbols/etc all loaded?

  uint32_t base_address_ = 0;
  uint32_t low_address_ = 0;
  uint32_t high_address_ = 0;

  XexFormat xex_format_ = kFormatUnknown;
  SecurityInfoContext security_info_ = {};
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_XEX_MODULE_H_
