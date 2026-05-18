/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/ppc/ppc_translator.h"

#include <charconv>
#include <cctype>
#include <limits>
#include <string_view>
#include <system_error>

#include "xenia/base/assert.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/reset_scope.h"
#include "xenia/base/string.h"
#include "xenia/cpu/compiler/compiler_passes.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/ppc/ppc_frontend.h"
#include "xenia/cpu/ppc/ppc_hir_builder.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"
#include "xenia/cpu/ppc/ppc_scanner.h"
#include "xenia/cpu/processor.h"

namespace xe {
namespace cpu {
namespace ppc {

using xe::cpu::backend::Backend;
using xe::cpu::compiler::Compiler;
namespace passes = xe::cpu::compiler::passes;

namespace {

std::string_view Trim(std::string_view value) {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1);
  }
  return value;
}

bool ParseAddress(std::string_view value, uint32_t* out_address) {
  value = Trim(value);
  if (value.empty()) {
    return false;
  }

  int base = 10;
  if (value.size() > 2 && value[0] == '0' &&
      (value[1] == 'x' || value[1] == 'X')) {
    value.remove_prefix(2);
    base = 16;
  } else {
    if (value.size() >= 8) {
      base = 16;
    }
    for (char c : value) {
      if ((c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')) {
        base = 16;
        break;
      }
    }
  }

  uint64_t parsed = 0;
  auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed, base);
  if (result.ec != std::errc() || result.ptr != value.data() + value.size() ||
      parsed > std::numeric_limits<uint32_t>::max()) {
    return false;
  }
  *out_address = static_cast<uint32_t>(parsed);
  return true;
}

bool FunctionMatchesFilter(GuestFunction* function, std::string_view list) {
  if (!function || list.empty()) {
    return false;
  }

  uint32_t function_start = function->address();
  uint32_t function_end = function->end_address();
  if (function_end < function_start) {
    function_end = function_start;
  }

  size_t token_start = 0;
  while (token_start < list.size()) {
    while (token_start < list.size() &&
           (std::isspace(static_cast<unsigned char>(list[token_start])) ||
            list[token_start] == ',' || list[token_start] == ';')) {
      ++token_start;
    }
    if (token_start >= list.size()) {
      break;
    }

    size_t token_end = token_start;
    while (token_end < list.size() && list[token_end] != ',' &&
           list[token_end] != ';' &&
           !std::isspace(static_cast<unsigned char>(list[token_end]))) {
      ++token_end;
    }

    std::string_view token =
        Trim(list.substr(token_start, token_end - token_start));
    size_t range_separator = token.find('-');
    uint32_t start = 0;
    uint32_t end = 0;
    if (range_separator != std::string_view::npos) {
      if (ParseAddress(token.substr(0, range_separator), &start) &&
          ParseAddress(token.substr(range_separator + 1), &end)) {
        if (start > end) {
          std::swap(start, end);
        }
        if (start <= function_end && end >= function_start) {
          return true;
        }
      }
    } else if (ParseAddress(token, &start) && start >= function_start &&
               start <= function_end) {
      return true;
    }

    token_start = token_end;
  }

  return false;
}

void LogTextBlockLineByLine(uint32_t function_address, const char* label,
                            const char* text) {
  if (!text || !*text) {
    return;
  }

  XELOGI("Filtered function dump {:08X} {} begins", function_address, label);
  const char* line_start = text;
  while (*line_start) {
    const char* line_end = line_start;
    while (*line_end && *line_end != '\n') {
      ++line_end;
    }
    if (line_end != line_start) {
      XELOGI("Filtered function dump {:08X} {}: {}", function_address, label,
             std::string_view(line_start,
                              static_cast<size_t>(line_end - line_start)));
    }
    line_start = *line_end == '\n' ? line_end + 1 : line_end;
  }
  XELOGI("Filtered function dump {:08X} {} ends", function_address, label);
}

void LogFilteredFunctionDebugInfo(GuestFunction* function,
                                  FunctionDebugInfo* debug_info) {
  if (!function || !debug_info) {
    return;
  }

  const uint32_t address = function->address();
  XELOGI("Filtered function dump {:08X}-{:08X} {}", address,
         function->end_address(), function->name());
  LogTextBlockLineByLine(address, "PPC", debug_info->source_disasm());
  LogTextBlockLineByLine(address, "RawHIR", debug_info->raw_hir_disasm());
  LogTextBlockLineByLine(address, "OptHIR", debug_info->hir_disasm());
  LogTextBlockLineByLine(address, "MachineCode",
                         debug_info->machine_code_disasm());
}

}  // namespace

PPCTranslator::PPCTranslator(PPCFrontend* frontend) : frontend_(frontend) {
  Backend* backend = frontend->processor()->backend();

  scanner_.reset(new PPCScanner(frontend));
  builder_.reset(new PPCHIRBuilder(frontend));
  compiler_.reset(new Compiler(frontend->processor()));
  assembler_ = backend->CreateAssembler();
  assembler_->Initialize();

  bool validate = cvars::validate_hir;

  // Merge blocks early. This will let us use more context in other passes.
  // The CFG is required for simplification and dirtied by it.
  compiler_->AddPass(std::make_unique<passes::ControlFlowAnalysisPass>());
  compiler_->AddPass(std::make_unique<passes::ControlFlowSimplificationPass>());

  // Passes are executed in the order they are added. Multiple of the same
  // pass type may be used.
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());
  compiler_->AddPass(std::make_unique<passes::ContextPromotionPass>());
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());

  // Grouped simplification + constant propagation.
  // Loops until no changes are made.
  auto sap = std::make_unique<passes::ConditionalGroupPass>();
  sap->AddPass(std::make_unique<passes::SimplificationPass>());
  if (validate) sap->AddPass(std::make_unique<passes::ValidationPass>());
  sap->AddPass(std::make_unique<passes::ConstantPropagationPass>());
  if (validate) sap->AddPass(std::make_unique<passes::ValidationPass>());
  compiler_->AddPass(std::move(sap));

  if (backend->machine_info()->supports_extended_load_store) {
    // Backend supports the advanced LOAD/STORE instructions.
    // These will save us a lot of HIR opcodes.
    compiler_->AddPass(
        std::make_unique<passes::MemorySequenceCombinationPass>());
    if (validate)
      compiler_->AddPass(std::make_unique<passes::ValidationPass>());
  }
  compiler_->AddPass(std::make_unique<passes::SimplificationPass>());
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());
  // compiler_->AddPass(std::make_unique<passes::DeadStoreEliminationPass>());
  // if (validate)
  // compiler_->AddPass(std::make_unique<passes::ValidationPass>());
  compiler_->AddPass(std::make_unique<passes::DeadCodeEliminationPass>());
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());

  //// Removes all unneeded variables. Try not to add new ones after this.
  // compiler_->AddPass(new passes::ValueReductionPass());
  // if (validate) compiler_->AddPass(new passes::ValidationPass());

  // Register allocation for the target backend.
  // Will modify the HIR to add loads/stores.
  // This should be the last pass before finalization, as after this all
  // registers are assigned and ready to be emitted.
  compiler_->AddPass(std::make_unique<passes::RegisterAllocationPass>(
      backend->machine_info()));
  if (validate) compiler_->AddPass(std::make_unique<passes::ValidationPass>());

  // Must come last. The HIR is not really HIR after this.
  compiler_->AddPass(std::make_unique<passes::FinalizationPass>());
}

