/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// Differential tests for the LLVM-JIT CPU backend: each guest function HIR is
// run on BOTH the a64 per-block backend and the LLVM backend, and the resulting
// PPCContext GPRs must match. This validates the HIR->LLVM lowering (and the
// reserved-register x20/x21 ABI) against the known-good a64 backend, device-free
// under qemu-aarch64. ARM64-only (the LLVM backend is ARM64); a no-op elsewhere.

#include "xenia/base/platform.h"

#if XE_ARCH_ARM64

#include "xenia/base/cvar.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/llvm/llvm_backend.h"
#include "xenia/cpu/testing/util.h"

DECLARE_int32(cpu_backend_llvm_opt);

using namespace xe;                // xe::Memory
using namespace xe::cpu;
using namespace xe::cpu::backend;  // a64::, llvm_backend::
using namespace xe::cpu::hir;
using namespace xe::cpu::testing;
using xe::cpu::ppc::PPCContext;

namespace {

enum class Be { kA64, kLLVM };

// Runs `generator` as guest fn 0x80000000 on the chosen backend, applies
// pre_call, calls it, and captures the resulting GPRs.
static void Capture(Be be, const std::function<void(HIRBuilder&)>& generator,
                    const std::function<void(PPCContext*)>& pre_call,
                    uint64_t out_r[32]) {
  const uint32_t memory_size = 16 * 1024 * 1024;
  auto memory = std::make_unique<Memory>();
  memory->Initialize();

  std::unique_ptr<backend::Backend> backend;
  if (be == Be::kA64) {
    backend.reset(new a64::A64Backend());
  } else {
    backend.reset(new llvm_backend::LLVMBackend());
  }
  auto processor = std::make_unique<Processor>(memory.get(), nullptr);
  processor->Setup(std::move(backend));

  auto module = std::make_unique<TestModule>(
      processor.get(), "Test",
      [](uint64_t address) { return address == 0x80000000; },
      [&generator](HIRBuilder& b) {
        generator(b);
        return true;
      });
  processor->AddModule(std::move(module));
  processor->backend()->CommitExecutableRange(0x80000000, 0x80010000);

  auto fn = processor->ResolveFunction(0x80000000);
  uint32_t stack_size = 64 * 1024;
  uint32_t stack_address = memory_size - stack_size;
  uint32_t thread_state_address = stack_address - 0x1000;
  auto thread_state = std::make_unique<ThreadState>(
      processor.get(), 0x100, stack_address, thread_state_address);
  auto ctx = thread_state->context();
  ctx->lr = 0xBCBCBCBC;
  pre_call(ctx);
  fn->Call(thread_state.get(), uint32_t(ctx->lr));
  for (int i = 0; i < 32; ++i) {
    out_r[i] = ctx->r[i];
  }
}

// Asserts the LLVM backend produces GPRs identical to the a64 backend.
static void RunDiff(const std::function<void(HIRBuilder&)>& generator,
                    const std::function<void(PPCContext*)>& pre_call) {
  if (!llvm_backend::LLVMBackend::IsAvailable()) {
    WARN("LLVM backend not linked (stub build) - skipping differential test");
    return;
  }
  // O0: skip the (qemu-slow) middle-end passes; correctness needs only the
  // direct-ctx lowering. The residency win (O2) is validated on-device.
  cvars::cpu_backend_llvm_opt = 0;
  uint64_t ra[32] = {0}, rl[32] = {0};
  Capture(Be::kA64, generator, pre_call, ra);
  Capture(Be::kLLVM, generator, pre_call, rl);
  for (int i = 0; i < 32; ++i) {
    INFO("GPR r" << i);
    REQUIRE(rl[i] == ra[i]);
  }
}

}  // namespace

TEST_CASE("LLVM_ADD_SUB_I64", "[llvm]") {
  RunDiff(
      [](HIRBuilder& b) {
        StoreGPR(b, 3, b.Add(LoadGPR(b, 4), LoadGPR(b, 5)));
        StoreGPR(b, 6, b.Sub(LoadGPR(b, 4), LoadGPR(b, 5)));
        b.Return();
      },
      [](PPCContext* ctx) {
        ctx->r[4] = 0x1122334455667788ull;
        ctx->r[5] = 0x0102030405060708ull;
      });
}

