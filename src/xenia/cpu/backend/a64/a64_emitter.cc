/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/backend/a64/a64_emitter.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <cstring>
#include <limits>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/clock.h"
#include "xenia/base/debugging.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/cpu/backend/a64/a64_backend.h"
#include "xenia/cpu/backend/a64/a64_code_cache.h"
#include "xenia/cpu/backend/a64/a64_function.h"
#include "xenia/cpu/backend/a64/a64_sequences.h"
#include "xenia/cpu/backend/a64/a64_stack_layout.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/function.h"
#include "xenia/cpu/hir/hir_builder.h"
#include "xenia/cpu/hir/label.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"

DECLARE_uint32(a64_max_stackpoints);
DECLARE_bool(a64_enable_host_guest_stack_synchronization);
DECLARE_uint32(arm64_compiled_call_trace_interval);
DECLARE_uint32(arm64_compiled_call_trace_min_count);
DECLARE_uint32(arm64_compiled_call_trace_budget);
DECLARE_string(arm64_compiled_call_trace_functions);
DECLARE_string(arm64_compiled_call_trace_guest_tids);
DECLARE_uint32(arm64_compiled_call_trace_after_ms);
DECLARE_bool(arm64_blue_dragon_draw_wait_probe);
DECLARE_bool(arm64_blue_dragon_draw_wait_fastpath);
DECLARE_bool(arm64_blue_dragon_draw_wait_fastpath_host_counter_time);
DECLARE_uint32(arm64_blue_dragon_draw_wait_probe_stride);
DECLARE_uint32(arm64_blue_dragon_draw_wait_inline_tick_step);
DEFINE_bool(a64_inline_gprlr_helpers, true,
            "Inline PPC __savegprlr_* / __restgprlr_* ABI helpers in the "
            "A64 backend.",
            "a64");
DEFINE_bool(
    a64_inline_ppc_thread_field_leaf_helpers, true,
    "Inline tiny PPC leaf helpers matching "
    "`lwz r11,D(r13); lwz r3,D(r11); blr` in the A64 backend.",
    "a64");

