/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/llvm/llvm_object_cache.h"

#if XE_LLVM_BACKEND_ENABLED

#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>
#include <utility>
#include <vector>

#include "xenia/base/logging.h"

#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/ExecutionEngine/Orc/MapperJITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/MemoryMapper.h"
#include "llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/Orc/Shared/AllocationActions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"

#if !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>
#endif

// Subclassing llvm::ObjectCache and instantiating llvm::orc::SimpleCompiler from
// -frtti code (this lib is -frtti for cvar.h's dynamic_cast) makes the compiler
// emit our subclass's typeinfo, which references the bases' typeinfo by pointer -
// but the -fno-rtti libLLVM never defines those, so the link fails with
// "undefined reference to typeinfo for llvm::ObjectCache / SimpleCompiler". A
// per-file -fno-rtti is not portable (the androidndk premake generator only sets
// RTTI per-module), so we instead provide WEAK PLACEHOLDER typeinfo symbols here.
// They are referenced only as POINTER VALUES inside our subclass's typeinfo and
// are never dereferenced (nothing dynamic_casts/typeids an ObjectCache or
// SimpleCompiler - LLVM dispatches both purely through vtables), so a placeholder
// is safe. Itanium ABI mangling is identical on Clang (android) and GCC (linux).
// (This TU deliberately does NOT include cvar.h - all config arrives as args.)
#if defined(__clang__)
// Clang (the Android/device target, -fno-rtti libLLVM): weak null data symbols.
// Device-proven (the APK links + runs).
extern "C" {
[[gnu::weak]] const void* _ZTIN4llvm11ObjectCacheE = nullptr;
[[gnu::weak]] const void* _ZTIN4llvm3orc14SimpleCompilerE = nullptr;
// llvm_assembler.cc's llvm::Error usage instantiates ErrorInfo<..,ErrorInfoBase>
// whose typeinfo references the base's; libLLVM (-fno-rtti) omits it. Never
// dereferenced (llvm::Error dispatches via its own classID, not C++ typeid).
[[gnu::weak]] const void* _ZTIN4llvm13ErrorInfoBaseE = nullptr;
// XeSlabMemoryMapper below subclasses llvm::orc::MemoryMapper.
[[gnu::weak]] const void* _ZTIN4llvm3orc12MemoryMapperE = nullptr;
}
#else
// GCC (the qemu/linux cpu-tests build) emits a std::type_info reference for these
// same mangled names and rejects a `const void*` DEFINITION of them ("conflicting
// declaration"); a bare `.weak` DECLARATION doesn't resolve under that build's LTO
// ("undefined reference to typeinfo"). So DEFINE them via asm as weak 8-byte data
// (no C++ type = no conflict; a real definition = survives LTO). Value 0, never
// dereferenced; a real libLLVM typeinfo, if present, is strong and overrides.
asm(".pushsection .data.rel.ro,\"aw\"\n"
    ".weak _ZTIN4llvm11ObjectCacheE\n"
    "_ZTIN4llvm11ObjectCacheE: .quad 0\n"
    ".weak _ZTIN4llvm3orc14SimpleCompilerE\n"
    "_ZTIN4llvm3orc14SimpleCompilerE: .quad 0\n"
    ".weak _ZTIN4llvm13ErrorInfoBaseE\n"
    "_ZTIN4llvm13ErrorInfoBaseE: .quad 0\n"
    ".weak _ZTIN4llvm3orc12MemoryMapperE\n"
    "_ZTIN4llvm3orc12MemoryMapperE: .quad 0\n"
    ".popsection");
#endif

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

