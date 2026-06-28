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

// One ORCv2 LLJIT for the whole process; each guest function is lowered into
// its own Module, addIRModule'd, then looked up for its native code pointer.
struct LlvmJitContext {
#if XE_LLVM_BACKEND_ENABLED
  // Declared BEFORE `jit`: members are destroyed in reverse order, so `jit`
  // (whose compile layer holds a raw ObjectCache*) is torn down before the cache
  // it points at. Null unless cpu_llvm_object_cache is enabled.
  std::unique_ptr<llvm::ObjectCache> object_cache;
  std::unique_ptr<llvm::orc::LLJIT> jit;
#endif
  bool initialized = false;
};

}  // namespace llvm_backend
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_LLVM_LLVM_JIT_CONTEXT_H_