namespace {
std::atomic<int> g_a64_call_trace_budget{0};
std::atomic<uint32_t> g_a64_call_trace_configured_budget{
    std::numeric_limits<uint32_t>::max()};
std::atomic<uint64_t> g_a64_call_trace_first_host_ms{0};
std::mutex g_a64_call_trace_counts_mutex;
std::unordered_map<uint64_t, uint64_t> g_a64_call_trace_counts;

std::string_view TrimTraceToken(std::string_view value) {
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

bool ParseTraceNumber(std::string_view value, uint32_t* out_value) {
  value = TrimTraceToken(value);
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

  uint32_t parsed = 0;
  auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed, base);
  if (result.ec != std::errc() || result.ptr != value.data() + value.size()) {
    return false;
  }

  *out_value = parsed;
  return true;
}

bool TraceFilterMatches(uint32_t value, std::string_view filter) {
  filter = TrimTraceToken(filter);
  if (filter.empty()) {
    return true;
  }

  size_t start = 0;
  while (start < filter.size()) {
    size_t end = filter.find_first_of(",; ", start);
    if (end == std::string_view::npos) {
      end = filter.size();
    }
    std::string_view token = TrimTraceToken(filter.substr(start, end - start));
    if (!token.empty()) {
      size_t dash = token.find('-');
      uint32_t range_start = 0;
      uint32_t range_end = 0;
      if (dash != std::string_view::npos &&
          ParseTraceNumber(token.substr(0, dash), &range_start) &&
          ParseTraceNumber(token.substr(dash + 1), &range_end)) {
        if (range_start > range_end) {
          std::swap(range_start, range_end);
        }
        if (range_start <= value && value <= range_end) {
          return true;
        }
      } else if (ParseTraceNumber(token, &range_start) &&
                 range_start == value) {
        return true;
      }
    }
    start = end + 1;
  }

  return false;
}

void ConfigureA64CallTraceBudget() {
  uint32_t budget = cvars::arm64_compiled_call_trace_budget;
  uint32_t configured_budget =
      g_a64_call_trace_configured_budget.load(std::memory_order_relaxed);
  if (configured_budget == budget) {
    return;
  }

  if (g_a64_call_trace_configured_budget.compare_exchange_strong(
          configured_budget, budget, std::memory_order_acq_rel)) {
    int clamped_budget =
        budget > static_cast<uint32_t>(std::numeric_limits<int>::max())
            ? std::numeric_limits<int>::max()
            : static_cast<int>(budget);
    g_a64_call_trace_budget.store(clamped_budget, std::memory_order_release);
  }
}

bool ConsumeA64CallTraceBudget() {
  int value = g_a64_call_trace_budget.load(std::memory_order_relaxed);
  while (value > 0) {
    if (g_a64_call_trace_budget.compare_exchange_strong(
            value, value - 1, std::memory_order_acq_rel)) {
      return true;
    }
  }
  return false;
}

bool A64CallTraceRequested() {
  return cvars::arm64_compiled_call_trace_interval != 0 &&
         cvars::arm64_compiled_call_trace_budget != 0;
}

std::string DescribeTraceFunction(xe::cpu::Processor* processor,
                                  uint32_t address) {
  if (!processor || !address) {
    return {};
  }

  xe::cpu::Function* function = processor->QueryFunction(address);
  if (!function) {
    auto functions = processor->FindFunctionsWithAddress(address);
    if (!functions.empty()) {
      function = functions.front();
    }
  }
  if (!function) {
    return {};
  }

  std::string description = function->name();
  if (description.empty()) {
    description = fmt::format("sub_{:08X}", function->address());
  }
  if (address != function->address()) {
    description += fmt::format("+{:X}", address - function->address());
  }
  return description;
}

void TraceFunctionEntry(void* raw_context, uint64_t function_address) {
  auto ctx = reinterpret_cast<xe::cpu::ppc::PPCContext*>(raw_context);
  if (!ctx || !A64CallTraceRequested()) {
    return;
  }

  uint32_t function_u32 = static_cast<uint32_t>(function_address);
  if (!TraceFilterMatches(
          ctx->thread_id, cvars::arm64_compiled_call_trace_guest_tids) ||
      !TraceFilterMatches(
          function_u32, cvars::arm64_compiled_call_trace_functions)) {
    return;
  }

  uint64_t now_ms = xe::Clock::QueryHostUptimeMillis();
  uint64_t first_ms =
      g_a64_call_trace_first_host_ms.load(std::memory_order_relaxed);
  if (!first_ms &&
      g_a64_call_trace_first_host_ms.compare_exchange_strong(
          first_ms, now_ms, std::memory_order_acq_rel)) {
    first_ms = now_ms;
  }
  uint32_t after_ms = cvars::arm64_compiled_call_trace_after_ms;
  if (after_ms && now_ms - first_ms < after_ms) {
    return;
  }

  uint64_t count = 0;
  {
    std::lock_guard<std::mutex> lock(g_a64_call_trace_counts_mutex);
    count = ++g_a64_call_trace_counts[function_u32];
  }

  uint32_t min_count = cvars::arm64_compiled_call_trace_min_count;
  if (min_count && count < min_count) {
    return;
  }

  uint32_t interval = cvars::arm64_compiled_call_trace_interval;
  if (interval > 1 && count % interval != 0 &&
      cvars::arm64_compiled_call_trace_functions.empty()) {
    return;
  }

  ConfigureA64CallTraceBudget();
  if (!ConsumeA64CallTraceBudget()) {
    return;
  }

  uint32_t lr = static_cast<uint32_t>(ctx->lr);
  uint32_t ctr = static_cast<uint32_t>(ctx->ctr);
  std::string fn_name = DescribeTraceFunction(ctx->processor, function_u32);
  std::string lr_name = DescribeTraceFunction(ctx->processor, lr);
  std::string ctr_name = DescribeTraceFunction(ctx->processor, ctr);

  XELOGI(
      "A64 call trace thid {:08X} fn {:08X} '{}' count {} lr {:08X} '{}' "
      "ctr {:08X} '{}' r1 {:08X} r3 {:08X} r10 {:08X} r11 {:08X} r13 {:08X} "
      "r29 {:08X} r30 {:08X} r31 {:08X}",
      ctx->thread_id, function_u32, fn_name, count, lr, lr_name, ctr, ctr_name,
      static_cast<uint32_t>(ctx->r[1]), static_cast<uint32_t>(ctx->r[3]),
      static_cast<uint32_t>(ctx->r[10]), static_cast<uint32_t>(ctx->r[11]),
      static_cast<uint32_t>(ctx->r[13]), static_cast<uint32_t>(ctx->r[29]),
      static_cast<uint32_t>(ctx->r[30]), static_cast<uint32_t>(ctx->r[31]));
}

void UpdateBlueDragonDrawWaitKernelTimeForFastpath(void* raw_context) {
  auto ctx = reinterpret_cast<xe::cpu::ppc::PPCContext*>(raw_context);
  if (!ctx || !ctx->processor) {
    return;
  }
  auto memory = ctx->processor->memory();
  uint32_t pcr_address = static_cast<uint32_t>(ctx->r[13]);
  auto pcr = memory->TranslateVirtual(pcr_address);
  if (!pcr) {
    return;
  }
  uint32_t current_thread = xe::load_and_swap<uint32_t>(pcr + 0x100);
  auto thread = memory->TranslateVirtual(current_thread);
  if (!thread) {
    return;
  }
  xe::store_and_swap<uint32_t>(thread + 0x58,
                               xe::Clock::QueryGuestUptimeMillis());
}

bool ParseGprLrHelper(const xe::cpu::GuestFunction* function, bool* is_save,
                      int* first_gpr) {
  if (!function || !is_save || !first_gpr) {
    return false;
  }

  const std::string& name = function->name();
  constexpr std::string_view kSavePrefix = "__savegprlr_";
  constexpr std::string_view kRestPrefix = "__restgprlr_";
  std::string_view suffix;
  if (function->behavior() == xe::cpu::Function::Behavior::kProlog &&
      name.rfind(kSavePrefix, 0) == 0) {
    *is_save = true;
    suffix = std::string_view(name).substr(kSavePrefix.size());
  } else if (function->behavior() ==
                 xe::cpu::Function::Behavior::kEpilogReturn &&
             name.rfind(kRestPrefix, 0) == 0) {
    *is_save = false;
    suffix = std::string_view(name).substr(kRestPrefix.size());
  } else {
    return false;
  }

  int parsed = 0;
  auto result =
      std::from_chars(suffix.data(), suffix.data() + suffix.size(), parsed);
  if (result.ec != std::errc() || result.ptr != suffix.data() + suffix.size() ||
      parsed < 14 || parsed > 31) {
    return false;
  }
  *first_gpr = parsed;
  return true;
}

bool DecodePpcLwz(uint32_t instr, uint32_t* dest_reg, uint32_t* base_reg,
                  int32_t* offset) {
  if (!dest_reg || !base_reg || !offset || (instr >> 26) != 32) {
    return false;
  }
  *dest_reg = (instr >> 21) & 0x1F;
  *base_reg = (instr >> 16) & 0x1F;
  *offset = static_cast<int16_t>(instr & 0xFFFF);
  return true;
}

bool IsScaledU32LoadOffset(int32_t offset) {
  return offset >= 0 && offset <= 16380 && (offset & 3) == 0;
}

bool ParsePpcThreadFieldLeafHelper(xe::Memory* memory,
                                   const xe::cpu::GuestFunction* function,
                                   int32_t* thread_offset,
                                   int32_t* field_offset) {
  if (!memory || !function || !thread_offset || !field_offset ||
      function->behavior() != xe::cpu::Function::Behavior::kDefault) {
    return false;
  }

  const uint32_t address = function->address();
  const uint8_t* code = memory->TranslateVirtual<const uint8_t*>(address);
  const uint32_t instr0 = xe::load_and_swap<uint32_t>(code + 0);
  const uint32_t instr1 = xe::load_and_swap<uint32_t>(code + 4);
  const uint32_t instr2 = xe::load_and_swap<uint32_t>(code + 8);

  uint32_t dest0 = 0;
  uint32_t base0 = 0;
  int32_t offset0 = 0;
  uint32_t dest1 = 0;
  uint32_t base1 = 0;
  int32_t offset1 = 0;
  if (!DecodePpcLwz(instr0, &dest0, &base0, &offset0) ||
      !DecodePpcLwz(instr1, &dest1, &base1, &offset1) ||
      instr2 != 0x4E800020 || dest0 != 11 || base0 != 13 || dest1 != 3 ||
      base1 != 11 || !IsScaledU32LoadOffset(offset0) ||
      !IsScaledU32LoadOffset(offset1)) {
    return false;
  }

  *thread_offset = offset0;
  *field_offset = offset1;
  return true;
}
}  // namespace

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