namespace {

// ORCv2 ObjectCache backed by per-function .o files on disk. getObject() is
// consulted by the compile layer BEFORE codegen (a hit skips AsmPrinter/
// MCAssembler entirely - the .o is just re-linked); notifyObjectCompiled() is
// called after a miss with the freshly emitted object to persist it. The
// emitted object keeps external calls (xe_llvm_*) UNRESOLVED and is re-linked by
// name on each load, so it is ASLR-safe across runs. Compilation is serialized
// by s_llvm_compile_mutex in LowerAndJit, so the file I/O here is effectively
// single-threaded (the atomics are belt-and-suspenders for the stats only).
class XeLlvmObjectCache : public llvm::ObjectCache {
 public:
  explicit XeLlvmObjectCache(std::filesystem::path dir)
      : dir_(std::move(dir)) {
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    PruneSupersededCacheDirs();
  }

  // Delete sibling cache directories that are not the active one.
  //
  // ⚠️ THIS EXISTS BECAUSE THE BUILD-STAMP IN THE DIRECTORY NAME IS A DISK LEAK
  // WITHOUT IT. The stamp (added 2026-08-09 so a changed lowering cannot be
  // served stale objects) means every rebuild of llvm_assembler.cc starts a
  // FRESH directory - and a populated one is ~400 MB. Device-measured the same
  // day: FIVE directories, 765 MB, after a single session of iteration, and
  // growing by a full cache per rebuild.
  //
  // That is not a cosmetic problem in this repo - a full disk here previously
  // presented as `clang++: error: linker command failed due to signal`, i.e. it
  // looks like a toolchain crash rather than an out-of-space condition.
  //
  // Safe by construction: only siblings inside the SAME objcache root whose
  // name starts with "objcache_" are touched, never the active directory, and
  // everything removed is a regenerable compile artifact. Failures are ignored
  // - losing the prune is harmless, and a cache dir held open by another
  // process must not stop us from booting.
  void PruneSupersededCacheDirs() {
    std::error_code ec;
    const auto root = dir_.parent_path();
    if (root.empty()) {
      return;
    }
    for (std::filesystem::directory_iterator it(root, ec), end;
         !ec && it != end; it.increment(ec)) {
      if (!it->is_directory(ec)) {
        continue;
      }
      const auto name = it->path().filename().string();
      if (name.rfind("objcache_", 0) != 0) {
        continue;  // not ours
      }
      if (it->path() == dir_) {
        continue;  // the live one
      }
      std::error_code rm_ec;
      const auto removed = std::filesystem::remove_all(it->path(), rm_ec);
      if (!rm_ec) {
        XELOGI("LLVM objcache: pruned superseded cache dir '{}' ({} files)",
               name, removed);
      }
    }
  }

  void notifyObjectCompiled(const llvm::Module* m,
                            llvm::MemoryBufferRef obj) override {
    if (!m || IsNoCache(m->getModuleIdentifier())) {
      return;  // baked a run-specific host pointer -> never persist
    }
    const std::filesystem::path path = FilePath(m->getModuleIdentifier());
    // Write to a temp then rename so a crash mid-write never leaves a truncated
    // .o that a later run would load as valid code.
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
      std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
      if (!f) {
        return;
      }
      f.write(obj.getBufferStart(),
              static_cast<std::streamsize>(obj.getBufferSize()));
      if (!f) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return;
      }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
      std::filesystem::remove(tmp, ec);
      return;
    }
    stored_.fetch_add(1, std::memory_order_relaxed);
  }

  std::unique_ptr<llvm::MemoryBuffer> getObject(const llvm::Module* m) override {
    if (!m || IsNoCache(m->getModuleIdentifier())) {
      return nullptr;  // force fresh codegen for non-portable functions
    }
    const std::filesystem::path path = FilePath(m->getModuleIdentifier());
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0) {
      misses_.fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }
    auto buf = llvm::MemoryBuffer::getFile(path.string(), /*IsText=*/false,
                                           /*RequiresNullTerminator=*/false);
    if (!buf) {
      misses_.fetch_add(1, std::memory_order_relaxed);
      return nullptr;
    }
    uint64_t h = hits_.fetch_add(1, std::memory_order_relaxed) + 1;
    if ((h & 511) == 0) {
      XELOGI("LLVM objcache: {} hits, {} stored, {} misses", h,
             stored_.load(std::memory_order_relaxed),
             misses_.load(std::memory_order_relaxed));
    }
    return std::move(*buf);  // layer takes ownership; getFile already owns it
  }

 private:
  // LowerAndJit prefixes a module "nocache_" when the function baked a
  // run-specific host pointer (CALL_EXTERN target / MMIO ptr) - such code can't
  // be reused across launches, so it is never read from / written to the cache.
  static bool IsNoCache(llvm::StringRef module_id) {
    return module_id.starts_with("nocache_");
  }
  std::filesystem::path FilePath(llvm::StringRef module_id) const {
    return dir_ / (module_id.str() + ".o");
  }
  std::filesystem::path dir_;
  std::atomic<uint64_t> hits_{0};
  std::atomic<uint64_t> stored_{0};
  std::atomic<uint64_t> misses_{0};
};

}  // namespace