TEST_CASE("LLVM_BITWISE_I64", "[llvm]") {
  RunDiff(
      [](HIRBuilder& b) {
        StoreGPR(b, 3, b.And(LoadGPR(b, 4), LoadGPR(b, 5)));
        StoreGPR(b, 6, b.Or(LoadGPR(b, 4), LoadGPR(b, 5)));
        StoreGPR(b, 7, b.Xor(LoadGPR(b, 4), LoadGPR(b, 5)));
        StoreGPR(b, 8, b.Not(LoadGPR(b, 4)));
        b.Return();
      },
      [](PPCContext* ctx) {
        ctx->r[4] = 0xF0F0F0F00F0F0F0Full;
        ctx->r[5] = 0xFFFF0000FFFF0000ull;
      });
}

TEST_CASE("LLVM_SHIFTS_I64", "[llvm]") {
  RunDiff(
      [](HIRBuilder& b) {
        StoreGPR(b, 3, b.Shl(LoadGPR(b, 4), LoadGPR(b, 5)));
        StoreGPR(b, 6, b.Shr(LoadGPR(b, 4), LoadGPR(b, 5)));
        StoreGPR(b, 7, b.Sha(LoadGPR(b, 4), LoadGPR(b, 5)));
        b.Return();
      },
      [](PPCContext* ctx) {
        ctx->r[4] = 0x8000000000000003ull;
        ctx->r[5] = 4;
      });
}

TEST_CASE("LLVM_MUL_CNTLZ_BSWAP_I64", "[llvm]") {
  RunDiff(
      [](HIRBuilder& b) {
        StoreGPR(b, 3, b.Mul(LoadGPR(b, 4), LoadGPR(b, 5)));
        StoreGPR(b, 6, b.CountLeadingZeros(LoadGPR(b, 4)));
        StoreGPR(b, 7, b.ByteSwap(LoadGPR(b, 4)));
        b.Return();
      },
      [](PPCContext* ctx) {
        ctx->r[4] = 0x0000000012345678ull;
        ctx->r[5] = 0x100;
      });
}

TEST_CASE("LLVM_COMPARE_SELECT_I64", "[llvm]") {
  RunDiff(
      [](HIRBuilder& b) {
        auto lt = b.CompareSLT(LoadGPR(b, 4), LoadGPR(b, 5));
        StoreGPR(b, 3, b.Select(lt, LoadGPR(b, 6), LoadGPR(b, 7)));
        auto eq = b.CompareEQ(LoadGPR(b, 4), LoadGPR(b, 5));
        StoreGPR(b, 8, b.Select(eq, LoadGPR(b, 6), LoadGPR(b, 7)));
        b.Return();
      },
      [](PPCContext* ctx) {
        ctx->r[4] = 5;
        ctx->r[5] = 9;
        ctx->r[6] = 0xAAAAAAAAAAAAAAAAull;
        ctx->r[7] = 0x5555555555555555ull;
      });
}

TEST_CASE("LLVM_BRANCH_I64", "[llvm]") {
  RunDiff(
      [](HIRBuilder& b) {
        auto label_else = b.NewLabel();
        b.BranchFalse(b.CompareSLT(LoadGPR(b, 4), LoadGPR(b, 5)), label_else);
        StoreGPR(b, 3, LoadGPR(b, 6));
        b.Return();
        b.MarkLabel(label_else);
        StoreGPR(b, 3, LoadGPR(b, 7));
        b.Return();
      },
      [](PPCContext* ctx) {
        ctx->r[4] = 2;
        ctx->r[5] = 8;
        ctx->r[6] = 0x1111111111111111ull;
        ctx->r[7] = 0x2222222222222222ull;
      });
}

#endif  // XE_ARCH_ARM64
