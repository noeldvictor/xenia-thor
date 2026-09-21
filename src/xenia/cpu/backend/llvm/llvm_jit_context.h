/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_LLVM_LLVM_JIT_CONTEXT_H_
#define XENIA_CPU_BACKEND_LLVM_LLVM_JIT_CONTEXT_H_

#include <memory>

// Internal (not the public backend header): the full LlvmJitContext definition,
// shared by llvm_backend.cc and llvm_assembler.cc. The public llvm_backend.h
// only forward-declares it so emulator.cc never pulls in LLVM headers.
#ifndef XE_LLVM_BACKEND_ENABLED
#define XE_LLVM_BACKEND_ENABLED 0
#endif

#if XE_LLVM_BACKEND_ENABLED
#include "llvm/ExecutionEngine/ObjectCache.h"
#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#endif

namespace xe {
namespace cpu {
namespace backend {
namespace llvm_backend {

#include <mutex>
#include <vector>

// A pool of ORCv2 LLJITs. Each guest function is lowered into its own Module,
// addIRModule'd into one unit, then looked up for its native code pointer.
//
// Until 2026-09-20 there was one LLJIT and one global compile lock, because
// concurrent codegen through one LLJIT corrupted the heap (SIGBUS in
// MCAssembler, device-found). The six precompile workers queued on that lock
// and the compile ran on one core: about 20 ms per function, Blue Dragon
// 19,884 functions in 414 s, Banjo 38,104 in 693 s. Each unit here owns its
// own LLJIT (TargetMachine, contexts, linking layer, slab memory) and its own
// lock, so units compile in parallel. Guest calls go through xenia's own
// dispatch, not LLVM symbol resolution, so the units need no shared symbols
// except the runtime helpers, which every unit defines.
struct LlvmJitUnit {
#if XE_LLVM_BACKEND_ENABLED
  std::unique_ptr<llvm::orc::LLJIT> jit;
#endif
  std::timed_mutex mutex;
};

struct LlvmJitContext {
#if XE_LLVM_BACKEND_ENABLED
  // Declared BEFORE the units: members are destroyed in reverse order, so every
  // unit (whose compile layer holds a raw ObjectCache*) is torn down before the
  // cache it points at. Null unless cpu_llvm_object_cache is enabled.
  std::unique_ptr<llvm::ObjectCache> object_cache;
  std::vector<std::unique_ptr<LlvmJitUnit>> units;
  // Unit 0, kept for the callers that only need a data layout or a triple.
  llvm::orc::LLJIT* jit = nullptr;
#endif
  bool initialized = false;
};

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_LLVM_LLVM_JIT_CONTEXT_H_