std::unique_ptr<llvm::ObjectCache> CreateAndWireObjectCache(
    llvm::orc::LLJITBuilder& builder, const std::string& dir) {
  auto cache = std::make_unique<XeLlvmObjectCache>(std::filesystem::path(dir));
  llvm::ObjectCache* cache_raw = cache.get();
  // Route codegen through a SimpleCompiler bound to the cache (vs the default
  // ConcurrentIRCompiler). Compilation is serialized by s_llvm_compile_mutex in
  // LowerAndJit, so a single-TM SimpleCompiler is safe. The per-fn
  // target-features attr (x20/x21 reserve, -sve) lives in the IR, so it still
  // governs codegen through this compiler.
  builder.setCompileFunctionCreator(
      [cache_raw](llvm::orc::JITTargetMachineBuilder jtmb)
          -> llvm::Expected<
              std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler>> {
        auto tm = jtmb.createTargetMachine();
        if (!tm) {
          return tm.takeError();
        }
        return std::make_unique<llvm::orc::TMOwningSimpleCompiler>(
            std::move(*tm), cache_raw);
      });
  return cache;
}

#if !defined(_WIN32)

namespace {

// JIT code memory in large slabs with one protection for the whole slab.
//
// The default in-process JITLink memory manager mmaps one block per linked
// object and then mprotects each segment: a 4 KB r-x page for the code and a
// 4 KB r-- page for the constants and .eh_frame. With one LLVM module per guest
// function that is two VMAs per function, never mergeable because the
// permissions alternate. Device count, 2026-09-20: 14,181 + 14,181 VMAs at
// 14,181 functions. vm.max_map_count is 65,530, so a title with more than
// about 32,000 functions dies in the precompile with mmap ENOMEM (Scudo
// returns null, LLVM calls report_bad_alloc_error): Banjo-Kazooie at 37,632,
// Gears, MagnaCarta 2. This mapper reserves 64 MB slabs as rwx and never
// changes protections, so a slab stays one VMA. The a64 code cache is rwx too.
// The layout is still page based (MapperJITLinkMemoryManager), so the memory
// per function is unchanged.
class XeSlabMemoryMapper : public llvm::orc::MemoryMapper {
 public:
  explicit XeSlabMemoryMapper(size_t page_size) : page_size_(page_size) {}
  ~XeSlabMemoryMapper() override {
    for (auto& r : reservations_) {
      munmap(r.first, r.second);
    }
  }

  unsigned int getPageSize() override {
    return static_cast<unsigned int>(page_size_);
  }

  void reserve(size_t num_bytes, OnReservedFunction on_reserved) override {
    void* p = mmap(nullptr, num_bytes, PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
      on_reserved(llvm::make_error<llvm::StringError>(
          "XeSlabMemoryMapper: mmap of JIT slab failed",
          llvm::inconvertibleErrorCode()));
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      reservations_.emplace_back(p, num_bytes);
    }
    XELOGI("LLVM JIT memory: reserved a {} MB rwx slab at {}",
           num_bytes >> 20, p);
    on_reserved(llvm::orc::ExecutorAddrRange(
        llvm::orc::ExecutorAddr::fromPtr(p), num_bytes));
  }

