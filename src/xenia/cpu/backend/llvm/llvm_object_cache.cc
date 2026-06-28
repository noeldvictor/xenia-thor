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
#include <filesystem>
#include <fstream>
#include <system_error>

#include "xenia/base/logging.h"

#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/MemoryBuffer.h"

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

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XE_LLVM_BACKEND_ENABLED
