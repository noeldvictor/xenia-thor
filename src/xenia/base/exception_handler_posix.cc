/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/exception_handler.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>
#include <unwind.h>
#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "xenia/base/platform.h"

#if XE_PLATFORM_ANDROID
#include <android/log.h>
#endif

#include "xenia/base/assert.h"
#include "xenia/base/host_thread_context.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"

namespace xe {

bool signal_handlers_installed_ = false;
struct sigaction original_sigill_handler_;
struct sigaction original_sigsegv_handler_;
struct sigaction original_sigbus_handler_;

// This can be as large as needed, but isn't often needed.
// As we will be sometimes firing many exceptions we want to avoid having to
// scan the table too much or invoke many custom handlers.
constexpr size_t kMaxHandlerCount = 8;

// All custom handlers, left-aligned and null terminated.
// Executed in order.
std::pair<ExceptionHandler::Handler, void*> handlers_[kMaxHandlerCount];

// /proc/self/mem, opened once at Install time. Reading the faulting instruction
// through pread on this fd cannot itself fault, unlike dereferencing the pc
// directly - a jump to an unmapped address (the classic JIT/HLE codegen bug)
// used to kill the process INSIDE this handler, producing no log, no tombstone
// and no clue at all. -1 until installed.
int self_mem_fd_ = -1;

// Reads the 4-byte instruction at |pc| without faulting. Returns false if the
// address is unmapped, in which case *out is left untouched. Async-signal-safe
// (pread is; dereferencing an arbitrary pointer is not).
bool SafeReadInstruction(uint64_t pc, uint32_t* out) {
  if (self_mem_fd_ < 0) {
    return false;
  }
  uint32_t value = 0;
  ssize_t read_bytes =
      pread(self_mem_fd_, &value, sizeof(value), static_cast<off_t>(pc));
  if (read_bytes != static_cast<ssize_t>(sizeof(value))) {
    return false;
  }
  *out = value;
  return true;
}

// Writes DIRECTLY to logcat/stderr, bypassing XELOG*. Xenia's logger is
// asynchronous (a writer thread drains a ring buffer), so anything XELOGE'd
// from a fault handler is lost when the process dies moments later - which is
// why every one of these startup crashes presented with no diagnostic at all.
// This lands synchronously.
XE_NOINLINE void FaultLog(const char* format, ...) {
  va_list args;
  va_start(args, format);
#if XE_PLATFORM_ANDROID
  __android_log_vprint(ANDROID_LOG_ERROR, "xenia-fault", format, args);
#else
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  fflush(stderr);
#endif
  va_end(args);
}

struct BacktraceState {
  uint64_t frames[24];
  uint32_t count;
};

_Unwind_Reason_Code BacktraceFrame(struct _Unwind_Context* context, void* arg) {
  auto* state = static_cast<BacktraceState*>(arg);
  uint64_t pc = uint64_t(_Unwind_GetIP(context));
  if (pc && state->count < xe::countof(state->frames)) {
    state->frames[state->count++] = pc;
    return _URC_NO_REASON;
  }
  return _URC_END_OF_STACK;
}

// Logs a symbolized host backtrace. Only called on the fatal path, where the
// process is already lost - so the (not strictly async-signal-safe) logging and
// dladdr calls are an acceptable trade for having ANY diagnostic at all.
void LogHostBacktrace(const char* what) {
  BacktraceState state;
  state.count = 0;
  _Unwind_Backtrace(&BacktraceFrame, &state);
  FaultLog("--- host backtrace (%s), %u frames ---", what, state.count);
  for (uint32_t i = 0; i < state.count; ++i) {
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(state.frames[i]), &info) &&
        info.dli_fname) {
      uint64_t module_base =
          uint64_t(reinterpret_cast<uintptr_t>(info.dli_fbase));
      FaultLog("  #%02u pc %016llX  %s+%llX  %s", i,
               (unsigned long long)state.frames[i], info.dli_fname,
               (unsigned long long)(state.frames[i] - module_base),
               info.dli_sname ? info.dli_sname : "?");
    } else {
      FaultLog("  #%02u pc %016llX  <unmapped>", i,
               (unsigned long long)state.frames[i]);
    }
  }
}