using namespace Xbyak_aarch64;

// Defined in a64_backend.cc.
extern uint64_t ResolveFunction(void* raw_context, uint64_t target_address);

static uint64_t UndefinedCallExtern(void* raw_context, uint64_t function_ptr) {
  auto function = reinterpret_cast<Function*>(function_ptr);
  XELOGE("undefined extern call to {:08X} {}", function->address(),
         function->name());
  return 0;
}

static uint64_t TrapDebugPrint(void* raw_context, uint64_t address) {
  (void)address;
  auto context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  if (!context || !context->virtual_membase) {
    return 0;
  }

  uint32_t str_ptr = static_cast<uint32_t>(context->r[3]);
  auto str =
      reinterpret_cast<const char*>(context->virtual_membase + str_ptr);
  XELOGD("(DebugPrint) {}", str ? str : "");
  return 0;
}

static uint64_t TrapDebugBreak(void* raw_context, uint64_t address) {
  (void)address;
  auto context = reinterpret_cast<ppc::PPCContext*>(raw_context);
  uint32_t thread_id = context ? context->thread_id : 0;
  XELOGE("tw/td forced trap hit on A64 thid {:08X}", thread_id);
  if (cvars::break_on_debugbreak) {
    xe::debugging::Break();
  }
  return 0;
}

static constexpr size_t kMaxCodeSize = 1_MiB;

// Register maps:
// GPR allocatable registers: x22, x23, x24, x25, x26, x27, x28
// (x19=backend context, x20=context, x21=membase are reserved)
const uint32_t A64Emitter::gpr_reg_map_[GPR_COUNT] = {
    22, 23, 24, 25, 26, 27, 28,
};

// VEC allocatable registers: v4-v15, v16-v31
// (v0-v3 are scratch)
const uint32_t A64Emitter::vec_reg_map_[VEC_COUNT] = {
    4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
};

A64Emitter::A64Emitter(A64Backend* backend, XbyakA64Allocator* allocator)
    : CodeGenerator(kMaxCodeSize, Xbyak_aarch64::DontSetProtectRWE, allocator),
      processor_(backend->processor()),
      backend_(backend),
      code_cache_(backend->code_cache()),
      allocator_(allocator),
      feature_flags_(arm64::GetFeatureFlags()) {}

A64Emitter::~A64Emitter() = default;

bool A64Emitter::Emit(GuestFunction* function, hir::HIRBuilder* builder,
                      uint32_t debug_info_flags, FunctionDebugInfo* debug_info,
                      void** out_code_address, size_t* out_code_size,
                      std::vector<SourceMapEntry>* out_source_map) {
  SCOPE_profile_cpu_f("cpu");

  guest_module_ = dynamic_cast<XexModule*>(function->module());

  debug_info_ = debug_info;
  debug_info_flags_ = debug_info_flags;
  trace_data_ = &function->trace_data();

  current_guest_function_ = function->address();
  current_guest_function_entry_count_ =
      static_cast<A64Function*>(function)->profile_entry_count();

  // Reset state.
  stack_size_ = StackLayout::GUEST_STACK_SIZE;
  source_map_arena_.Reset();
  tail_code_.clear();
  fpcr_mode_ = FPCRMode::Unknown;

  // Try to emit.
  EmitFunctionInfo func_info = {};
  if (!Emit(builder, func_info)) {
    return false;
  }

  // Emplace the code into the code cache.
  *out_code_address = Emplace(func_info, function);
  *out_code_size = func_info.code_size.total;

  // Copy source map.
  source_map_arena_.CloneContents(out_source_map);

  return *out_code_address != nullptr;
}

void A64Emitter::EmitAtomicIncrement64(std::atomic<uint64_t>* counter) {
  if (!counter) {
    return;
  }

  mov(x9, reinterpret_cast<uint64_t>(counter));
  auto& retry = NewCachedLabel();
  L(retry);
  ldxr(x10, ptr(x9));
  add(x10, x10, uint32_t{1});
  stxr(w11, x10, ptr(x9));
  cbnz(w11, retry);
}

