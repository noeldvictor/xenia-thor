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
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <unwind.h>
#include <unistd.h>
#include <signal.h>
#include <dlfcn.h>
#include <dirent.h>

#include <cstdint>
#include <cstdio>
#include <set>
#include <filesystem>
#include <system_error>
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
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/kernel_trap.h"
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

// Host backtraces of every thread of this process, from inside the process
// (2026-09-21): debuggerd needs root, which the adb shell user lacks. A
// realtime signal is sent to each thread; its handler unwinds its own stack
// with _Unwind_Backtrace and hands the program counters back. Frames are
// reported as module + offset (dladdr; most xenia symbols are hidden), and
// the PC symbolizes them against the unstripped libxenia-app.so with
// llvm-symbolizer. This is the hang picture: which host lock or wait each
// guest thread sits in.
namespace {

inline int BacktraceSignal() { return SIGRTMIN + 4; }
constexpr size_t kBacktraceMaxFrames = 48;

struct BacktraceRequest {
  std::atomic<int> state{0};  // 0 idle, 1 armed, 2 done
  uintptr_t pcs[kBacktraceMaxFrames];
  size_t count = 0;
};
BacktraceRequest g_backtrace_request;

_Unwind_Reason_Code BacktraceUnwindCallback(_Unwind_Context* context,
                                             void* arg) {
  auto* req = static_cast<BacktraceRequest*>(arg);
  if (req->count >= kBacktraceMaxFrames) {
    return _URC_END_OF_STACK;
  }
  uintptr_t pc = _Unwind_GetIP(context);
  if (pc) {
    req->pcs[req->count++] = pc;
  }
  return _URC_NO_REASON;
}

void BacktraceSignalHandler(int, siginfo_t*, void*) {
  BacktraceRequest* req = &g_backtrace_request;
  if (req->state.load(std::memory_order_acquire) != 1) {
    return;
  }
  req->count = 0;
  _Unwind_Backtrace(BacktraceUnwindCallback, req);
  req->state.store(2, std::memory_order_release);
}

bool EnsureBacktraceHandler() {
  static bool installed = false;
  if (installed) {
    return true;
  }
  struct sigaction action = {};
  action.sa_sigaction = BacktraceSignalHandler;
  action.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&action.sa_mask);
  if (sigaction(BacktraceSignal(), &action, nullptr) != 0) {
    return false;
  }
  installed = true;
  return true;
}

std::string ReadSmallFile(const std::string& path) {
  std::string out;
  FILE* f = fopen(path.c_str(), "r");
  if (!f) {
    return out;
  }
  char buf[256];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
    out.append(buf, n);
  }
  fclose(f);
  while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) {
    out.pop_back();
  }
  return out;
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
  std::set<uint32_t> seen;  // two handles on one thread gave two rows
  for (auto& t : threads) {
    if (!seen.insert(t->thread_id()).second) {
      continue;
    }
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
        "\"r1\":\"{:08X}\",\"r3\":\"{:016X}\",\"r13\":\"{:08X}\"",
        t->thread_id(), JsonEscape(t->thread_name()), host_tid,
        t->is_guest_thread() ? "true" : "false",
        t->is_running() ? "true" : "false", state, wait_reason,
        static_cast<uint32_t>(lr), static_cast<uint32_t>(r1), r3,
        static_cast<uint32_t>(r13));
    // The guest chain and the text on the stack: the thread that queued a
    // failing request blocks on it with the request name in its frames
    // (Banjo dirty-disc, 2026-09-21).
    if (t->is_guest_thread() && r1 >= 0x10000 && r1 < 0x8C000000) {
      json += ",\"chain\":" + xe::kernel::GuestChainJson(
                                    static_cast<uint32_t>(r1),
                                    static_cast<uint32_t>(lr), 12);
      json += ",\"stack_text\":" + xe::kernel::GuestStackTextJson(
                                         static_cast<uint32_t>(r1), 2048, 12);
    }
    json += "}";
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

// One GPU frame trace (.xtr) of the next frame into a directory. The file
// is <title>_<frame>.xtr; the PC replays it with xenia-gpu-vulkan-trace-dump
// on a desktop GPU, so a device-only glitch splits into "the command stream"
// and "the device's execution of it" in one round trip (2026-09-21).
JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeTraceFrame(
    JNIEnv* env, jclass, jstring dir) {
  xe::Emulator* emulator = xe::GetGlobalEmulator();
  if (!emulator || !emulator->graphics_system() ||
      !emulator->graphics_system()->command_processor()) {
    return ToJava(env, "{\"error\":\"no graphics system\"}");
  }
  std::string d = FromJava(env, dir);
  std::error_code ec;
  std::filesystem::create_directories(d, ec);
  uint32_t title_id = emulator->title_id();
  emulator->graphics_system()->command_processor()->RequestFrameTrace(d);
  return ToJava(env, fmt::format(
                         "{{\"requested\":true,\"dir\":\"{}\",\"file_prefix\":\"{:08X}_\","
                         "\"note\":\"the next swap writes <title>_<frame>.xtr; poll the "
                         "directory\"}}",
                         JsonEscape(d), title_id));
}