  // In process: the working memory is the target memory.
  char* prepare(llvm::orc::ExecutorAddr addr, size_t) override {
    return addr.toPtr<char*>();
  }

  void initialize(AllocInfo& ai, OnInitializedFunction on_initialized) override {
    llvm::orc::ExecutorAddr min_addr(~0ULL);
    for (auto& seg : ai.Segments) {
      auto base = ai.MappingBase + seg.Offset;
      size_t size = seg.ContentSize + seg.ZeroFillSize;
      if (base < min_addr) {
        min_addr = base;
      }
      if (seg.ZeroFillSize) {
        std::memset((base + seg.ContentSize).toPtr<void*>(), 0,
                    seg.ZeroFillSize);
      }
      if (static_cast<unsigned>(seg.AG.getMemProt()) &
          static_cast<unsigned>(llvm::orc::MemProt::Exec)) {
        char* b = base.toPtr<char*>();
        __builtin___clear_cache(b, b + size);
      }
    }
    auto deinit = llvm::orc::shared::runFinalizeActions(ai.Actions);
    if (!deinit) {
      on_initialized(deinit.takeError());
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      deinit_actions_[min_addr] = std::move(*deinit);
    }
    on_initialized(min_addr);
  }

  void deinitialize(llvm::ArrayRef<llvm::orc::ExecutorAddr> allocations,
                    OnDeinitializedFunction on_deinitialized) override {
    llvm::Error err = llvm::Error::success();
    for (auto addr : allocations) {
      std::vector<llvm::orc::shared::WrapperFunctionCall> actions;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = deinit_actions_.find(addr);
        if (it != deinit_actions_.end()) {
          actions = std::move(it->second);
          deinit_actions_.erase(it);
        }
      }
      err = llvm::joinErrors(std::move(err),
                             llvm::orc::shared::runDeallocActions(actions));
    }
    on_deinitialized(std::move(err));
  }

  void release(llvm::ArrayRef<llvm::orc::ExecutorAddr> reservations,
               OnReleasedFunction on_released) override {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto addr : reservations) {
      void* p = addr.toPtr<void*>();
      for (auto it = reservations_.begin(); it != reservations_.end(); ++it) {
        if (it->first == p) {
          munmap(it->first, it->second);
          reservations_.erase(it);
          break;
        }
      }
    }
    on_released(llvm::Error::success());
  }

 private:
  size_t page_size_;
  std::mutex mutex_;
  std::vector<std::pair<void*, size_t>> reservations_;
  llvm::DenseMap<llvm::orc::ExecutorAddr,
                 std::vector<llvm::orc::shared::WrapperFunctionCall>>
      deinit_actions_;
};

constexpr size_t kJitSlabBytes = size_t(64) << 20;

}  // namespace

void WireSlabJitMemory(llvm::orc::LLJITBuilder& builder) {
  builder.setObjectLinkingLayerCreator(
      [](llvm::orc::ExecutionSession& es, const llvm::Triple&)
          -> llvm::Expected<std::unique_ptr<llvm::orc::ObjectLayer>> {
        size_t page_size = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        if (!page_size) {
          page_size = 4096;
        }
        auto mapper = std::make_unique<XeSlabMemoryMapper>(page_size);
        auto memmgr =
            std::make_unique<llvm::orc::MapperJITLinkMemoryManager>(
                kJitSlabBytes, std::move(mapper));
        return std::make_unique<llvm::orc::ObjectLinkingLayer>(
            es, std::move(memmgr));
      });
}

#else  // _WIN32

void WireSlabJitMemory(llvm::orc::LLJITBuilder&) {}

#endif  // !_WIN32

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XE_LLVM_BACKEND_ENABLED
