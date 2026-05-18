/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_BACKEND_H_
#define XENIA_CPU_BACKEND_BACKEND_H_

#include <map>
#include <memory>
#include <mutex>

#include "xenia/cpu/backend/machine_info.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/thread_debug_info.h"

namespace xe {
namespace cpu {
class Breakpoint;
class Function;
class GuestFunction;
class Module;
class Processor;
}  // namespace cpu
}  // namespace xe

namespace xe {
namespace cpu {
namespace backend {

static constexpr uint32_t MAX_GUEST_PSEUDO_STACKTRACE_ENTRIES = 32;

struct GuestPseudoStackTrace {
  uint32_t count = 0;
  uint32_t truncated_flag = 0;
  uint32_t return_addrs[MAX_GUEST_PSEUDO_STACKTRACE_ENTRIES] = {};
};

using GuestTrampolineProc = void (*)(ppc::PPCContext* context, void* userarg1,
                                     void* userarg2);
using SimpleGuestTrampolineProc = void (*)(ppc::PPCContext*);

class Assembler;
class CodeCache;

class Backend {
 public:
  explicit Backend();
  virtual ~Backend();

  Processor* processor() const { return processor_; }
  const MachineInfo* machine_info() const { return &machine_info_; }
  CodeCache* code_cache() const { return code_cache_; }

  virtual bool Initialize(Processor* processor);

  virtual void* AllocThreadData();
  virtual void FreeThreadData(void* thread_data);

  virtual void CommitExecutableRange(uint32_t guest_low,
                                     uint32_t guest_high) = 0;

  virtual std::unique_ptr<Assembler> CreateAssembler() = 0;

  virtual std::unique_ptr<GuestFunction> CreateGuestFunction(
      Module* module, uint32_t address) = 0;

  // Calculates the next host instruction based on the current thread state and
  // current PC. This will look for branches and other control flow
  // instructions.
  virtual uint64_t CalculateNextHostInstruction(ThreadDebugInfo* thread_info,
                                                uint64_t current_pc) = 0;

  virtual void InstallBreakpoint(Breakpoint* breakpoint) {}
  virtual void InstallBreakpoint(Breakpoint* breakpoint, Function* fn) {}
  virtual void UninstallBreakpoint(Breakpoint* breakpoint) {}

  virtual void InitializeBackendContext(void* ctx) {}
  virtual void DeinitializeBackendContext(void* ctx) {}
  virtual void SetGuestRoundingMode(void* ctx, unsigned int mode) {}
  virtual void PrepareForReentry(void* ctx) {}
  virtual bool PopulatePseudoStacktrace(GuestPseudoStackTrace* st) {
    return false;
  }

  virtual uint32_t CreateGuestTrampoline(GuestTrampolineProc proc,
                                         void* userdata1, void* userdata2,
                                         bool long_term = false) {
    return 0;
  }
  uint32_t CreateGuestTrampoline(SimpleGuestTrampolineProc proc,
                                 bool long_term = false) {
    return CreateGuestTrampoline(
        reinterpret_cast<GuestTrampolineProc>(proc), nullptr, nullptr,
        long_term);
  }
  uint32_t CreateLongTermGuestTrampoline(SimpleGuestTrampolineProc proc) {
    return CreateGuestTrampoline(
        reinterpret_cast<GuestTrampolineProc>(proc), nullptr, nullptr, true);
  }
  virtual void FreeGuestTrampoline(uint32_t trampoline_addr) {}

 protected:
  Processor* processor_ = nullptr;
  MachineInfo machine_info_;
  CodeCache* code_cache_ = nullptr;
};

class GuestTrampolineGroup
    : public std::map<SimpleGuestTrampolineProc, uint32_t> {
 public:
  explicit GuestTrampolineGroup(Backend* backend) : backend_(backend) {}
  ~GuestTrampolineGroup() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto&& entry : *this) {
      backend_->FreeGuestTrampoline(entry.second);
    }
  }

  uint32_t NewLongtermTrampoline(SimpleGuestTrampolineProc proc) {
    return NewTrampolineInternal(proc, true);
  }

  uint32_t NewTrampoline(SimpleGuestTrampolineProc proc) {
    return NewTrampolineInternal(proc, false);
  }

 private:
  uint32_t NewTrampolineInternal(SimpleGuestTrampolineProc proc,
                                 bool long_term) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto iter = find(proc);
    if (iter != end()) {
      return iter->second;
    }

    uint32_t new_entry = long_term
                             ? backend_->CreateLongTermGuestTrampoline(proc)
                             : backend_->CreateGuestTrampoline(proc);
    emplace_hint(iter, proc, new_entry);
    return new_entry;
  }

  Backend* const backend_;
  std::mutex mutex_;
};

}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_BACKEND_H_
