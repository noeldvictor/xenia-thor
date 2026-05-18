/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/arm64/arm64_assembler.h"

#include <array>
#include <atomic>
#include <string_view>
#include <unordered_map>

#include "xenia/base/logging.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/backend/arm64/arm64_backend.h"
#include "xenia/cpu/backend/arm64/arm64_function.h"
#include "xenia/cpu/backend/arm64/arm64_jit.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/hir/block.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/instr.h"
#include "xenia/cpu/hir/label.h"

namespace xe {
namespace cpu {
namespace backend {
namespace arm64 {
namespace {

std::atomic<int> g_program_log_budget{160};
std::atomic<int> g_program_log_suppression_budget{1};
std::atomic<int> g_jit_fallback_log_budget{512};

bool ConsumeLogBudget(std::atomic<int>* budget) {
  int value = budget->load();
  while (value > 0) {
    if (budget->compare_exchange_strong(value, value - 1)) {
      return true;
    }
  }
  return false;
}

void LogHirLineByLine(const char* text) {
  const char* line_start = text;
  while (*line_start) {
    const char* line_end = line_start;
    while (*line_end && *line_end != '\n') {
      ++line_end;
    }
    if (line_end != line_start) {
      XELOGI("ARM64 HIR: {}",
             std::string_view(line_start,
                              static_cast<size_t>(line_end - line_start)));
    }
    line_start = *line_end == '\n' ? line_end + 1 : line_end;
  }
}

void RememberValueType(Arm64Function::Program* program,
                       const hir::Value* value) {
  if (!value || value->IsConstant()) {
    return;
  }
  if (value->ordinal < program->value_types.size()) {
    program->value_types[value->ordinal] = value->type;
  }
}

Arm64Function::Operand CloneValueOperand(Arm64Function::Program* program,
                                         const hir::Value* value) {
  Arm64Function::Operand operand;
  if (!value) {
    return operand;
  }

  operand.type = value->type;
  if (value->IsConstant()) {
    operand.kind = Arm64Function::Operand::Kind::kConstant;
    operand.constant = value->constant;
  } else {
    operand.kind = Arm64Function::Operand::Kind::kValue;
    operand.value_ordinal = value->ordinal;
    RememberValueType(program, value);
  }
  return operand;
}

Arm64Function::Operand CloneOperand(
    Arm64Function::Program* program,
    const std::unordered_map<hir::Block*, uint32_t>& block_indices,
    hir::OpcodeSignatureType signature_type, const hir::Instr::Op& op) {
  Arm64Function::Operand operand;
  switch (signature_type) {
    case hir::OPCODE_SIG_TYPE_X:
      break;
    case hir::OPCODE_SIG_TYPE_L:
      operand.kind = Arm64Function::Operand::Kind::kBlock;
      if (op.label && op.label->block) {
        auto it = block_indices.find(op.label->block);
        if (it != block_indices.end()) {
          operand.block_index = it->second;
        }
      }
      break;
    case hir::OPCODE_SIG_TYPE_O:
      operand.kind = Arm64Function::Operand::Kind::kOffset;
      operand.offset = op.offset;
      break;
    case hir::OPCODE_SIG_TYPE_S:
      operand.kind = Arm64Function::Operand::Kind::kSymbol;
      operand.symbol = op.symbol;
      break;
    case hir::OPCODE_SIG_TYPE_V:
      operand = CloneValueOperand(program, op.value);
      break;
  }
  return operand;
}

std::unique_ptr<Arm64Function::Program> BuildInterpreterProgram(
    hir::HIRBuilder* builder) {
  auto program = std::make_unique<Arm64Function::Program>();
  program->max_value_ordinal = builder->max_value_ordinal();
  program->value_types.resize(program->max_value_ordinal, hir::INT64_TYPE);

  std::unordered_map<const hir::Value*, uint32_t> local_indices;
  for (auto local : builder->locals()) {
    local_indices[local] = static_cast<uint32_t>(local_indices.size());
    RememberValueType(program.get(), local);
  }
  program->local_count = static_cast<uint32_t>(local_indices.size());

  std::unordered_map<hir::Block*, uint32_t> block_indices;
  uint32_t block_index = 0;
  for (auto block = builder->first_block(); block; block = block->next) {
    block_indices[block] = block_index++;
  }
  program->blocks.resize(block_index);

  uint32_t current_source_offset = 0;
  for (auto block = builder->first_block(); block; block = block->next) {
    auto& program_block = program->blocks[block_indices[block]];
    program_block.instruction_start =
        static_cast<uint32_t>(program->instructions.size());

    for (auto hir_instr = block->instr_head; hir_instr;
         hir_instr = hir_instr->next) {
      auto& instruction = program->instructions.emplace_back();
      instruction.opcode = hir_instr->opcode->num;
      instruction.flags = hir_instr->flags;
      instruction.ordinal = hir_instr->ordinal;
      instruction.source_offset = current_source_offset;
      instruction.block_index = block_indices[block];

      const auto* info = hir_instr->opcode;
      auto dest_type =
          static_cast<hir::OpcodeSignatureType>(info->signature & 0x7);
      auto src1_type =
          static_cast<hir::OpcodeSignatureType>((info->signature >> 3) & 0x7);
      auto src2_type =
          static_cast<hir::OpcodeSignatureType>((info->signature >> 6) & 0x7);
      auto src3_type =
          static_cast<hir::OpcodeSignatureType>((info->signature >> 9) & 0x7);

      if (dest_type == hir::OPCODE_SIG_TYPE_V && hir_instr->dest) {
        instruction.dest_ordinal = hir_instr->dest->ordinal;
        instruction.dest_type = hir_instr->dest->type;
        RememberValueType(program.get(), hir_instr->dest);
      }

      instruction.src1 =
          CloneOperand(program.get(), block_indices, src1_type,
                       hir_instr->src1);
      instruction.src2 =
          CloneOperand(program.get(), block_indices, src2_type,
                       hir_instr->src2);
      instruction.src3 =
          CloneOperand(program.get(), block_indices, src3_type,
                       hir_instr->src3);

      if ((instruction.opcode == hir::OPCODE_LOAD_LOCAL ||
           instruction.opcode == hir::OPCODE_STORE_LOCAL) &&
          hir_instr->src1.value) {
        auto local_it = local_indices.find(hir_instr->src1.value);
        if (local_it != local_indices.end()) {
          instruction.src1.kind = Arm64Function::Operand::Kind::kOffset;
          instruction.src1.offset = local_it->second;
        }
      }

      if (instruction.opcode == hir::OPCODE_SOURCE_OFFSET) {
        current_source_offset = static_cast<uint32_t>(hir_instr->src1.offset);
        instruction.source_offset = current_source_offset;
      }
    }

    program_block.instruction_end =
        static_cast<uint32_t>(program->instructions.size());
  }

  return program;
}

void LogInterpreterProgramDetails(GuestFunction* function,
                                  hir::HIRBuilder* builder) {
  bool force_log =
      function && (function->address() == 0x824669E0 ||
                   function->address() == 0x826BFC78 ||
                   function->address() == 0x826BFD0C);
  bool should_log = force_log;
  int log_budget = g_program_log_budget.load();
  while (!should_log && log_budget > 0) {
    if (g_program_log_budget.compare_exchange_strong(log_budget,
                                                     log_budget - 1)) {
      should_log = true;
    }
  }
  if (!should_log) {
    int suppression_budget = g_program_log_suppression_budget.load();
    if (suppression_budget > 0 &&
        g_program_log_suppression_budget.compare_exchange_strong(
            suppression_budget, suppression_budget - 1)) {
      XELOGI("ARM64 HIR interpreter program logging suppressed after budget");
    }
    return;
  }

  std::array<uint32_t, hir::__OPCODE_MAX_VALUE> opcode_counts = {};
  uint32_t block_count = 0;
  uint32_t instr_count = 0;

  for (auto block = builder->first_block(); block; block = block->next) {
    ++block_count;
    for (auto instr = block->instr_head; instr; instr = instr->next) {
      ++instr_count;
      ++opcode_counts[instr->opcode->num];
    }
  }

  XELOGI(
      "ARM64 HIR interpreter program for guest function {:08X}-{:08X}: {} "
      "blocks, {} HIR instructions",
      function ? function->address() : 0,
      function ? function->end_address() : 0, block_count, instr_count);

  for (uint32_t i = 0; i < opcode_counts.size(); ++i) {
    if (!opcode_counts[i]) {
      continue;
    }
    XELOGI("ARM64 HIR opcode {:03}: {}", i, opcode_counts[i]);
  }

  if (force_log) {
    StringBuffer hir_dump;
    builder->Dump(&hir_dump);
    XELOGI("ARM64 optimized HIR dump for guest function {:08X} begins",
           function->address());
    LogHirLineByLine(hir_dump.buffer());
    XELOGI("ARM64 optimized HIR dump for guest function {:08X} ends",
           function->address());
  }
}

}  // namespace

Arm64Assembler::Arm64Assembler(Arm64Backend* backend)
    : Assembler(backend), arm64_backend_(backend) {}

Arm64Assembler::~Arm64Assembler() = default;

bool Arm64Assembler::Initialize() {
  if (!Assembler::Initialize()) {
    return false;
  }
  return arm64_backend_ != nullptr;
}

bool Arm64Assembler::Assemble(GuestFunction* function,
                              hir::HIRBuilder* builder,
                              uint32_t debug_info_flags,
                              std::unique_ptr<FunctionDebugInfo> debug_info) {
  (void)debug_info_flags;

  if (!function || !builder) {
    return false;
  }

  auto program = BuildInterpreterProgram(builder);
  LogInterpreterProgramDetails(function, builder);

  function->set_debug_info(std::move(debug_info));
  auto arm64_function = static_cast<Arm64Function*>(function);
  std::string jit_reject_reason;
  const bool jit_compiled = TryCompileArm64Program(
      arm64_backend_, arm64_function, *program, &jit_reject_reason);
  arm64_function->set_jit_reject_reason(jit_reject_reason);
  if (!jit_compiled && !jit_reject_reason.empty() &&
      ConsumeLogBudget(&g_jit_fallback_log_budget)) {
    XELOGI("ARM64 JIT fallback for guest {:08X}: {}", function->address(),
           jit_reject_reason);
  }
  arm64_function->SetupProgram(std::move(program));
  return true;
}

}  // namespace arm64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