// Count of faults no installed handler resolved. The handler re-executes the
// faulting instruction on an unresolved fault, so a climbing value is a re-fault
// storm pinning a core (see exception_handler.h / GetUnhandledFaultCount).
std::atomic<uint32_t> unhandled_fault_count_{0};
// Re-fault storm detection: the pc + consecutive-repeat count of the last
// unresolved fault. When the SAME pc repeats, the thread is in an infinite
// re-fault loop and is parked (see the storm guard in ExceptionHandlerCallback).
std::atomic<uint64_t> unhandled_last_pc_{0};
std::atomic<uint32_t> unhandled_repeat_count_{0};

static void ExceptionHandlerCallback(int signal_number, siginfo_t* signal_info,
                                     void* signal_context) {
  mcontext_t& mcontext =
      reinterpret_cast<ucontext_t*>(signal_context)->uc_mcontext;

  // Breadcrumb for the first few faults on the fatal path: without it there is
  // no way to tell "the handler never ran" (dead before install / bad stack)
  // apart from "the handler ran and something inside it died".
  {
    static std::atomic<uint32_t> entry_count{0};
    uint32_t entry = entry_count.fetch_add(1, std::memory_order_relaxed);
    if (entry < 4) {
#if XE_ARCH_ARM64
      FaultLog("host fault entered: sig=%d addr=%016llX pc=%016llX", signal_number,
               (unsigned long long)reinterpret_cast<uintptr_t>(
                   signal_info->si_addr),
               (unsigned long long)mcontext.pc);
#else
      FaultLog("host fault entered: sig=%d addr=%016llX", signal_number,
               (unsigned long long)reinterpret_cast<uintptr_t>(
                   signal_info->si_addr));
#endif
      // NOTE: adding LogHostBacktrace("fault entry") here dumps the stack of
      // whoever faulted first, which is how the 2026-08-03 startup crashes were
      // found when the process was dying before any other reporting could run.
      // Not kept on by default: guest write-watch faults are routine, so the
      // first fault of a healthy run is normally benign and the dump is just
      // noise. Re-add it temporarily when a fault kills the process outright.
    }
  }

  HostThreadContext thread_context;

#if XE_ARCH_AMD64
  thread_context.rip = uint64_t(mcontext.gregs[REG_RIP]);
  thread_context.eflags = uint32_t(mcontext.gregs[REG_EFL]);
  // The REG_ order may be different than the register indices in the
  // instruction encoding.
  thread_context.rax = uint64_t(mcontext.gregs[REG_RAX]);
  thread_context.rcx = uint64_t(mcontext.gregs[REG_RCX]);
  thread_context.rdx = uint64_t(mcontext.gregs[REG_RDX]);
  thread_context.rbx = uint64_t(mcontext.gregs[REG_RBX]);
  thread_context.rsp = uint64_t(mcontext.gregs[REG_RSP]);
  thread_context.rbp = uint64_t(mcontext.gregs[REG_RBP]);
  thread_context.rsi = uint64_t(mcontext.gregs[REG_RSI]);
  thread_context.rdi = uint64_t(mcontext.gregs[REG_RDI]);
  thread_context.r8 = uint64_t(mcontext.gregs[REG_R8]);
  thread_context.r9 = uint64_t(mcontext.gregs[REG_R9]);
  thread_context.r10 = uint64_t(mcontext.gregs[REG_R10]);
  thread_context.r11 = uint64_t(mcontext.gregs[REG_R11]);
  thread_context.r12 = uint64_t(mcontext.gregs[REG_R12]);
  thread_context.r13 = uint64_t(mcontext.gregs[REG_R13]);
  thread_context.r14 = uint64_t(mcontext.gregs[REG_R14]);
  thread_context.r15 = uint64_t(mcontext.gregs[REG_R15]);
  std::memcpy(thread_context.xmm_registers, mcontext.fpregs->_xmm,
              sizeof(thread_context.xmm_registers));
#elif XE_ARCH_ARM64
  std::memcpy(thread_context.x, mcontext.regs, sizeof(thread_context.x));
  thread_context.sp = mcontext.sp;
  thread_context.pc = mcontext.pc;
  thread_context.pstate = mcontext.pstate;
  struct fpsimd_context* mcontext_fpsimd = nullptr;
  struct esr_context* mcontext_esr = nullptr;
  for (struct _aarch64_ctx* mcontext_extension =
           reinterpret_cast<struct _aarch64_ctx*>(mcontext.__reserved);
       mcontext_extension->magic;
       mcontext_extension = reinterpret_cast<struct _aarch64_ctx*>(
           reinterpret_cast<uint8_t*>(mcontext_extension) +
           mcontext_extension->size)) {
    switch (mcontext_extension->magic) {
      case FPSIMD_MAGIC:
        mcontext_fpsimd =
            reinterpret_cast<struct fpsimd_context*>(mcontext_extension);
        break;
      case ESR_MAGIC:
        mcontext_esr =
            reinterpret_cast<struct esr_context*>(mcontext_extension);
        break;
      default:
        break;
    }
  }
  assert_not_null(mcontext_fpsimd);
  if (mcontext_fpsimd) {
    thread_context.fpsr = mcontext_fpsimd->fpsr;
    thread_context.fpcr = mcontext_fpsimd->fpcr;
    std::memcpy(thread_context.v, mcontext_fpsimd->vregs,
                sizeof(thread_context.v));
  }
#endif  // XE_ARCH

  Exception ex;
  switch (signal_number) {
    case SIGILL:
      ex.InitializeIllegalInstruction(&thread_context);
      break;
    // SIGBUS is an access-violation class fault too (e.g. an access past the
    // end of the shm backing file of a guest memory view) - dispatch it like
    // SIGSEGV so the guest fault handlers get a chance (xenia-edge parity).
    case SIGBUS:
    case SIGSEGV: {
      Exception::AccessViolationOperation access_violation_operation;
#if XE_ARCH_AMD64
      // x86_pf_error_code::X86_PF_WRITE
      constexpr uint64_t kX86PageFaultErrorCodeWrite = UINT64_C(1) << 1;
      access_violation_operation =
          (uint64_t(mcontext.gregs[REG_ERR]) & kX86PageFaultErrorCodeWrite)
              ? Exception::AccessViolationOperation::kWrite
              : Exception::AccessViolationOperation::kRead;
#elif XE_ARCH_ARM64
      // For a Data Abort (EC - ESR_EL1 bits 31:26 - 0b100100 from a lower
      // Exception Level, 0b100101 without a change in the Exception Level),
      // bit 6 is 0 for reading from a memory location, 1 for writing to a
      // memory location.
      if (mcontext_esr && ((mcontext_esr->esr >> 26) & 0b111110) == 0b100100) {
        access_violation_operation =
            (mcontext_esr->esr & (UINT64_C(1) << 6))
                ? Exception::AccessViolationOperation::kWrite
                : Exception::AccessViolationOperation::kRead;
      } else {
        // Determine the memory access direction based on which instruction has
        // requested it.
        // esr_context may be unavailable on certain hosts (for instance, on
        // Android, it was added only in NDK r16 - which is the first NDK
        // version to support the Android API level 27, while NDK r15 doesn't
        // have esr_context in its API 26 sigcontext.h).
        // On AArch64 (unlike on AArch32), the program counter is the address of
        // the currently executing instruction.
        // NOTE: read the faulting instruction through /proc/self/mem rather
        // than dereferencing the pc. If the pc itself is what is unmapped
        // (branch to a bad address), a direct load faults a second time while
        // already inside the handler and the process dies with NO diagnostic.
        uint32_t fault_instruction = 0;
        bool instruction_readable =
            SafeReadInstruction(uint64_t(mcontext.pc), &fault_instruction);
        bool instruction_is_store;
        if (instruction_readable &&
            IsArm64LoadPrefetchStore(fault_instruction, instruction_is_store)) {
          access_violation_operation =
              instruction_is_store ? Exception::AccessViolationOperation::kWrite
                                   : Exception::AccessViolationOperation::kRead;
        } else {
          FaultLog(
              "ARM64 SIGSEGV could not classify access: pc=%016llX "
              "instruction=%08X (readable=%d) fault=%016llX esr_present=%d "
              "esr=%016llX",
              (unsigned long long)mcontext.pc, fault_instruction,
              instruction_readable ? 1 : 0,
              (unsigned long long)reinterpret_cast<uintptr_t>(
                  signal_info->si_addr),
              mcontext_esr ? 1 : 0,
              (unsigned long long)(mcontext_esr ? mcontext_esr->esr : 0));
          if (!instruction_readable) {
            // The pc is unmapped: this is a branch to a bad address, not a data
            // access. No guest handler can resolve it, so report it here where
            // there is still a usable stack.
            LogHostBacktrace("unmapped pc");
          }
          access_violation_operation =
              Exception::AccessViolationOperation::kUnknown;
        }
      }
#else
      access_violation_operation =
          Exception::AccessViolationOperation::kUnknown;
#endif  // XE_ARCH
      ex.InitializeAccessViolation(
          &thread_context, reinterpret_cast<uint64_t>(signal_info->si_addr),
          access_violation_operation);
    } break;
    default:
      assert_unhandled_case(signal_number);
  }

  for (size_t i = 0; i < xe::countof(handlers_) && handlers_[i].first; ++i) {
    if (handlers_[i].first(&ex, handlers_[i].second)) {
      // Exception handled.
#if XE_ARCH_AMD64
      mcontext.gregs[REG_RIP] = greg_t(thread_context.rip);
      mcontext.gregs[REG_EFL] = greg_t(thread_context.eflags);
      uint32_t modified_register_index;
      // The order must match the order in X64Register.
      static const size_t kIntRegisterMap[] = {
          REG_RAX, REG_RCX, REG_RDX, REG_RBX, REG_RSP, REG_RBP,
          REG_RSI, REG_RDI, REG_R8,  REG_R9,  REG_R10, REG_R11,
          REG_R12, REG_R13, REG_R14, REG_R15,
      };
      uint16_t modified_int_registers_remaining = ex.modified_int_registers();
      while (xe::bit_scan_forward(modified_int_registers_remaining,
                                  &modified_register_index)) {
        modified_int_registers_remaining &=
            ~(UINT16_C(1) << modified_register_index);
        mcontext.gregs[kIntRegisterMap[modified_register_index]] =
            thread_context.int_registers[modified_register_index];
      }
      uint16_t modified_xmm_registers_remaining = ex.modified_xmm_registers();
      while (xe::bit_scan_forward(modified_xmm_registers_remaining,
                                  &modified_register_index)) {
        modified_xmm_registers_remaining &=
            ~(UINT16_C(1) << modified_register_index);
        std::memcpy(&mcontext.fpregs->_xmm[modified_register_index],
                    &thread_context.xmm_registers[modified_register_index],
                    sizeof(vec128_t));
      }
#elif XE_ARCH_ARM64
      uint32_t modified_register_index;
      uint32_t modified_x_registers_remaining = ex.modified_x_registers();
      while (xe::bit_scan_forward(modified_x_registers_remaining,
                                  &modified_register_index)) {
        modified_x_registers_remaining &=
            ~(UINT32_C(1) << modified_register_index);
        mcontext.regs[modified_register_index] =
            thread_context.x[modified_register_index];
      }
      mcontext.sp = thread_context.sp;
      mcontext.pc = thread_context.pc;
      mcontext.pstate = thread_context.pstate;
      if (mcontext_fpsimd) {
        mcontext_fpsimd->fpsr = thread_context.fpsr;
        mcontext_fpsimd->fpcr = thread_context.fpcr;
        uint32_t modified_v_registers_remaining = ex.modified_v_registers();
        while (xe::bit_scan_forward(modified_v_registers_remaining,
                                    &modified_register_index)) {
          modified_v_registers_remaining &=
              ~(UINT32_C(1) << modified_register_index);
          std::memcpy(&mcontext_fpsimd->vregs[modified_register_index],
                      &thread_context.v[modified_register_index],
                      sizeof(vec128_t));
          mcontext.regs[modified_register_index] =
              thread_context.x[modified_register_index];
        }
      }
#endif  // XE_ARCH
      return;
    }
  }

  // No xenia handler resolved this fault. On POSIX we then fall through and
  // RETURN from the signal handler WITHOUT advancing the PC or chaining to the
  // original handler -> the kernel re-executes the faulting instruction. If the
  // fault is deterministic this is an infinite re-fault "signal storm" (0 fps,
  // no crash). This is exactly what the LLVM-JIT backend hit: a fault in
  // ORCv2-JIT'd code whose PC is outside the a64 code cache, so MMIOHandler /
  // A64Backend / Emulator all reject it. Log the fault (rate-limited) so the
  // offending PC / instruction / address / guest ctx+membase is visible instead
  // of a silent hang. x[20]=guest PPCContext, x[21]=guest membase (a wrong x21
  // here => the JIT emitted/clobbered membase => wild guest address).
#if XE_ARCH_ARM64
  {
    uint32_t un = unhandled_fault_count_.fetch_add(1, std::memory_order_relaxed);
    if (un < 16) {
      uint32_t insn = 0;
      SafeReadInstruction(uint64_t(mcontext.pc), &insn);
      FaultLog(
          "UNHANDLED host fault #%u: code=%u pc=%016llX insn=0x%08X "
          "fault_addr=%016llX x20_ctx=%016llX x21_membase=%016llX "
          "x30_lr=%016llX - no handler resolved it; return will re-fault "
          "(signal storm). Likely a JIT codegen bug (wild addr / bad instr / "
          "membase).",
          un, static_cast<uint32_t>(ex.code()),
          (unsigned long long)mcontext.pc, insn,
          (unsigned long long)ex.fault_address(),
          (unsigned long long)mcontext.regs[20],
          (unsigned long long)mcontext.regs[21],
          (unsigned long long)mcontext.regs[30]);
      if (un == 0) {
        LogHostBacktrace("first unhandled fault");
      }
    }
    // STORM GUARD: returning re-executes the faulting instruction; a
    // deterministic unresolved fault therefore re-faults FOREVER, pinning a core
    // AND saturating the signal-delivery path (libsigchain/kernel) so other
    // threads' faults - the renderer's write-watch - stall too => BD freezes
    // (device-observed for an intermittent libLLVM codegen crash). If the SAME pc
    // repeats, PARK this thread instead of returning: the storm stops, other
    // threads resume, and in-flight/future LLVM compiles fall back to a64 via
    // GetUnhandledFaultCount(). The stuck thread is sacrificed (it was already
    // lost to the re-fault loop). Async-signal-safe: only atomics + nanosleep.
    uint64_t storm_pc = uint64_t(mcontext.pc);
    if (storm_pc == unhandled_last_pc_.load(std::memory_order_relaxed)) {
      if (unhandled_repeat_count_.fetch_add(1, std::memory_order_relaxed) >= 8) {
        FaultLog(
            "UNHANDLED fault STORM at pc=%016llX - parking this thread to stop "
            "the infinite re-fault (other threads continue; LLVM compiles fall "
            "back to a64). Root cause: a libLLVM/JIT codegen crash.",
            (unsigned long long)storm_pc);
        for (;;) {
          struct timespec ts;
          ts.tv_sec = 3600;
          ts.tv_nsec = 0;
          nanosleep(&ts, nullptr);
        }
      }
    } else {
      unhandled_last_pc_.store(storm_pc, std::memory_order_relaxed);
      unhandled_repeat_count_.store(0, std::memory_order_relaxed);
    }
  }
#endif  // XE_ARCH_ARM64
}

