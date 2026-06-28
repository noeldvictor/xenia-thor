/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_LLVM_LLVM_OBJECT_CACHE_H_
#define XENIA_CPU_BACKEND_LLVM_LLVM_OBJECT_CACHE_H_

#include <cstdint>
#include <memory>
#include <string>

#ifndef XE_LLVM_BACKEND_ENABLED
#define XE_LLVM_BACKEND_ENABLED 0
#endif

#if XE_LLVM_BACKEND_ENABLED
#include "llvm/ExecutionEngine/ObjectCache.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

// Bump when the LLVM lowering, ABI, or runtime-helper set changes so cached .o
// files compiled by an older xenia are ignored (a stale .o would link against a
// changed call ABI -> wild execution). Part of the cache subdir name, so a bump
// just starts a fresh dir; the old one can be deleted. Used by llvm_backend.cc to
// build the per-version cache dir.
constexpr uint32_t kLlvmObjectCacheVersion = 1;

// Creates the disk-backed ORCv2 AOT object cache rooted at `dir`, wires `builder`
// to compile through a SimpleCompiler bound to it (so getObject() is consulted
// before codegen and emitted objects are persisted via notifyObjectCompiled()),
// and returns the cache (which MUST outlive the LLJIT - its compiler holds a raw
// pointer to it). Returns nullptr on allocation failure.
//
// This + its XeLlvmObjectCache subclass + the SimpleCompiler instantiation are
// isolated in llvm_object_cache.cc, which provides weak placeholder typeinfo
// symbols for llvm::ObjectCache / llvm::orc::SimpleCompiler (the -fno-rtti
// libLLVM omits them, but our -frtti subclass references them) - see that file.
std::unique_ptr<llvm::ObjectCache> CreateAndWireObjectCache(
    llvm::orc::LLJITBuilder& builder, const std::string& dir);

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XE_LLVM_BACKEND_ENABLED
#endif  // XENIA_CPU_BACKEND_LLVM_LLVM_OBJECT_CACHE_H_
