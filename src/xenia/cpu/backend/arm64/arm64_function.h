/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_BACKEND_ARM64_ARM64_FUNCTION_H_
#define XENIA_CPU_BACKEND_ARM64_ARM64_FUNCTION_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/hir/opcodes.h"
#include "xenia/cpu/hir/value.h"
#include "xenia/cpu/thread_state.h"

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {

class Arm64Function : public GuestFunction {
 public:
  using CompiledProgram = bool (*)(ppc::PPCContext* context,
                                   ThreadState* thread_state,
                                   uint32_t return_address);

  struct Operand {
    enum class Kind {
      kNone,
      kValue,
      kConstant,
      kOffset,
      kSymbol,
      kBlock,
    };

    Kind kind = Kind::kNone;
    hir::TypeName type = hir::INT64_TYPE;
    hir::Value::ConstantValue constant = {};
    uint32_t value_ordinal = 0;
    uint32_t block_index = 0;
    uint64_t offset = 0;
    Function* symbol = nullptr;
  };

  struct Instruction {
    hir::Opcode opcode = hir::OPCODE_NOP;
    uint16_t flags = 0;
    uint32_t ordinal = 0;
    uint32_t source_offset = 0;
    uint32_t block_index = 0;
    uint32_t dest_ordinal = UINT32_MAX;
    hir::TypeName dest_type = hir::INT64_TYPE;
    Operand src1;
    Operand src2;
    Operand src3;
  };

  struct Block {
    uint32_t instruction_start = 0;
    uint32_t instruction_end = 0;
  };

  struct Program {
    uint32_t max_value_ordinal = 0;
    uint32_t local_count = 0;
    std::vector<hir::TypeName> value_types;
    std::vector<hir::TypeName> local_types;
    std::vector<Block> blocks;
    std::vector<Instruction> instructions;
  };

  Arm64Function(Module* module, uint32_t address);
  ~Arm64Function() override;

  uint8_t* machine_code() const override { return machine_code_; }
  size_t machine_code_length() const override { return machine_code_length_; }

  void Setup(uint8_t* machine_code, size_t machine_code_length);
  void SetupProgram(std::unique_ptr<Program> program);
  void SetupCompiledProgram(CompiledProgram compiled_program,
                            size_t machine_code_length);
  void set_jit_reject_reason(std::string reason) {
    jit_reject_reason_ = std::move(reason);
  }
  const std::string& jit_reject_reason() const { return jit_reject_reason_; }

 protected:
  bool CallImpl(ThreadState* thread_state, uint32_t return_address) override;

 private:
  bool ExecuteProgram(ThreadState* thread_state, uint32_t return_address);

  uint8_t* machine_code_ = nullptr;
  size_t machine_code_length_ = 0;
  CompiledProgram compiled_program_ = nullptr;
  std::atomic<uint64_t> compiled_call_count_{0};
  std::unique_ptr<Program> program_;
  std::string jit_reject_reason_;
};

void LogArm64GuestStoreWatch(ThreadState* thread_state,
                             uint32_t function_address,
                             uint32_t source_offset, uint64_t address,
                             hir::TypeName type, uint64_t value);
void LogArm64GuestMemoryRangeWatch(ThreadState* thread_state,
                                   uint32_t function_address,
                                   uint32_t source_offset, uint64_t address,
                                   uint64_t size, const char* operation,
                                   uint64_t value);

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_BACKEND_ARM64_ARM64_FUNCTION_H_
