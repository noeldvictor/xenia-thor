/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// The in-app debug API (2026-09-20). jp.xenia.emulator.DebugServer serves
// HTTP on the device and calls these to read the running emulator directly:
// status, guest threads, the log ring, guest memory, PowerPC disassembly, the
// GPU counters, the last spin-lock stall, and cvars. The PC-side MCP is a
// client of that server; adb remains only for install, port forward, launch
// and force-stop, and simpleperf. Every function returns a JSON string built
// by hand: the values are numbers, hex strings, and escaped text.

#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/cpu/ppc/ppc_opcode_info.h"
#include "xenia/cpu/precompile_status.h"
#include "xenia/cpu/processor.h"
#include "xenia/emulator.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/object_table.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_threading.h"
#include "xenia/kernel/xthread.h"
#include "xenia/memory.h"
#include "xenia/ui/vulkan/vulkan_diagnostic_counters.h"

namespace {

std::string JsonEscape(std::string_view s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          out += fmt::format("\\u{:04x}", static_cast<unsigned>(c));
        } else {
          out.push_back(c);
        }
    }
  }
  return out;
}

jstring ToJava(JNIEnv* env, const std::string& s) {
  return env->NewStringUTF(s.c_str());
}

std::string FromJava(JNIEnv* env, jstring js) {
  if (!js) {
    return "";
  }
  const char* chars = env->GetStringUTFChars(js, nullptr);
  std::string s = chars ? chars : "";
  if (chars) {
    env->ReleaseStringUTFChars(js, chars);
  }
  return s;
}

}  // namespace

extern "C" {

JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeStatus(
    JNIEnv* env, jclass) {
  xe::Emulator* emulator = xe::GetGlobalEmulator();
  const xe::cpu::PrecompileStatus& s = xe::cpu::GetPrecompileStatus();
  std::string json = "{";
  json += fmt::format("\"running\":{},", emulator ? "true" : "false");
  if (emulator) {
    json += fmt::format("\"title_id\":\"{:08X}\",\"title_name\":\"{}\",",
                        emulator->title_id(),
                        JsonEscape(emulator->title_name()));
    json += fmt::format("\"guest_uptime_ms\":{},",
                        xe::Clock::QueryGuestUptimeMillis());
  }
  json += fmt::format("\"host_uptime_ms\":{},", xe::Clock::QueryHostUptimeMillis());
  json += fmt::format("\"swap_count\":{},",
                      xe::ui::vulkan::VulkanPerfCountersGetIssueSwapCount());
  json += fmt::format(
      "\"precompile\":{{\"state\":{},\"done\":{},\"frontier\":{},\"workers\":{},"
      "\"active\":{},\"elapsed_ms\":{},\"temp_c\":{},\"case_c\":{},"
      "\"throttled\":{}}}",
      s.state.load(std::memory_order_acquire),
      s.done.load(std::memory_order_relaxed),
      s.frontier.load(std::memory_order_relaxed),
      s.workers.load(std::memory_order_relaxed),
      s.workers_active.load(std::memory_order_relaxed),
      static_cast<unsigned long long>(s.elapsed_ms.load(std::memory_order_relaxed)),
      s.temp_c.load(std::memory_order_relaxed),
      s.case_c.load(std::memory_order_relaxed),
      s.throttled.load(std::memory_order_relaxed));
  json += "}";
  return ToJava(env, json);
}

// Every kernel thread: id, name, host tid, guest or host, running, the
// KTHREAD state and wait reason, and the guest lr, r1, r3, r13 from the
// context. A thread with a hot host tid and a lr inside a spin function is
// the stall picture; the lr names the caller of the wait.
JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeThreads(
    JNIEnv* env, jclass) {
  xe::Emulator* emulator = xe::GetGlobalEmulator();
  if (!emulator || !emulator->kernel_state()) {
    return ToJava(env, "[]");
  }
  auto threads = emulator->kernel_state()->object_table()->GetObjectsByType<
      xe::kernel::XThread>(xe::kernel::XObject::Type::Thread);
  std::string json = "[";
  bool first = true;
  for (auto& t : threads) {
    if (!first) {
      json += ",";
    }
    first = false;
    uint32_t host_tid = t->thread() ? t->thread()->system_id() : 0;
    uint64_t lr = 0, r1 = 0, r3 = 0, r13 = 0;
    if (t->thread_state() && t->thread_state()->context()) {
      auto* c = t->thread_state()->context();
      lr = c->lr;
      r1 = c->r[1];
      r3 = c->r[3];
      r13 = c->r[13];
    }
    uint32_t state = 0, wait_reason = 0;
    if (t->guest_object()) {
      auto* kt = t->guest_object<xe::kernel::X_KTHREAD>();
      if (kt) {
        state = kt->thread_state;
        wait_reason = kt->wait_reason;
      }
    }
    json += fmt::format(
        "{{\"tid\":\"{:08X}\",\"name\":\"{}\",\"host_tid\":{},\"guest\":{},"
        "\"running\":{},\"state\":{},\"wait_reason\":{},\"lr\":\"{:08X}\","
        "\"r1\":\"{:08X}\",\"r3\":\"{:016X}\",\"r13\":\"{:08X}\"}}",
        t->thread_id(), JsonEscape(t->thread_name()), host_tid,
        t->is_guest_thread() ? "true" : "false",
        t->is_running() ? "true" : "false", state, wait_reason,
        static_cast<uint32_t>(lr), static_cast<uint32_t>(r1), r3,
        static_cast<uint32_t>(r13));
  }
  json += "]";
  return ToJava(env, json);
}

JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeLogTail(
    JNIEnv* env, jclass, jint max_lines, jstring filter) {
  std::string f = FromJava(env, filter);
  auto lines = xe::logging::LogRingTail(
      static_cast<size_t>(max_lines < 1 ? 1 : max_lines), f);
  std::string json = "[";
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i) {
      json += ",";
    }
    json += "\"" + JsonEscape(lines[i]) + "\"";
  }
  json += "]";
  return ToJava(env, json);
}