// A streaming GPU trace: every frame from the next primary buffer until
// off. The last frame before a title stops swapping (a static title screen)
// is the frame on the panel, and only a stream can hold it.
JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeTraceStream(
    JNIEnv* env, jclass, jstring dir, jboolean on) {
  xe::Emulator* emulator = xe::GetGlobalEmulator();
  if (!emulator || !emulator->graphics_system() ||
      !emulator->graphics_system()->command_processor()) {
    return ToJava(env, "{\"error\":\"no graphics system\"}");
  }
  auto* cp = emulator->graphics_system()->command_processor();
  if (on == JNI_TRUE) {
    std::string d = FromJava(env, dir);
    std::error_code ec;
    std::filesystem::create_directories(d, ec);
    cp->BeginTracing(d);
    return ToJava(env, fmt::format("{{\"streaming\":true,\"dir\":\"{}\"}}",
                                   JsonEscape(d)));
  }
  cp->EndTracing();
  return ToJava(env, "{\"streaming\":false}");
}


JNIEXPORT jstring JNICALL
Java_jp_xenia_emulator_DebugServer_nativeHostBacktraces(JNIEnv* env, jclass) {
  if (!EnsureBacktraceHandler()) {
    return ToJava(env, "{\"error\":\"sigaction failed\"}");
  }
  pid_t pid = getpid();
  pid_t self = gettid();
  std::vector<pid_t> tids;
  if (DIR* dir = opendir("/proc/self/task")) {
    while (dirent* entry = readdir(dir)) {
      if (entry->d_name[0] >= '0' && entry->d_name[0] <= '9') {
        tids.push_back(pid_t(atoi(entry->d_name)));
      }
    }
    closedir(dir);
  }
  std::string json = "{\"pid\":" + std::to_string(pid) + ",\"threads\":[";
  bool first = true;
  for (pid_t tid : tids) {
    if (tid == self) {
      continue;  // the debug server's own client thread
    }
    std::string comm =
        ReadSmallFile("/proc/self/task/" + std::to_string(tid) + "/comm");
    std::string wchan =
        ReadSmallFile("/proc/self/task/" + std::to_string(tid) + "/wchan");
    BacktraceRequest* req = &g_backtrace_request;
    req->count = 0;
    req->state.store(1, std::memory_order_release);
    bool got = false;
    if (tgkill(pid, tid, BacktraceSignal()) == 0) {
      for (int i = 0; i < 200; ++i) {  // up to 200 ms per thread
        if (req->state.load(std::memory_order_acquire) == 2) {
          got = true;
          break;
        }
        usleep(1000);
      }
    }
    req->state.store(0, std::memory_order_release);
    if (!first) {
      json += ",";
    }
    first = false;
    json += fmt::format(
        "{{\"tid\":{},\"comm\":\"{}\",\"wchan\":\"{}\",\"frames\":[", tid,
        JsonEscape(comm), JsonEscape(wchan));
    if (got) {
      for (size_t i = 0; i < req->count; ++i) {
        Dl_info info = {};
        uintptr_t pc = req->pcs[i];
        const char* module = "";
        uintptr_t offset = pc;
        std::string symbol;
        if (dladdr(reinterpret_cast<void*>(pc), &info) && info.dli_fname) {
          const char* slash = strrchr(info.dli_fname, '/');
          module = slash ? slash + 1 : info.dli_fname;
          offset = pc - reinterpret_cast<uintptr_t>(info.dli_fbase);
          if (info.dli_sname) {
            symbol = fmt::format(",\"symbol\":\"{}\"", JsonEscape(info.dli_sname));
          }
        }
        json += fmt::format(
            "{}{{\"pc\":\"{:x}\",\"module\":\"{}\",\"offset\":\"{:x}\"{}}}",
            i ? "," : "", pc, JsonEscape(module), offset, symbol);
      }
    }
    json += "]}";
  }
  json += "]}";
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

// The export trap: arm (name, pause), report, release, clear.
JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeTrapSet(
    JNIEnv* env, jclass, jstring name, jboolean pause, jint lr, jstring dump,
    jstring r3) {
  std::string n = FromJava(env, name);
  xe::kernel::KernelTrapSetDump(FromJava(env, dump));
  std::string r3s = FromJava(env, r3);
  xe::kernel::KernelTrapSetR3Filter(
      !r3s.empty(), r3s.empty() ? 0u : uint32_t(std::strtoul(r3s.c_str(), nullptr, 16)));
  std::string err = xe::kernel::KernelTrapSet(n, pause == JNI_TRUE,
                                              static_cast<uint32_t>(lr));
  return ToJava(env, err.empty() ? "{\"armed\":true}"
                                 : "{\"armed\":false,\"reason\":\"" +
                                       JsonEscape(err) + "\"}");
}

JNIEXPORT jstring JNICALL Java_jp_xenia_emulator_DebugServer_nativeTrapReport(
    JNIEnv* env, jclass) {
  return ToJava(env, xe::kernel::KernelTrapReportJson());
}

JNIEXPORT void JNICALL Java_jp_xenia_emulator_DebugServer_nativeTrapRelease(
    JNIEnv*, jclass) {
  xe::kernel::KernelTrapRelease();
}

JNIEXPORT void JNICALL Java_jp_xenia_emulator_DebugServer_nativeTrapClear(
    JNIEnv*, jclass) {
  xe::kernel::KernelTrapClear();
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
  // The live value: config_value() is the config file's (it said
  // cpu_backend_llvm=true while a launch override had it off, 2026-09-22).
  return ToJava(env, it->second->current_value_string());
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
