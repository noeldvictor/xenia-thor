/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/util/kernel_trap.h"

#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/string.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xthread.h"
#include "xenia/memory.h"

namespace xe {
namespace kernel {

std::atomic<uint32_t> g_kernel_trap_key{0};
std::atomic<uint32_t> g_kernel_trap_lr{0};

namespace {

constexpr size_t kStackWords = 256;

struct TrapRecord {
  std::mutex mutex;
  std::atomic<uint32_t> hits{0};
  std::atomic<bool> pause{false};
  std::atomic<bool> paused{false};
  std::atomic<bool> release{false};
  std::string export_name;
  std::string armed_name;
  uint32_t thread_id = 0;
  uint64_t r[32] = {};
  uint64_t lr = 0;
  uint64_t ctr = 0;
  uint32_t stack_base = 0;
  uint32_t stack[kStackWords] = {};
  size_t stack_count = 0;
  uint64_t when_ms = 0;
};

TrapRecord& record() {
  static TrapRecord r;
  return r;
}

// The module tables the shim registers: xboxkrnl is module 0, xam module 1,
// xbdm module 2 (KernelModuleId order). The key is (module + 1) << 16 |
// ordinal so that 0 stays "no trap".
const char* kModuleNames[] = {"xboxkrnl.exe", "xam.xex", "xbdm.xex"};

}  // namespace

void KernelTrapHit(cpu::Export* export_entry, cpu::ppc::PPCContext* ctx) {
  uint32_t lr_filter = g_kernel_trap_lr.load(std::memory_order_relaxed);
  if (lr_filter && static_cast<uint32_t>(ctx->lr) != lr_filter) {
    return;
  }
  TrapRecord& rec = record();
  {
    std::lock_guard<std::mutex> lock(rec.mutex);
    rec.export_name = export_entry ? export_entry->name : "";
    auto* thread = XThread::GetCurrentThread();
    rec.thread_id = thread ? thread->thread_id() : 0;
    for (int i = 0; i < 32; ++i) {
      rec.r[i] = ctx->r[i];
    }
    rec.lr = ctx->lr;
    rec.ctr = ctx->ctr;
    rec.when_ms = xe::Clock::QueryHostUptimeMillis();
    uint32_t r1 = static_cast<uint32_t>(ctx->r[1]);
    rec.stack_base = r1;
    rec.stack_count = 0;
    auto* memory = ctx->kernel_state->memory();
    for (size_t i = 0; i < kStackWords; ++i) {
      uint32_t addr = r1 + static_cast<uint32_t>(i * 4);
      auto* heap = memory->LookupHeap(addr);
      if (!heap || heap->QueryRangeAccess(addr, addr + 3) ==
                       xe::memory::PageAccess::kNoAccess) {
        break;
      }
      rec.stack[i] =
          xe::load_and_swap<uint32_t>(memory->TranslateVirtual(addr));
      rec.stack_count = i + 1;
    }
  }
  uint32_t hit = rec.hits.fetch_add(1, std::memory_order_relaxed) + 1;
  XELOGE("kernel trap hit #{}: {} on thread {:08X} lr={:08X} r1={:08X}", hit,
         rec.export_name, rec.thread_id, static_cast<uint32_t>(ctx->lr),
         static_cast<uint32_t>(ctx->r[1]));
  if (rec.pause.load(std::memory_order_relaxed)) {
    // Hold this guest thread; the other threads keep running and the debug
    // server reads memory. Released by KernelTrapRelease or after 10 minutes.
    rec.release.store(false, std::memory_order_relaxed);
    rec.paused.store(true, std::memory_order_release);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    while (!rec.release.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    rec.paused.store(false, std::memory_order_release);
    XELOGE("kernel trap released: {} on thread {:08X}", rec.export_name,
           rec.thread_id);
  }
}

std::string KernelTrapSet(std::string_view export_name, bool pause,
                          uint32_t lr_filter) {
  auto* ks = kernel_state();
  auto* emulator = ks ? ks->emulator() : nullptr;
  auto* resolver = emulator ? emulator->export_resolver() : nullptr;
  if (!resolver) {
    return "no export resolver (is a title running?)";
  }
  for (const auto& table : resolver->tables()) {
    // The table names are the short module names ("xam"); the shim's
    // KernelModuleId order is xboxkrnl, xam, xbdm.
    uint32_t module_index = 0;
    for (uint32_t m = 0; m < 3; ++m) {
      if (xe::utf8::starts_with_case(kModuleNames[m], table.module_name())) {
        module_index = m;
      }
    }
    for (auto* e : table.exports_by_ordinal()) {
      if (e && export_name == e->name) {
        TrapRecord& rec = record();
        rec.pause.store(pause, std::memory_order_relaxed);
        rec.release.store(false, std::memory_order_relaxed);
        {
          std::lock_guard<std::mutex> lock(rec.mutex);
          rec.armed_name = std::string(export_name);
        }
        g_kernel_trap_lr.store(lr_filter, std::memory_order_relaxed);
        g_kernel_trap_key.store(((module_index + 1u) << 16) | e->ordinal,
                                std::memory_order_release);
        XELOGI("kernel trap armed: {} (module {} ordinal {}, pause={}, lr={:08X})",
               export_name, kModuleNames[module_index], e->ordinal, pause,
               lr_filter);
        return "";
      }
    }
  }
  return fmt::format("export '{}' not found", export_name);
}

void KernelTrapClear() {
  g_kernel_trap_key.store(0, std::memory_order_release);
  KernelTrapRelease();
}

void KernelTrapRelease() { record().release.store(true, std::memory_order_release); }

std::string KernelTrapReportJson() {
  TrapRecord& rec = record();
  std::lock_guard<std::mutex> lock(rec.mutex);
  std::string json = fmt::format(
      "{{\"armed\":\"{}\",\"key\":{},\"hits\":{},\"paused\":{},\"export\":\"{}\","
      "\"tid\":\"{:08X}\",\"age_ms\":{},\"lr\":\"{:08X}\",\"ctr\":\"{:08X}\","
      "\"stack_base\":\"{:08X}\",\"r\":[",
      rec.armed_name, g_kernel_trap_key.load(std::memory_order_relaxed),
      rec.hits.load(std::memory_order_relaxed),
      rec.paused.load(std::memory_order_relaxed) ? "true" : "false",
      rec.export_name, rec.thread_id,
      rec.when_ms ? xe::Clock::QueryHostUptimeMillis() - rec.when_ms : 0,
      static_cast<uint32_t>(rec.lr), static_cast<uint32_t>(rec.ctr),
      rec.stack_base);
  for (int i = 0; i < 32; ++i) {
    json += fmt::format("{}\"{:016X}\"", i ? "," : "", rec.r[i]);
  }
  json += "],\"stack\":[";
  for (size_t i = 0; i < rec.stack_count; ++i) {
    json += fmt::format("{}\"{:08X}\"", i ? "," : "", rec.stack[i]);
  }
  json += "]}";
  return json;
}

}  // namespace kernel
}  // namespace xe
