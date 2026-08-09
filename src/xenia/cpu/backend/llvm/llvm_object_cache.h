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
// v2 (2026-07-24): the cache key gained the residency_writeback / residency_abi
// bits. v1 keys encoded only opt + context_residency, so a v1 .o compiled under a
// DIFFERENT writeback/abi setting could be served to a run that asked for another
// one - see the key construction in llvm_assembler.cc.
// v3 (2026-08-09): DEVICE-PROVEN that a stale cache is not theoretical - it is
// the "intermittent startup stall" that cost five measurement attempts and
// several retracted diagnoses. Measured 3/3 vs 3/3 on one variable:
//   stale cache (92,556 objects, written by older builds) -> the guest main
//     thread parks at its FIRST wait: 0 CPU ticks EVER, vctx=2, 0 frames
//   fresh or bypassed cache -> guest runs normally, vctx=31,000+, 838 frames
// Exactly the "wild execution" this comment warned about, except it presents
// as a silent hang rather than a crash, which is why it was misread for days.
constexpr uint32_t kLlvmObjectCacheVersion = 3;

// Compile-time stamp of the translation unit that owns EVERY LLVM lowering
// (llvm_assembler.cc), folded into the cache directory name.
//
// WHY THIS EXISTS: the version constant above only invalidates when a HUMAN
// remembers to bump it on a lowering change. That is a promise, not a
// mechanism, and it was broken twice in a single day (scalar-FMA lowering and
// the VPERM TBX lowering both landed without a bump) - which is what produced
// the stall above. Deriving the identity from the compile stamp makes the
// invalidation automatic for the case that actually bites: the lowerings
// changed but the cache did not.
//
// NOT git-derived on purpose: build/version.h comes from git HEAD, so it does
// not change for UNCOMMITTED edits - i.e. it is blind during exactly the
// edit-build-test loop where stale objects get produced.
const char* LlvmLoweringBuildStamp();

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
