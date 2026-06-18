/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_PPC_PPC_FRONTEND_H_
#define XENIA_CPU_PPC_PPC_FRONTEND_H_

#include <cstdint>
#include <memory>

#include "xenia/base/type_pool.h"
#include "xenia/cpu/function.h"
#include "xenia/memory.h"

namespace xe {
namespace cpu {
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace ppc {

class PPCTranslator;

struct PPCBuiltins {
  int32_t global_lock_count;
  Function* check_global_lock;
  Function* enter_global_lock;
  Function* leave_global_lock;
  Function* syscall_handler;
  // Shared-function fast-path: native replacements for hot, byte-identical XDK
  // runtime kernels that recur across titles (cpu_shared_function_fastpath).
  Function* shared_memset;
  Function* shared_memcpy;
  Function* shared_memmove;
};

// Which native kernel a recognized shared guest function maps to.
enum class SharedFunctionKind : uint32_t {
  kNone = 0,
  kMemset,
  kMemcpy,
  kMemmove,
};

// Canonical, RELOCATION-INVARIANT hash of a guest function's instruction stream.
// The same statically-linked XDK/CRT library function is duplicated as guest
// machine code in every title that links it, but relocated to a different
// address - so its inter-function call targets differ. We zero the LI field of
// b/bl (primary opcode 18) before hashing so the function hashes identically
// across titles; everything else (including intra-function relative branches,
// which move with the function) is hashed raw. Masking is deliberately MINIMAL
// to keep false-positive collisions near-impossible: under-masking only yields a
// missed match (safe - falls back to normal JIT), never a wrong substitution.
// Leaf kernels (memset/memcpy/memmove: loops, no external calls) hash raw.
uint64_t CanonicalFunctionHashRaw(const void* code, uint32_t size_bytes);
uint64_t CanonicalFunctionHash(const Memory* memory, uint32_t start_address,
                               uint32_t end_address);

// Curated hash+size -> kind lookup. kNone if unrecognized. Entries are added
// ONLY after a harvested hash is RE-confirmed to be the named XDK kernel, so a
// match is exact (and the native handler is byte-equivalent anyway).
SharedFunctionKind LookupSharedFunction(uint64_t canonical_hash,
                                        uint32_t size_bytes);
// Register a (hash,size)->kind mapping (host test uses this to drive the
// substitution path without a device-harvested table).
void RegisterSharedFunctionForTesting(uint64_t canonical_hash,
                                      uint32_t size_bytes,
                                      SharedFunctionKind kind);

struct GlobalLockOwnerSnapshot {
  int32_t count;
  uint32_t thread_id;
  uint32_t lr;
  uint32_t ctr;
  uint32_t r1;
  uint32_t r3;
  uint32_t r4;
};

GlobalLockOwnerSnapshot QueryGlobalLockOwnerSnapshot();

class PPCFrontend {
 public:
  explicit PPCFrontend(Processor* processor);
  ~PPCFrontend();

  bool Initialize();

  Processor* processor() const { return processor_; }
  Memory* memory() const;
  PPCBuiltins* builtins() { return &builtins_; }

  bool DeclareFunction(GuestFunction* function);
  bool DefineFunction(GuestFunction* function, uint32_t debug_info_flags);

 private:
  Processor* processor_;
  PPCBuiltins builtins_ = {0};
  TypePool<PPCTranslator, PPCFrontend*> translator_pool_;
};

}  // namespace ppc
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_PPC_PPC_FRONTEND_H_