bool A64Emitter::Emit(hir::HIRBuilder* builder, EmitFunctionInfo& func_info) {
  // Calculate local variable stack offsets.
  auto locals = builder->locals();
  size_t stack_offset = StackLayout::GUEST_STACK_SIZE;
  for (auto it = locals.begin(); it != locals.end(); ++it) {
    auto slot = *it;
    size_t type_size = hir::GetTypeSize(slot->type);
    // Align to natural size (at least 4 bytes for ARM64 alignment).
    size_t align_size = xe::round_up(type_size, static_cast<size_t>(4));
    stack_offset = xe::align(stack_offset, align_size);
    slot->set_constant(static_cast<uint32_t>(stack_offset));
    stack_offset += type_size;
  }
  // Align total stack offset to 16 bytes (ARM64 ABI requirement).
  stack_offset -= StackLayout::GUEST_STACK_SIZE;
  stack_offset = xe::align(stack_offset, static_cast<size_t>(16));

  const size_t stack_size = StackLayout::GUEST_STACK_SIZE + stack_offset;
  // ARM64 ABI: SP must always be 16-byte aligned.
  assert_true(stack_size % 16 == 0);
  func_info.stack_size = stack_size;
  func_info.lr_save_offset = StackLayout::HOST_RET_ADDR;
  stack_size_ = stack_size;

  struct {
    size_t prolog;
    size_t body;
    size_t epilog;
    size_t tail;
    size_t prolog_stack_alloc;
  } code_offsets = {};

  // ========================================================================
  // PROLOG
  // ========================================================================
  code_offsets.prolog = getSize();

  // sub sp, sp, #stack_size
  if (stack_size <= 4095) {
    sub(sp, sp, static_cast<uint32_t>(stack_size));
  } else {
    mov(x17, static_cast<uint64_t>(stack_size));
    sub(sp, sp, x17, UXTX);
  }
  code_offsets.prolog_stack_alloc = getSize();

  // Store host return address (x30/LR) so the epilog can restore it.
  str(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
  // Store guest PPC return address (passed in x0 by convention).
  str(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
  // Store zero for call return address (we haven't made a call yet).
  str(xzr, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));

  // Record stackpoint for longjmp recovery, then save the resulting depth
  // for post-call detection (if depth changes, a longjmp skipped frames).
  PushStackpoint();
  if (cvars::a64_enable_host_guest_stack_synchronization) {
    ldr(w16, ptr(x19, static_cast<uint32_t>(offsetof(
                          A64BackendContext, current_stackpoint_depth))));
    str(w16, ptr(sp, static_cast<uint32_t>(
                         StackLayout::GUEST_SAVED_STACKPOINT_DEPTH)));
  }
  if (A64CallTraceRequested()) {
    mov(x1, static_cast<uint64_t>(current_guest_function_));
    CallNativeSafe(reinterpret_cast<void*>(&TraceFunctionEntry));
  }
  if (backend_->speed_profile_enabled()) {
    EmitAtomicIncrement64(current_guest_function_entry_count_);
  }

  // ========================================================================
  // BODY
  // ========================================================================
  code_offsets.body = getSize();

  // Allocate the epilog label (owned by label_cache_ for cleanup).
  auto epilog_label_ptr = new Label();
  label_cache_.push_back(epilog_label_ptr);
  epilog_label_ = epilog_label_ptr;

  if (TryEmitBlueDragonDrawWaitFunctionBody()) {
    b(*epilog_label_);
  } else {
  // Walk HIR blocks and emit ARM64 instructions.
  auto block = builder->first_block();
  synchronize_stack_on_next_instruction_ = false;
  while (block) {
    // Reset FPCR tracking on each block entry (we don't know which
    // predecessor ran, so mode is unknown).
    ForgetFpcrMode();

    // Bind all labels targeting this block.
    auto label = block->label_head;
    while (label) {
      L(GetLabel(label->id));
      label = label->next;
    }

    // Process each instruction in the block.
    const hir::Instr* instr = block->instr_head;
    while (instr) {
      // After a guest call, check for longjmp on the next real instruction.
      // Skip SOURCE_OFFSET because the return address from the call would
      // point past the check, so it would never execute.
      if (synchronize_stack_on_next_instruction_) {
        if (instr->GetOpcodeNum() != hir::OPCODE_SOURCE_OFFSET) {
          synchronize_stack_on_next_instruction_ = false;
          EnsureSynchronizedGuestAndHostStack();
        }
      }
      const hir::Instr* new_tail = instr;
      if (!SelectSequence(this, instr, &new_tail)) {
        // No sequence matched — this is expected in Phase 1 before
        // sequences are implemented.
        XELOGE("A64: Unable to process HIR opcode {}",
               hir::GetOpcodeName(instr->GetOpcodeInfo()));
        return false;
      }
      instr = new_tail;
    }

    block = block->next;
  }
  }

  // ========================================================================
  // EPILOG
  // ========================================================================
  L(*epilog_label_);
  epilog_label_ = nullptr;
  code_offsets.epilog = getSize();

  // Pop stackpoint before leaving.
  PopStackpoint();

  // Restore host return address and deallocate stack.
  ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
  if (stack_size <= 4095) {
    add(sp, sp, static_cast<uint32_t>(stack_size));
  } else {
    mov(x17, static_cast<uint64_t>(stack_size));
    add(sp, sp, x17, UXTX);
  }
  ret();

  // ========================================================================
  // TAIL CODE
  // ========================================================================
  for (auto& tail_item : tail_code_) {
    // ARM64 instructions are always 4-byte aligned, so alignment is mostly
    // a no-op unless we want cache-line alignment for hot paths.
    L(tail_item.label);
    tail_item.func(*this, tail_item.label);
  }
  code_offsets.tail = getSize();

  // Fill in EmitFunctionInfo metrics.
  assert_zero(code_offsets.prolog);
  func_info.code_size.total = getSize();
  func_info.code_size.prolog = code_offsets.body - code_offsets.prolog;
  func_info.code_size.body = code_offsets.epilog - code_offsets.body;
  func_info.code_size.epilog = code_offsets.tail - code_offsets.epilog;
  func_info.code_size.tail = getSize() - code_offsets.tail;
  func_info.prolog_stack_alloc_offset =
      code_offsets.prolog_stack_alloc - code_offsets.prolog;

  return true;
}

void* A64Emitter::Emplace(const EmitFunctionInfo& func_info,
                          GuestFunction* function) {
  assert_true(func_info.code_size.total == getSize());

  void* new_execute_address;
  void* new_write_address;

  if (function) {
    code_cache_->PlaceGuestCode(
        function->address(),
        const_cast<void*>(static_cast<const void*>(getCode())), func_info,
        function, new_execute_address, new_write_address);
  } else {
    code_cache_->PlaceHostCode(
        0, const_cast<void*>(static_cast<const void*>(getCode())), func_info,
        new_execute_address, new_write_address);
  }

  // In xbyak_aarch64, labels are resolved at define time (backpatching),
  // so all relative offsets are already correct. We just need to reset
  // the codegen state for the next function.
  reset();
  tail_code_.clear();

  // Clean up cached labels.
  for (auto* cached_label : label_cache_) {
    delete cached_label;
  }
  label_cache_.clear();

  // Clean up HIR->xbyak label map.
  for (auto& pair : label_map_) {
    delete pair.second;
  }
  label_map_.clear();

  return new_execute_address;
}

void A64Emitter::MarkSourceOffset(const hir::Instr* i) {
  auto entry = source_map_arena_.Alloc<SourceMapEntry>();
  entry->guest_address = static_cast<uint32_t>(i->src1.offset);
  entry->hir_offset = uint32_t(i->block->ordinal << 16) | i->ordinal;
  entry->code_offset = static_cast<uint32_t>(getSize());
}

void A64Emitter::DebugBreak() {
  if (!cvars::break_on_debugbreak) {
    return;
  }
  brk(0xF000);
}

void A64Emitter::Trap(uint16_t trap_type) {
  switch (trap_type) {
    case 20:
    case 26:
      // 0x0FE00014 is a debug print trap where r3 points at the buffer.
      mov(x1, uint64_t{0});
      CallNativeSafe(reinterpret_cast<void*>(&TrapDebugPrint));
      break;
    case 0:
    case 22:
      mov(x1, uint64_t{0});
      CallNativeSafe(reinterpret_cast<void*>(&TrapDebugBreak));
      break;
    case 25:
      break;
    default:
      XELOGW("A64: Unknown trap type {}", trap_type);
      DebugBreak();
      break;
  }
}

void A64Emitter::UnimplementedInstr(const hir::Instr* i) {
  XELOGE("A64: Unimplemented HIR instruction: {}",
         hir::GetOpcodeName(i->GetOpcodeInfo()));
  DebugBreak();
}

bool A64Emitter::TryEmitGprLrHelperCall(const hir::Instr* instr,
                                        GuestFunction* function) {
  if (!cvars::a64_inline_gprlr_helpers) {
    return false;
  }

  bool is_save = false;
  int first_gpr = 0;
  if (!ParseGprLrHelper(function, &is_save, &first_gpr)) {
    return false;
  }

  const bool is_tail_call = (instr->flags & hir::CALL_TAIL) != 0;
  if (is_save && is_tail_call) {
    return false;
  }
  if (!is_save && !is_tail_call) {
    return false;
  }

  ForgetFpcrMode();

  const int32_t first_stack_offset = -8 * (33 - first_gpr);
  const uint32_t base_subtract =
      static_cast<uint32_t>(-first_stack_offset);

  ldr(w9, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[1]))));
  add(x9, GetMembaseReg(), x9);
  if (base_subtract) {
    sub(x9, x9, base_subtract);
  }

  for (int gpr = first_gpr; gpr <= 31; ++gpr) {
    const uint32_t slot_offset =
        static_cast<uint32_t>((-8 * (33 - gpr)) - first_stack_offset);
    const auto context_offset =
        static_cast<int32_t>(offsetof(ppc::PPCContext, r) +
                             sizeof(uint64_t) * static_cast<size_t>(gpr));
    if (is_save) {
      ldr(x10, ptr(GetContextReg(), context_offset));
      rev(x10, x10);
      str(x10, ptr(x9, slot_offset));
    } else {
      ldr(x10, ptr(x9, slot_offset));
      rev(x10, x10);
      str(x10, ptr(GetContextReg(), context_offset));
    }
  }

  const uint32_t lr_slot_offset =
      static_cast<uint32_t>(-8 - first_stack_offset);
  if (is_save) {
    ldr(w10, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, r[12]))));
    rev(w10, w10);
    str(w10, ptr(x9, lr_slot_offset));
  } else {
    ldr(w10, ptr(x9, lr_slot_offset));
    rev(w10, w10);
    str(x10, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, r[12]))));
    str(x10, ptr(GetContextReg(),
                 static_cast<int32_t>(offsetof(ppc::PPCContext, lr))));
    mov(w16, w10);
    ldr(w11, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    cmp(w16, w11);
    auto& tail_jump = NewCachedLabel();
    b(NE, tail_jump);
    b(epilog_label());

    L(tail_jump);
    if (backend_->speed_profile_enabled()) {
      EmitAtomicIncrement64(backend_->speed_profile_indirect_guest_calls());
    }
    if (code_cache_->has_indirection_table()) {
      mov(x0, A64CodeCache::execute_address_high());
      orr(x16, x16, x0);
      ldr(w9, ptr(x16, static_cast<uint32_t>(0)));
      orr(x9, x9, x0);
    } else {
      mov(x0, x20);
      mov(x1, x16);
      mov(x9, reinterpret_cast<uint64_t>(&ResolveFunction));
      blr(x9);
      mov(x9, x0);
    }

    PopStackpoint();
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
    if (stack_size() <= 4095) {
      add(sp, sp, static_cast<uint32_t>(stack_size()));
    } else {
      mov(x17, static_cast<uint64_t>(stack_size()));
      add(sp, sp, x17, UXTX);
    }
    br(x9);
  }

  return true;
}

