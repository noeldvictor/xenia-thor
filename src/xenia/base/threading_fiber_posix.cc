/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#if !defined(__ELF__)
#error     "threading_fiber_posix.cc: ELF-only assembly (Android/Linux); add Mach-O directives for a macOS build"
#endif

#include <sys/mman.h>
#include <unistd.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "xenia/base/assert.h"
#include "xenia/base/threading.h"

// POSIX (Android/Linux) implementation of xe::threading::Fiber for the
// cooperative guest scheduler. Bionic has no makecontext/swapcontext and Edge
// pulls in Boost.Context for its fcontext assembly; to avoid a new assembly
// submodule and its NDK/premake build glue, this uses a minimal hand-rolled
// context switch (the same callee-saved register save/restore Boost's
// jump_fcontext performs) for the two Android ABIs this project ships
// (aarch64 and x86_64). Stacks are mmap'd with a PROT_NONE guard page at the
// floor so a stack overflow faults detectably (IsStackOverflowFault).

namespace xe {
namespace threading {

namespace {

// Saved-frame sizes for the raw context switch below.
#if defined(__aarch64__)
// x19-x28 (10), x29, x30 (2), d8-d15 (8) = 20 slots of 8 bytes = 0xA0.
constexpr size_t kContextFrameBytes = 0xA0;
#elif defined(__x86_64__)
// rbx, rbp, r12, r13, r14, r15, return address = 7 slots = 0x38.
constexpr size_t kContextFrameBytes = 0x38;
#else
#error "POSIX fibers: unsupported architecture (add a context frame)"
#endif

extern "C" {
// Saves the callee-saved state on the current stack, stores the resulting
// stack pointer to *save_sp, switches to resume_sp and restores the state
// saved there. Returns (on the original context) when something later
// switches back to it.
void xe_fiber_swap(void** save_sp, void* resume_sp);
// First-entry thunk: receives the WinFiber-equivalent pointer in a
// callee-saved register (set up by the fake initial frame) and calls the C++
// entry.
void xe_fiber_entry_thunk();
void xe_fiber_entry_c(void* fiber);
}

#if defined(__aarch64__)
__asm__(
    ".text\n"
    ".align 4\n"
    ".globl xe_fiber_swap\n"
    ".type xe_fiber_swap, %function\n"
    "xe_fiber_swap:\n"
    "  sub sp, sp, #0xA0\n"
    "  stp x19, x20, [sp, #0x00]\n"
    "  stp x21, x22, [sp, #0x10]\n"
    "  stp x23, x24, [sp, #0x20]\n"
    "  stp x25, x26, [sp, #0x30]\n"
    "  stp x27, x28, [sp, #0x40]\n"
    "  stp x29, x30, [sp, #0x50]\n"
    "  stp d8,  d9,  [sp, #0x60]\n"
    "  stp d10, d11, [sp, #0x70]\n"
    "  stp d12, d13, [sp, #0x80]\n"
    "  stp d14, d15, [sp, #0x90]\n"
    "  mov x9, sp\n"
    "  str x9, [x0]\n"
    "  mov sp, x1\n"
    "  ldp x19, x20, [sp, #0x00]\n"
    "  ldp x21, x22, [sp, #0x10]\n"
    "  ldp x23, x24, [sp, #0x20]\n"
    "  ldp x25, x26, [sp, #0x30]\n"
    "  ldp x27, x28, [sp, #0x40]\n"
    "  ldp x29, x30, [sp, #0x50]\n"
    "  ldp d8,  d9,  [sp, #0x60]\n"
    "  ldp d10, d11, [sp, #0x70]\n"
    "  ldp d12, d13, [sp, #0x80]\n"
    "  ldp d14, d15, [sp, #0x90]\n"
    "  add sp, sp, #0xA0\n"
    "  ret\n"
    ".size xe_fiber_swap, .-xe_fiber_swap\n"
    ".align 4\n"
    ".globl xe_fiber_entry_thunk\n"
    ".type xe_fiber_entry_thunk, %function\n"
    "xe_fiber_entry_thunk:\n"
    // The fake initial frame put the Fiber* in x19 and zeroed x29/x30.
    "  mov x0, x19\n"
    "  bl xe_fiber_entry_c\n"
    "  brk #0\n"  // The entry never returns.
    ".size xe_fiber_entry_thunk, .-xe_fiber_entry_thunk\n");
#elif defined(__x86_64__)
__asm__(
    ".text\n"
    ".align 16\n"
    ".globl xe_fiber_swap\n"
    ".type xe_fiber_swap, @function\n"
    "xe_fiber_swap:\n"
    // Return address is already on the stack (slot 6 of the frame).
    "  pushq %rbp\n"
    "  pushq %rbx\n"
    "  pushq %r12\n"
    "  pushq %r13\n"
    "  pushq %r14\n"
    "  pushq %r15\n"
    "  movq %rsp, (%rdi)\n"
    "  movq %rsi, %rsp\n"
    "  popq %r15\n"
    "  popq %r14\n"
    "  popq %r13\n"
    "  popq %r12\n"
    "  popq %rbx\n"
    "  popq %rbp\n"
    "  ret\n"
    ".size xe_fiber_swap, .-xe_fiber_swap\n"
    ".align 16\n"
    ".globl xe_fiber_entry_thunk\n"
    ".type xe_fiber_entry_thunk, @function\n"
    "xe_fiber_entry_thunk:\n"
    // The fake initial frame put the Fiber* in r12 (restored by the swap).
    "  movq %r12, %rdi\n"
    // Align: entry via ret leaves rsp 16-byte aligned at the thunk; the call
    // below pushes 8 making the callee see the standard alignment.
    "  callq xe_fiber_entry_c\n"
    "  ud2\n"  // The entry never returns.
    ".size xe_fiber_entry_thunk, .-xe_fiber_entry_thunk\n");
#endif

class PosixFiber;

thread_local PosixFiber* current_fiber_ = nullptr;

// Live fiber stacks for IsStackOverflowFault. Rarely mutated (fiber create /
// destroy), read from the crash handler.
std::mutex fiber_stacks_mutex_;
struct FiberStackRange {
  const uint8_t* guard_low;
  const uint8_t* guard_high;  // guard_low + page size
};
std::vector<FiberStackRange> fiber_stack_ranges_;

class PosixFiber : public Fiber {
 public:
  PosixFiber(size_t stack_size, std::function<void()> start_routine)
      : start_routine_(std::move(start_routine)) {
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    stack_size = (stack_size + page - 1) & ~(page - 1);
    alloc_size_ = stack_size + page;  // + guard page at the floor
    alloc_ = mmap(nullptr, alloc_size_, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    assert_true(alloc_ != MAP_FAILED);
    mprotect(alloc_, page, PROT_NONE);
    {
      std::lock_guard<std::mutex> lock(fiber_stacks_mutex_);
      fiber_stack_ranges_.push_back(
          {static_cast<const uint8_t*>(alloc_),
           static_cast<const uint8_t*>(alloc_) + page});
    }

    // Build the fake initial frame at the stack top so the first swap "pops"
    // it and returns into xe_fiber_entry_thunk with this fiber in a
    // callee-saved register.
    uint8_t* stack_top = static_cast<uint8_t*>(alloc_) + alloc_size_;
    stack_top = reinterpret_cast<uint8_t*>(
        reinterpret_cast<uintptr_t>(stack_top) & ~uintptr_t(15));
    uint64_t* frame = reinterpret_cast<uint64_t*>(stack_top) -
                      kContextFrameBytes / sizeof(uint64_t);
    for (size_t i = 0; i < kContextFrameBytes / sizeof(uint64_t); ++i) {
      frame[i] = 0;
    }
#if defined(__aarch64__)
    frame[0] = reinterpret_cast<uint64_t>(this);  // x19
    frame[11] =
        reinterpret_cast<uint64_t>(&xe_fiber_entry_thunk);  // x30 (lr)
#elif defined(__x86_64__)
    // Push order: retaddr (implicit, slot 6), rbp(5), rbx(4), r12(3), r13(2),
    // r14(1), r15(0).
    frame[3] = reinterpret_cast<uint64_t>(this);                     // r12
    frame[6] = reinterpret_cast<uint64_t>(&xe_fiber_entry_thunk);    // ret
#endif
    resume_sp_ = frame;
  }

  // Adoption of the calling host thread's stack.
  PosixFiber() : is_thread_fiber_(true) {}

  ~PosixFiber() override {
    if (is_thread_fiber_) {
      if (current_fiber_ == this) {
        current_fiber_ = nullptr;
      }
      return;
    }
    assert_true(current_fiber_ != this);
    if (alloc_ && alloc_ != MAP_FAILED) {
      {
        std::lock_guard<std::mutex> lock(fiber_stacks_mutex_);
        for (auto it = fiber_stack_ranges_.begin();
             it != fiber_stack_ranges_.end(); ++it) {
          if (it->guard_low == alloc_) {
            fiber_stack_ranges_.erase(it);
            break;
          }
        }
      }
      munmap(alloc_, alloc_size_);
    }
  }

  void SwitchTo() override {
    assert_false(terminated_);
    PosixFiber* previous = current_fiber_;
    assert_not_null(previous);
    current_fiber_ = this;
    xe_fiber_swap(&previous->resume_sp_, resume_sp_);
    // Control returned to |previous|'s context.
    current_fiber_ = previous;
  }

  void SetTerminated() override { terminated_ = true; }

  void RunEntry() {
    start_routine_();
    assert_always(
        "Fiber start routine returned instead of switching away after "
        "SetTerminated()");
    abort();
  }

 private:
  friend class Fiber;

  void* alloc_ = nullptr;
  size_t alloc_size_ = 0;
  // Where this fiber's context resumes: the saved frame's stack pointer.
  void* resume_sp_ = nullptr;
  std::function<void()> start_routine_;
  bool is_thread_fiber_ = false;
  bool terminated_ = false;
};

extern "C" void xe_fiber_entry_c(void* fiber) {
  static_cast<PosixFiber*>(fiber)->RunEntry();
}

}  // namespace

std::unique_ptr<Fiber> Fiber::Create(CreationParameters params,
                                     std::function<void()> start_routine) {
  return std::make_unique<PosixFiber>(params.stack_size,
                                      std::move(start_routine));
}

std::unique_ptr<Fiber> Fiber::CreateFromThread() {
  auto fiber = std::make_unique<PosixFiber>();
  current_fiber_ = fiber.get();
  return fiber;
}

Fiber* Fiber::GetCurrentFiber() { return current_fiber_; }

bool Fiber::IsStackOverflowFault(const void* address) {
  const uint8_t* addr = static_cast<const uint8_t*>(address);
  std::lock_guard<std::mutex> lock(fiber_stacks_mutex_);
  for (const FiberStackRange& range : fiber_stack_ranges_) {
    if (addr >= range.guard_low && addr < range.guard_high) {
      return true;
    }
  }
  return false;
}

}  // namespace threading
}  // namespace xe