// Guest memory as a hex string, or "" when the range is not readable. The
// range is checked page by page through the heap, so a bad address is an
// empty answer and never a host fault.
JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeReadMemory(
    JNIEnv* env, jclass, jint address, jint length) {
  xe::Emulator* emulator = xe::GetGlobalEmulator();
  if (!emulator || !emulator->memory() || length <= 0 ||
      length > 4 * 1024 * 1024) {
    return ToJava(env, "");
  }
  xe::Memory* memory = emulator->memory();
  uint32_t addr = static_cast<uint32_t>(address);
  uint32_t len = static_cast<uint32_t>(length);
  auto* heap = memory->LookupHeap(addr);
  if (!heap || memory->LookupHeap(addr + len - 1) != heap) {
    return ToJava(env, "");
  }
  if (heap->QueryRangeAccess(addr, addr + len - 1) ==
      xe::memory::PageAccess::kNoAccess) {
    return ToJava(env, "");
  }
  const uint8_t* p = memory->TranslateVirtual<const uint8_t*>(addr);
  std::string hex;
  hex.resize(len * 2);
  static const char kDigits[] = "0123456789abcdef";
  for (uint32_t i = 0; i < len; ++i) {
    hex[i * 2] = kDigits[p[i] >> 4];
    hex[i * 2 + 1] = kDigits[p[i] & 15];
  }
  return ToJava(env, hex);
}

JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeDisasm(
    JNIEnv* env, jclass, jint address, jint count) {
  xe::Emulator* emulator = xe::GetGlobalEmulator();
  if (!emulator || !emulator->memory() || count <= 0 || count > 4096) {
    return ToJava(env, "[]");
  }
  xe::Memory* memory = emulator->memory();
  uint32_t addr = static_cast<uint32_t>(address) & ~3u;
  std::string json = "[";
  xe::StringBuffer str;
  for (int i = 0; i < count; ++i, addr += 4) {
    auto* heap = memory->LookupHeap(addr);
    if (!heap || heap->QueryRangeAccess(addr, addr + 3) ==
                     xe::memory::PageAccess::kNoAccess) {
      break;
    }
    uint32_t code = xe::load_and_swap<uint32_t>(
        memory->TranslateVirtual<const uint8_t*>(addr));
    str.Reset();
    xe::cpu::ppc::DisasmPPC(addr, code, &str);
    if (i) {
      json += ",";
    }
    json += fmt::format("\"{:08X}: {:08X}  {}\"", addr, code,
                        JsonEscape(str.to_string_view()));
  }
  json += "]";
  return ToJava(env, json);
}

JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeGpu(
    JNIEnv* env, jclass) {
  uint64_t count = 0, ms = 0;
  xe::ui::vulkan::VulkanPipelineStatsGet(&count, &ms);
  std::string json = fmt::format(
      "{{\"swap_count\":{},\"pipelines_created\":{},\"pipeline_create_ms\":{}}}",
      xe::ui::vulkan::VulkanPerfCountersGetIssueSwapCount(), count, ms);
  return ToJava(env, json);
}

JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeStall(
    JNIEnv* env, jclass) {
  return ToJava(env, xe::kernel::xboxkrnl::xeSpinlockStallReportJson());
}

JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeCvarGet(
    JNIEnv* env, jclass, jstring name) {
  std::string n = FromJava(env, name);
  if (!cvar::ConfigVars) {
    return ToJava(env, "");
  }
  auto it = cvar::ConfigVars->find(n);
  if (it == cvar::ConfigVars->end()) {
    return ToJava(env, "");
  }
  return ToJava(env, it->second->config_value());
}

JNIEXPORT jboolean JNICALL Java_jp_xenia_emulator_DebugServer_nativeCvarSet(
    JNIEnv* env, jclass, jstring name, jstring value) {
  std::string n = FromJava(env, name);
  std::string v = FromJava(env, value);
  bool applied = cvar::SetCommandVarFromString(n, v);
  XELOGI("debug-api: cvar {}={} -> {}", n, v, applied ? "applied" : "unknown");
  return applied ? JNI_TRUE : JNI_FALSE;
}

}  // extern "C"