void A64Emitter::AddGuestAddressToMembase(Xbyak_aarch64::WReg guest_reg,
                                          Xbyak_aarch64::XReg host_reg) {
  mov(WReg(host_reg.getIdx()), guest_reg);
  if (xe::memory::allocation_granularity() > 0x1000) {
    mov(w12, 0xE0000000u);
    cmp(WReg(host_reg.getIdx()), w12);
    auto& skip_offset = NewCachedLabel();
    b(LO, skip_offset);
    add(WReg(host_reg.getIdx()), WReg(host_reg.getIdx()), 1, 12);
    L(skip_offset);
  }
  add(host_reg, GetMembaseReg(), host_reg);
}

bool A64Emitter::TryEmitPpcThreadFieldLeafHelperCall(const hir::Instr* instr,
                                                     GuestFunction* function) {
  if (!cvars::a64_inline_ppc_thread_field_leaf_helpers ||
      (instr->flags & hir::CALL_TAIL) != 0) {
    return false;
  }

  int32_t thread_offset = 0;
  int32_t field_offset = 0;
  if (!ParsePpcThreadFieldLeafHelper(processor_->memory(), function,
                                     &thread_offset, &field_offset)) {
    return false;
  }

  ForgetFpcrMode();

  // Inline:
  //   lwz r11, D(r13)
  //   lwz r3, D(r11)
  //   blr
  //
  // Store r11 as well as r3 so the volatile-register side effect matches the
  // guest helper for debug traces and any non-ABI caller oddities.
  ldr(w9, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[13]))));
  AddGuestAddressToMembase(w9, x9);
  ldr(w10, ptr(x9, static_cast<uint32_t>(thread_offset)));
  rev(w10, w10);
  str(x10, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[11]))));

  AddGuestAddressToMembase(w10, x10);
  ldr(w11, ptr(x10, static_cast<uint32_t>(field_offset)));
  rev(w11, w11);
  str(x11, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));

  return true;
}