void ExceptionHandler::InstallAlternateSignalStackForCurrentThread() {
  static thread_local bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  // Leaked deliberately: the stack must outlive every signal this thread can
  // take, including ones raised during thread teardown.
  size_t stack_size = size_t(SIGSTKSZ) * 4;
  void* stack_memory = malloc(stack_size);
  if (!stack_memory) {
    return;
  }
  stack_t signal_stack;
  std::memset(&signal_stack, 0, sizeof(signal_stack));
  signal_stack.ss_sp = stack_memory;
  signal_stack.ss_size = stack_size;
  signal_stack.ss_flags = 0;
  sigaltstack(&signal_stack, nullptr);
}

void ExceptionHandler::Install(Handler fn, void* data) {
  // Cover the installing (main) thread; threads created later get theirs in
  // ThreadStartRoutine.
  InstallAlternateSignalStackForCurrentThread();

  if (!signal_handlers_installed_) {
    struct sigaction signal_handler;

    // Opened before the handlers so the very first fault can already read its
    // own faulting instruction safely (see SafeReadInstruction).
    if (self_mem_fd_ < 0) {
      self_mem_fd_ = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
      if (self_mem_fd_ < 0) {
        XELOGW(
            "Could not open /proc/self/mem; faults at an unmapped pc will be "
            "reported without the faulting instruction.");
      }
    }

    std::memset(&signal_handler, 0, sizeof(signal_handler));
    signal_handler.sa_sigaction = ExceptionHandlerCallback;
    // SA_ONSTACK: run the handler on the alternate stack installed by
    // InstallAlternateSignalStack for this thread. Without it a stack-overflow
    // SIGSEGV cannot run the handler at all (no room to push a frame) and the
    // process dies instantly with no log and no tombstone.
    signal_handler.sa_flags = SA_SIGINFO | SA_ONSTACK;

    if (sigaction(SIGILL, &signal_handler, &original_sigill_handler_) != 0) {
      assert_always("Failed to install new SIGILL handler");
    }
    if (sigaction(SIGSEGV, &signal_handler, &original_sigsegv_handler_) != 0) {
      assert_always("Failed to install new SIGSEGV handler");
    }
    if (sigaction(SIGBUS, &signal_handler, &original_sigbus_handler_) != 0) {
      assert_always("Failed to install new SIGBUS handler");
    }
    signal_handlers_installed_ = true;
  }

  for (size_t i = 0; i < xe::countof(handlers_); ++i) {
    if (!handlers_[i].first) {
      handlers_[i].first = fn;
      handlers_[i].second = data;
      return;
    }
  }
  assert_always("Too many exception handlers installed");
}

void ExceptionHandler::Uninstall(Handler fn, void* data) {
  for (size_t i = 0; i < xe::countof(handlers_); ++i) {
    if (handlers_[i].first == fn && handlers_[i].second == data) {
      for (; i < xe::countof(handlers_) - 1; ++i) {
        handlers_[i] = handlers_[i + 1];
      }
      handlers_[i].first = nullptr;
      handlers_[i].second = nullptr;
      break;
    }
  }

  bool has_any = false;
  for (size_t i = 0; i < xe::countof(handlers_); ++i) {
    if (handlers_[i].first) {
      has_any = true;
      break;
    }
  }
  if (!has_any) {
    if (signal_handlers_installed_) {
      if (sigaction(SIGILL, &original_sigill_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGILL handler");
      }
      if (sigaction(SIGSEGV, &original_sigsegv_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGSEGV handler");
      }
      if (sigaction(SIGBUS, &original_sigbus_handler_, NULL) != 0) {
        assert_always("Failed to restore original SIGBUS handler");
      }
      signal_handlers_installed_ = false;
    }
  }
}

uint32_t ExceptionHandler::GetUnhandledFaultCount() {
  return unhandled_fault_count_.load(std::memory_order_relaxed);
}

}  // namespace xe