PPCTranslator::~PPCTranslator() = default;

bool PPCTranslator::Translate(GuestFunction* function,
                              uint32_t debug_info_flags) {
  SCOPE_profile_cpu_f("cpu");

  // Reset() all caching when we leave.
  xe::make_reset_scope(builder_);
  xe::make_reset_scope(compiler_);
  xe::make_reset_scope(assembler_);
  xe::make_reset_scope(&string_buffer_);

  // NOTE: we only want to do this when required, as it's expensive to build.
  bool filtered_disassembly =
      FunctionMatchesFilter(function, cvars::disassemble_function_filter);
  if (cvars::disassemble_functions || filtered_disassembly) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoAllDisasm;
  }
  if (cvars::trace_functions) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctions;
  }
  if (cvars::trace_function_coverage) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctionCoverage;
  }
  if (cvars::trace_function_references) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctionReferences;
  }
  if (cvars::trace_function_data) {
    debug_info_flags |= DebugInfoFlags::kDebugInfoTraceFunctionData;
  }
  std::unique_ptr<FunctionDebugInfo> debug_info;
  if (debug_info_flags) {
    debug_info.reset(new FunctionDebugInfo());
  }

  // Scan the function to find its extents and gather debug data.
  if (!scanner_->Scan(function, debug_info.get())) {
    return false;
  }

  // Setup trace data, if needed.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoTraceFunctions) {
    // Base trace data.
    size_t trace_data_size = FunctionTraceData::SizeOfHeader();
    if (debug_info_flags & DebugInfoFlags::kDebugInfoTraceFunctionCoverage) {
      // Additional space for instruction coverage counts.
      trace_data_size += FunctionTraceData::SizeOfInstructionCounts(
          function->address(), function->end_address());
    }
    uint8_t* trace_data =
        frontend_->processor()->AllocateFunctionTraceData(trace_data_size);
    if (trace_data) {
      function->trace_data().Reset(trace_data, trace_data_size,
                                   function->address(),
                                   function->end_address());
    } else {
      debug_info_flags &= ~(DebugInfoFlags::kDebugInfoTraceFunctions |
                            DebugInfoFlags::kDebugInfoTraceFunctionCoverage);
    }
  }

  // Stash source.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmSource) {
    DumpSource(function, &string_buffer_);
    debug_info->set_source_disasm(xe_strdup(string_buffer_.buffer()));
    string_buffer_.Reset();
  }

  // Emit function.
  uint32_t emit_flags = 0;
  if (debug_info) {
    emit_flags |= PPCHIRBuilder::EMIT_DEBUG_COMMENTS;
  }
  if (!builder_->Emit(function, emit_flags)) {
    return false;
  }

  // Stash raw HIR.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmRawHir) {
    builder_->Dump(&string_buffer_);
    debug_info->set_raw_hir_disasm(xe_strdup(string_buffer_.buffer()));
    string_buffer_.Reset();
  }

  // Compile/optimize/etc.
  if (!compiler_->Compile(builder_.get())) {
    return false;
  }

  // Stash optimized HIR.
  if (debug_info_flags & DebugInfoFlags::kDebugInfoDisasmHir) {
    builder_->Dump(&string_buffer_);
    debug_info->set_hir_disasm(xe_strdup(string_buffer_.buffer()));
    string_buffer_.Reset();
  }

  if (filtered_disassembly) {
    LogFilteredFunctionDebugInfo(function, debug_info.get());
  }

  // Assemble to backend machine code.
  if (!assembler_->Assemble(function, builder_.get(), debug_info_flags,
                            std::move(debug_info))) {
    return false;
  }

  return true;
}

void PPCTranslator::DumpSource(GuestFunction* function,
                               StringBuffer* string_buffer) {
  Memory* memory = frontend_->memory();

  string_buffer->AppendFormat(
      "{} fn {:08X}-{:08X} {}\n", function->module()->name().c_str(),
      function->address(), function->end_address(), function->name().c_str());

  auto blocks = scanner_->FindBlocks(function);

  uint32_t start_address = function->address();
  uint32_t end_address = function->end_address();
  auto block_it = blocks.begin();
  for (uint32_t address = start_address, offset = 0; address <= end_address;
       address += 4, offset++) {
    uint32_t code =
        xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));

    // Check labels.
    if (block_it != blocks.end() && block_it->start_address == address) {
      string_buffer->AppendFormat("{:08X}          loc_{:08X}:\n", address,
                                  address);
      ++block_it;
    }

    string_buffer->AppendFormat("{:08X} {:08X}   ", address, code);
    DisasmPPC(address, code, string_buffer);
    string_buffer->Append('\n');
  }
}

}  // namespace ppc
}  // namespace cpu
}  // namespace xe