bool A64Emitter::TryEmitBlueDragonDrawWaitFunctionBody() {
  if (!cvars::arm64_blue_dragon_draw_wait_fastpath ||
      current_guest_function_ != 0x8246B408) {
    return false;
  }

  ForgetFpcrMode();

  auto& return_zero = NewCachedLabel();
  auto& return_one = NewCachedLabel();
  auto& done = NewCachedLabel();
  auto& no_token_change = NewCachedLabel();
  auto& no_owner_refresh = NewCachedLabel();

  // Hand-emits Blue Dragon's draw wait predicate:
  // token progress or current-KTHREAD ownership refreshes wait_state+0xC;
  // elapsed < 5000 ms returns 1, otherwise returns 0. This intentionally skips
  // the timeout helper side effect and is guarded by a title-specific cvar.
  const bool update_kernel_time =
      cvars::arm64_blue_dragon_draw_wait_probe;
  uint32_t inline_step = update_kernel_time
                             ? cvars::arm64_blue_dragon_draw_wait_inline_tick_step
                             : 0;
  const bool host_counter_time =
      update_kernel_time &&
      cvars::arm64_blue_dragon_draw_wait_fastpath_host_counter_time &&
      inline_step == 0;
  if (update_kernel_time && inline_step == 0 && !host_counter_time) {
    uint32_t stride =
        std::max<uint32_t>(cvars::arm64_blue_dragon_draw_wait_probe_stride, 1);
    Xbyak_aarch64::Label* skip_update = nullptr;
    if (stride > 1 && (stride & (stride - 1)) == 0) {
      skip_update = &NewCachedLabel();
      ldr(w17, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                           A64BackendContext,
                                           blue_dragon_draw_wait_probe_counter))));
      add(w17, w17, 1);
      str(w17, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                           A64BackendContext,
                                           blue_dragon_draw_wait_probe_counter))));
      and_(w17, w17, stride - 1);
      cbnz(w17, *skip_update);
    }
    CallNativeSafe(
        reinterpret_cast<void*>(&UpdateBlueDragonDrawWaitKernelTimeForFastpath));
    if (skip_update) {
      L(*skip_update);
    }
  }

  ldr(w9, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));
  cbz(w9, return_zero);
  AddGuestAddressToMembase(w9, x9);  // x9 = wait state host pointer.

  ldr(w10, ptr(x9, 0));
  rev(w10, w10);
  cbz(w10, return_zero);
  AddGuestAddressToMembase(w10, x10);  // x10 = draw object host pointer.

  add(x12, x10, 2, 12);
  ldrb(w11, ptr(x12, 0xA39));
  tbnz(w11, 1, return_zero);

  ldr(w13, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[13]))));
  cbz(w13, return_zero);
  AddGuestAddressToMembase(w13, x13);
  ldr(w13, ptr(x13, 0x100));
  rev(w13, w13);
  cbz(w13, return_zero);
  AddGuestAddressToMembase(w13, x13);  // x13 = current KTHREAD host pointer.

  if (host_counter_time) {
    auto& non_negative_time = NewCachedLabel();
    auto& have_time = NewCachedLabel();
    // CNTVCT_EL0 / CNTFRQ_EL0 gives host milliseconds without a native thunk.
    // Subtract the per-context base so the value matches guest uptime shape.
    mrs(x17, 3, 3, 14, 0, 2);  // CNTVCT_EL0.
    mov(x11, uint64_t{1000});
    mul(x17, x17, x11);
    mrs(x11, 3, 3, 14, 0, 0);  // CNTFRQ_EL0.
    udiv(x17, x17, x11);
    ldr(x11, ptr(GetBackendCtxReg(), static_cast<uint32_t>(offsetof(
                                      A64BackendContext,
                                      host_uptime_millis_base))));
    cmp(x17, x11);
    b(HS, non_negative_time);
    mov(x17, uint64_t{0});
    b(have_time);
    L(non_negative_time);
    sub(x17, x17, x11);
    L(have_time);
    rev(w11, w17);
    str(w11, ptr(x13, 0x58));
  }

  ldr(w17, ptr(x13, 0x58));
  rev(w17, w17);
  if (inline_step != 0) {
    inline_step = std::min<uint32_t>(inline_step, 0xFFFFu);
    if (inline_step <= 4095) {
      add(w17, w17, inline_step);
    } else {
      mov(w11, inline_step);
      add(w17, w17, w11);
    }
    rev(w11, w17);
    str(w11, ptr(x13, 0x58));
  }

  ldr(w14, ptr(x10, 0x2A10));
  rev(w14, w14);
  cbz(w14, return_zero);
  AddGuestAddressToMembase(w14, x14);
  ldr(w15, ptr(x9, 0x8));
  rev(w15, w15);
  ldr(w16, ptr(x14, 0));
  rev(w16, w16);
  cmp(w15, w16);
  b(EQ, no_token_change);
  mov(w11, w17);
  rev(w11, w11);
  str(w11, ptr(x9, 0xC));
  mov(w11, w16);
  rev(w11, w11);
  str(w11, ptr(x9, 0x8));
  L(no_token_change);

  ldr(w11, ptr(x13, 0x14C));
  rev(w11, w11);
  ldr(w15, ptr(x10, 0x2A08));
  rev(w15, w15);
  cmp(w15, w11);
  b(NE, no_owner_refresh);
  ldr(w15, ptr(x10, 0x2A70));
  rev(w15, w15);
  cbz(w15, no_owner_refresh);
  mov(w11, w17);
  rev(w11, w11);
  str(w11, ptr(x9, 0xC));
  L(no_owner_refresh);

  ldr(w10, ptr(x9, 0xC));
  rev(w10, w10);
  sub(w10, w17, w10);
  mov(w11, uint32_t{0x1388});
  cmp(w10, w11);
  b(LO, return_one);

  L(return_zero);
  str(xzr, ptr(GetContextReg(),
               static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));
  b(done);

  L(return_one);
  mov(w9, uint32_t{1});
  str(x9, ptr(GetContextReg(),
              static_cast<int32_t>(offsetof(ppc::PPCContext, r[3]))));

  L(done);
  return true;
}

void A64Emitter::Call(const hir::Instr* instr, GuestFunction* function) {
  assert_not_null(function);
  if (TryEmitGprLrHelperCall(instr, function)) {
    return;
  }
  if (TryEmitPpcThreadFieldLeafHelperCall(instr, function)) {
    return;
  }

  ForgetFpcrMode();
  auto fn = static_cast<A64Function*>(function);
  if (backend_->speed_profile_enabled()) {
    EmitAtomicIncrement64(backend_->speed_profile_direct_guest_calls());
  }

  if (fn->machine_code()) {
    // Direct call — function is already compiled.
    mov(x9, reinterpret_cast<uint64_t>(fn->machine_code()));
    if (!(instr->flags & hir::CALL_TAIL)) {
      // Pass the next call's guest return address in x0.
      ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
      blr(x9);
      synchronize_stack_on_next_instruction_ = true;
    } else {
      // Tail call: pass our return address to the callee.
      PopStackpoint();
      ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
      ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
      if (stack_size() <= 4095) {
        add(sp, sp, static_cast<uint32_t>(stack_size()));
      } else {
        mov(x17, static_cast<uint64_t>(stack_size()));
        add(sp, sp, x17, UXTX);
      }
      br(x9);
    }
    return;
  }

  if (code_cache_->has_indirection_table()) {
    // Load host code address from indirection table.
    mov(x0,A64CodeCache::execute_address_high());
    mov(w16, function->address());
    orr(x16, x16,x0);
    ldr(w9, ptr(x16, static_cast<uint32_t>(0)));
    orr(x9, x9,x0);
  } else {
    // Fallback: resolve at runtime.
    mov(x0, x20);  // context
    mov(x1, static_cast<uint64_t>(function->address()));
    mov(x9, reinterpret_cast<uint64_t>(&ResolveFunction));
    blr(x9);
    mov(x9, x0);  // resolved address in x9
  }

  if (instr->flags & hir::CALL_TAIL) {
    PopStackpoint();
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
    if (stack_size() <= 4095) {
      add(sp, sp, static_cast<uint32_t>(stack_size()));
    } else {
      mov(x17, static_cast<uint64_t>(stack_size()));
      add(sp, sp, x17, UXTX);
    }
    br(x9);
  } else {
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
    blr(x9);
    synchronize_stack_on_next_instruction_ = true;
  }
}

void A64Emitter::CallIndirect(const hir::Instr* instr, int reg_index) {
  ForgetFpcrMode();
  auto target_w = WReg(reg_index);

  // Check if this is a possible return (e.g., PPC blr).
  if (instr->flags & hir::CALL_POSSIBLE_RETURN) {
    // Compare target guest address with our function's return address.
    ldr(w0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    cmp(target_w, w0);
    b(EQ, epilog_label());
  }
  if (backend_->speed_profile_enabled()) {
    EmitAtomicIncrement64(backend_->speed_profile_indirect_guest_calls());
  }

  // Load host code address from indirection table.
  if (code_cache_->has_indirection_table()) {
    mov(x0,A64CodeCache::execute_address_high());
    mov(w16, target_w);  // w16 = guest address (also used by resolve thunk)
    orr(x16, x16,x0);
    ldr(w9, ptr(x16, static_cast<uint32_t>(
                         0)));  // w9 = host code from indirection table
    orr(x9, x9,x0);
  } else {
    // Fallback: resolve at runtime.
    mov(w16, target_w);
    mov(x0, x20);  // context
    mov(x1, x16);  // guest address
    mov(x9, reinterpret_cast<uint64_t>(&ResolveFunction));
    blr(x9);
    mov(x9, x0);  // resolved address
  }

  if (instr->flags & hir::CALL_TAIL) {
    // Tail call: pass our return address to the callee.
    PopStackpoint();
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_RET_ADDR)));
    ldr(x30, ptr(sp, static_cast<uint32_t>(StackLayout::HOST_RET_ADDR)));
    if (stack_size() <= 4095) {
      add(sp, sp, static_cast<uint32_t>(stack_size()));
    } else {
      mov(x17, static_cast<uint64_t>(stack_size()));
      add(sp, sp, x17, UXTX);
    }
    br(x9);
  } else {
    // Regular call: pass the next call's return address.
    ldr(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
    blr(x9);
    synchronize_stack_on_next_instruction_ = true;
  }
}

void A64Emitter::CallExtern(const hir::Instr* instr, const Function* function) {
  ForgetFpcrMode();
  if (backend_->speed_profile_enabled()) {
    EmitAtomicIncrement64(backend_->speed_profile_extern_calls());
  }
  bool undefined = true;
  if (function->behavior() == Function::Behavior::kBuiltin) {
    auto builtin_function = static_cast<const BuiltinFunction*>(function);
    if (builtin_function->handler()) {
      undefined = false;
      // GuestToHostThunk: x0=target, x1=arg0, x2=arg1
      // Thunk rearranges to: x0=context, x1=arg0, x2=arg1, calls target
      mov(x0, reinterpret_cast<uint64_t>(builtin_function->handler()));
      mov(x1, reinterpret_cast<uint64_t>(builtin_function->arg0()));
      mov(x2, reinterpret_cast<uint64_t>(builtin_function->arg1()));
      mov(x9, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
      blr(x9);
    }
  } else if (function->behavior() == Function::Behavior::kExtern) {
    auto extern_function = static_cast<const GuestFunction*>(function);
    if (extern_function->extern_handler()) {
      undefined = false;
      // GuestToHostThunk: x0=target, x1=arg0
      mov(x0, reinterpret_cast<uint64_t>(extern_function->extern_handler()));
      ldr(x1, ptr(GetContextReg(), static_cast<int32_t>(offsetof(
                                       ppc::PPCContext, kernel_state))));
      mov(x9, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
      blr(x9);
    }
  }
  if (undefined) {
    // Set arg0 = function pointer, then call UndefinedCallExtern via thunk.
    mov(x1, reinterpret_cast<uint64_t>(function));
    CallNativeSafe(reinterpret_cast<void*>(&UndefinedCallExtern));
  }
}

void A64Emitter::CallNative(void* fn) { CallNativeSafe(fn); }

void A64Emitter::CallNativeSafe(void* fn) {
  // GuestToHostThunk: x0=target function, x1/x2=args (set by caller).
  // The thunk rearranges: saves x0 in x9, sets x0=context, calls x9.
  mov(x0, reinterpret_cast<uint64_t>(fn));
  mov(x9, reinterpret_cast<uint64_t>(backend()->guest_to_host_thunk()));
  blr(x9);
}

void A64Emitter::SetReturnAddress(uint64_t value) {
  mov(x0, value);
  str(x0, ptr(sp, static_cast<uint32_t>(StackLayout::GUEST_CALL_RET_ADDR)));
}

void A64Emitter::ReloadMembase() {
  // Reload x21 from context->virtual_membase.
  ldr(x21, ptr(x20, static_cast<int32_t>(
                        offsetof(ppc::PPCContext, virtual_membase))));
}

bool A64Emitter::ChangeFpcrMode(FPCRMode new_mode, bool already_set) {
  if (fpcr_mode_ == new_mode) {
    return false;
  }
  fpcr_mode_ = new_mode;
  if (!already_set) {
    // Load the pre-computed FPCR value from the backend context.
    // This avoids an expensive MRS + read-modify-write cycle.
    auto bctx = GetBackendCtxReg();
    if (new_mode == FPCRMode::Vmx) {
      ldr(w0, Xbyak_aarch64::ptr(bctx, static_cast<uint32_t>(offsetof(
                                           A64BackendContext, fpcr_vmx))));
    } else {
      ldr(w0, Xbyak_aarch64::ptr(bctx, static_cast<uint32_t>(offsetof(
                                           A64BackendContext, fpcr_fpu))));
    }
    msr(3, 3, 4, 4, 0, x0);  // msr FPCR, x0
  }
  return true;
}

Label& A64Emitter::AddToTail(TailEmitCallback callback, uint32_t alignment) {
  TailEmitter tail;
  tail.alignment = alignment;
  tail.func = std::move(callback);
  tail_code_.push_back(std::move(tail));
  return tail_code_.back().label;
}

Label& A64Emitter::NewCachedLabel() {
  auto* label = new Label();
  label_cache_.push_back(label);
  return *label;
}

Label& A64Emitter::GetLabel(uint32_t label_id) {
  auto it = label_map_.find(label_id);
  if (it != label_map_.end()) {
    return *it->second;
  }
  auto* label = new Label();
  label_map_[label_id] = label;
  return *label;
}

void A64Emitter::HandleStackpointOverflowError(ppc::PPCContext* context) {
  if (debugging::IsDebuggerAttached()) {
    debugging::Break();
  }
  xe::FatalError(
      "Overflowed stackpoints! Please report this error for this title to "
      "Xenia developers.");
}

void A64Emitter::PushStackpoint() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  // x8 = stackpoints array, w9 = current depth
  ldr(x8, ptr(x19,
              static_cast<uint32_t>(offsetof(A64BackendContext, stackpoints))));
  ldr(w9, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));

  // Compute offset into array: x10 = w9 * sizeof(A64BackendStackpoint)
  mov(w10, static_cast<uint32_t>(sizeof(A64BackendStackpoint)));
  umull(x10, w9, w10);
  add(x8, x8, x10);

  // Store host SP.
  mov(x10, sp);
  str(x10, ptr(x8, static_cast<uint32_t>(
                       offsetof(A64BackendStackpoint, host_stack_))));
  // Store guest r1 (32-bit).
  ldr(w10, ptr(x20, static_cast<int32_t>(offsetof(ppc::PPCContext, r[1]))));
  str(w10, ptr(x8, static_cast<uint32_t>(
                       offsetof(A64BackendStackpoint, guest_stack_))));
  // Store guest LR (32-bit).
  ldr(w10, ptr(x20, static_cast<int32_t>(offsetof(ppc::PPCContext, lr))));
  str(w10, ptr(x8, static_cast<uint32_t>(
                       offsetof(A64BackendStackpoint, guest_return_address_))));

  // Increment depth.
  add(w9, w9, 1);
  str(w9, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));

  // Check for overflow.
  mov(w10, static_cast<uint32_t>(cvars::a64_max_stackpoints));
  cmp(w9, w10);
  auto& overflow_label = AddToTail([](A64Emitter& e, Label& lbl) {
    e.CallNativeSafe(
        reinterpret_cast<void*>(A64Emitter::HandleStackpointOverflowError));
  });
  b(GE, overflow_label);
}

void A64Emitter::PopStackpoint() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  // Decrement current_stackpoint_depth.
  ldr(w8, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));
  sub(w8, w8, 1);
  str(w8, ptr(x19, static_cast<uint32_t>(
                       offsetof(A64BackendContext, current_stackpoint_depth))));
}

void A64Emitter::EnsureSynchronizedGuestAndHostStack() {
  if (!cvars::a64_enable_host_guest_stack_synchronization) {
    return;
  }
  // Compare current stackpoint depth against the value saved after
  // PushStackpoint in the prolog. If different, a longjmp occurred and
  // some frames' PopStackpoint never ran.
  auto& return_from_sync = NewCachedLabel();

  ldr(w17, ptr(x19, static_cast<uint32_t>(offsetof(A64BackendContext,
                                                   current_stackpoint_depth))));
  ldr(w16, ptr(sp, static_cast<uint32_t>(
                       StackLayout::GUEST_SAVED_STACKPOINT_DEPTH)));
  cmp(w17, w16);

  auto& sync_label = AddToTail([&return_from_sync](A64Emitter& e, Label& lbl) {
    // Set up arguments for the sync helper:
    //   x8 = return address (where to resume after fixup)
    //   x9 = this function's stack size
    e.adr(e.x8, return_from_sync);
    e.mov(e.x9, static_cast<uint64_t>(e.stack_size()));
    e.mov(e.x10, reinterpret_cast<uint64_t>(
                     e.backend()->synchronize_guest_and_host_stack_helper()));
    e.br(e.x10);
  });
  b(NE, sync_label);

  L(return_from_sync);
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe
