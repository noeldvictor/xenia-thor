/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/emulator.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <string_view>

#include "config.h"
#include "third_party/fmt/include/fmt/format.h"
#include "xenia/apu/audio_system.h"
#include "xenia/base/assert.h"
#include "xenia/base/byte_stream.h"
#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/debugging.h"
#include "xenia/base/exception_handler.h"
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/mapped_memory.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"
#include "xenia/cpu/backend/code_cache.h"
#include "xenia/cpu/backend/null_backend.h"
#include "xenia/cpu/cpu_flags.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/hid/input_driver.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/gameinfo_utils.h"
#include "xenia/kernel/util/xdbf_utils.h"
#include "xenia/kernel/xam/xam_module.h"
#include "xenia/kernel/xbdm/xbdm_module.h"
#include "xenia/kernel/xboxkrnl/xboxkrnl_module.h"
#include "xenia/memory.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/vfs/devices/disc_image_device.h"
#include "xenia/vfs/devices/host_path_device.h"
#include "xenia/vfs/devices/null_device.h"
#include "xenia/vfs/devices/stfs_container_device.h"
#include "xenia/vfs/virtual_file_system.h"

#if XE_ARCH_ARM64
#include "xenia/cpu/backend/arm64/arm64_backend.h"
#endif  // XE_ARCH_ARM64
#if XE_ARCH_AMD64
#include "xenia/cpu/backend/x64/x64_backend.h"
#endif  // XE_ARCH_AMD64

DECLARE_int32(user_language);

DEFINE_double(time_scalar, 1.0,
              "Scalar used to speed or slow time (1x, 2x, 1/2x, etc).",
              "General");
DEFINE_string(
    launch_module, "",
    "Executable to launch from the .iso or the package instead of default.xex "
    "or the module specified by the game. Leave blank to launch the default "
    "module.",
    "General");

namespace xe {

using namespace xe::literals;

namespace {

bool TryParsePpcDisasmAddress(std::string_view line, uint32_t* out_address) {
  if (line.size() < 8) {
    return false;
  }

  uint32_t value = 0;
  for (size_t i = 0; i < 8; ++i) {
    char c = line[i];
    uint32_t digit = 0;
    if (c >= '0' && c <= '9') {
      digit = uint32_t(c - '0');
    } else if (c >= 'A' && c <= 'F') {
      digit = uint32_t(c - 'A' + 10);
    } else if (c >= 'a' && c <= 'f') {
      digit = uint32_t(c - 'a' + 10);
    } else {
      return false;
    }
    value = (value << 4) | digit;
  }

  *out_address = value;
  return true;
}

bool TryParsePpcDisasmCodeLine(std::string_view line, uint32_t* out_address,
                               uint32_t* out_code) {
  if (!TryParsePpcDisasmAddress(line, out_address)) {
    return false;
  }

  size_t code_start = 8;
  while (code_start < line.size() && line[code_start] == ' ') {
    ++code_start;
  }
  if (code_start + 8 > line.size()) {
    return false;
  }

  uint32_t value = 0;
  for (size_t i = 0; i < 8; ++i) {
    char c = line[code_start + i];
    uint32_t digit = 0;
    if (c >= '0' && c <= '9') {
      digit = uint32_t(c - '0');
    } else if (c >= 'A' && c <= 'F') {
      digit = uint32_t(c - 'A' + 10);
    } else if (c >= 'a' && c <= 'f') {
      digit = uint32_t(c - 'a' + 10);
    } else {
      return false;
    }
    value = (value << 4) | digit;
  }

  *out_code = value;
  return true;
}

int32_t PpcSimm16(uint32_t code) { return static_cast<int16_t>(code); }

struct KnownPpcRegister {
  bool known = false;
  uint32_t value = 0;
};

using KnownPpcRegisters = std::array<KnownPpcRegister, 32>;

void ResetKnownPpcRegisters(KnownPpcRegisters* registers) {
  for (auto& reg : *registers) {
    reg = {};
  }
}

void SetKnownPpcRegister(KnownPpcRegisters* registers, uint32_t reg,
                         uint32_t value) {
  if (reg < registers->size()) {
    (*registers)[reg] = {true, value};
  }
}

void InvalidateKnownPpcRegister(KnownPpcRegisters* registers, uint32_t reg) {
  if (reg < registers->size()) {
    (*registers)[reg] = {};
  }
}

bool TryGetPpcBaseRegister(const KnownPpcRegisters& registers, uint32_t reg,
                           uint32_t* out_value) {
  if (reg == 0) {
    *out_value = 0;
    return true;
  }
  if (reg < registers.size() && registers[reg].known) {
    *out_value = registers[reg].value;
    return true;
  }
  return false;
}

struct PpcMemoryOpInfo {
  const char* name = nullptr;
  bool store = false;
  bool update = false;
};

bool GetPpcDFormMemoryOpInfo(uint32_t opcode, PpcMemoryOpInfo* out_info) {
  switch (opcode) {
    case 32:
      *out_info = {"lwz", false, false};
      return true;
    case 33:
      *out_info = {"lwzu", false, true};
      return true;
    case 34:
      *out_info = {"lbz", false, false};
      return true;
    case 35:
      *out_info = {"lbzu", false, true};
      return true;
    case 36:
      *out_info = {"stw", true, false};
      return true;
    case 37:
      *out_info = {"stwu", true, true};
      return true;
    case 38:
      *out_info = {"stb", true, false};
      return true;
    case 39:
      *out_info = {"stbu", true, true};
      return true;
    case 40:
      *out_info = {"lhz", false, false};
      return true;
    case 41:
      *out_info = {"lhzu", false, true};
      return true;
    case 42:
      *out_info = {"lha", false, false};
      return true;
    case 43:
      *out_info = {"lhau", false, true};
      return true;
    case 44:
      *out_info = {"sth", true, false};
      return true;
    case 45:
      *out_info = {"sthu", true, true};
      return true;
    case 46:
      *out_info = {"lmw", false, false};
      return true;
    case 47:
      *out_info = {"stmw", true, false};
      return true;
    case 48:
      *out_info = {"lfs", false, false};
      return true;
    case 49:
      *out_info = {"lfsu", false, true};
      return true;
    case 50:
      *out_info = {"lfd", false, false};
      return true;
    case 51:
      *out_info = {"lfdu", false, true};
      return true;
    case 52:
      *out_info = {"stfs", true, false};
      return true;
    case 53:
      *out_info = {"stfsu", true, true};
      return true;
    case 54:
      *out_info = {"stfd", true, false};
      return true;
    case 55:
      *out_info = {"stfdu", true, true};
      return true;
    default:
      return false;
  }
}

bool GetPpcDSFormMemoryOpInfo(uint32_t opcode, uint32_t code,
                              PpcMemoryOpInfo* out_info,
                              int32_t* out_displacement) {
  if (opcode != 58 && opcode != 62) {
    return false;
  }

  const uint32_t xo = code & 0x3;
  *out_displacement = static_cast<int16_t>(code & 0xFFFC);
  if (opcode == 58) {
    switch (xo) {
      case 0:
        *out_info = {"ld", false, false};
        return true;
      case 1:
        *out_info = {"ldu", false, true};
        return true;
      case 2:
        *out_info = {"lwa", false, false};
        return true;
      default:
        return false;
    }
  }

  switch (xo) {
    case 0:
      *out_info = {"std", true, false};
      return true;
    case 1:
      *out_info = {"stdu", true, true};
      return true;
    default:
      return false;
  }
}

void LogPpcGlobalReference(cpu::GuestFunction* function, uint32_t target,
                           uint32_t address, std::string_view line,
                           const PpcMemoryOpInfo& op_info) {
  XELOGE("PPC global ref 0x{:08X}: {} {} fn 0x{:08X}-0x{:08X} at 0x{:08X}: {}",
         target, op_info.store ? "store" : "load", op_info.name,
         function->address(), function->end_address(), address, line);
}

void ScanPpcGlobalReferencesInFunction(cpu::GuestFunction* function,
                                       uint32_t target,
                                       size_t max_matches,
                                       size_t* match_count,
                                       size_t* store_count,
                                       size_t* load_count) {
  if (!function || !function->debug_info() ||
      !function->debug_info()->source_disasm()) {
    return;
  }

  KnownPpcRegisters registers = {};
  std::string_view text(function->debug_info()->source_disasm());
  size_t start = 0;
  while (start <= text.size() && *match_count < max_matches) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    std::string_view line = text.substr(start, end - start);

    uint32_t address = 0;
    uint32_t code = 0;
    if (!TryParsePpcDisasmCodeLine(line, &address, &code)) {
      if (line.find("loc_") != std::string_view::npos) {
        ResetKnownPpcRegisters(&registers);
      }
      if (end == text.size()) {
        break;
      }
      start = end + 1;
      continue;
    }

    const uint32_t opcode = code >> 26;
    const uint32_t rt = (code >> 21) & 0x1F;
    const uint32_t ra = (code >> 16) & 0x1F;
    const uint32_t rb = (code >> 11) & 0x1F;

    if (opcode == 15) {
      uint32_t base = 0;
      if (TryGetPpcBaseRegister(registers, ra, &base)) {
        SetKnownPpcRegister(
            &registers, rt,
            base + (static_cast<uint32_t>(PpcSimm16(code)) << 16));
      } else {
        InvalidateKnownPpcRegister(&registers, rt);
      }
    } else if (opcode == 14) {
      uint32_t base = 0;
      if (TryGetPpcBaseRegister(registers, ra, &base)) {
        SetKnownPpcRegister(&registers, rt,
                            base + static_cast<uint32_t>(PpcSimm16(code)));
      } else {
        InvalidateKnownPpcRegister(&registers, rt);
      }
    } else if (opcode == 24) {
      const uint32_t rs = rt;
      if (rs < registers.size() && registers[rs].known) {
        SetKnownPpcRegister(&registers, ra,
                            registers[rs].value | (code & 0xFFFF));
      } else {
        InvalidateKnownPpcRegister(&registers, ra);
      }
    } else if (opcode == 31 && ((code >> 1) & 0x3FF) == 444) {
      const uint32_t rs = rt;
      if (rs < registers.size() && rb < registers.size() &&
          registers[rs].known && registers[rb].known) {
        SetKnownPpcRegister(&registers, ra,
                            registers[rs].value | registers[rb].value);
      } else {
        InvalidateKnownPpcRegister(&registers, ra);
      }
    } else {
      PpcMemoryOpInfo op_info = {};
      int32_t displacement = PpcSimm16(code);
      bool is_memory_op = GetPpcDFormMemoryOpInfo(opcode, &op_info);
      if (!is_memory_op) {
        is_memory_op =
            GetPpcDSFormMemoryOpInfo(opcode, code, &op_info, &displacement);
      }

      if (is_memory_op) {
        uint32_t base = 0;
        if (TryGetPpcBaseRegister(registers, ra, &base)) {
          const uint32_t effective_address =
              base + static_cast<uint32_t>(displacement);
          if (effective_address == target) {
            LogPpcGlobalReference(function, target, address, line, op_info);
            ++*match_count;
            if (op_info.store) {
              ++*store_count;
            } else {
              ++*load_count;
            }
          }
        }
        if (!op_info.store) {
          InvalidateKnownPpcRegister(&registers, rt);
        }
        if (op_info.update) {
          InvalidateKnownPpcRegister(&registers, ra);
        }
      }
    }

    if (end == text.size()) {
      break;
    }
    start = end + 1;
  }
}

void LogTranslatedPpcGlobalReferenceSearch(cpu::Processor* processor,
                                           uint32_t target,
                                           size_t max_matches = 80) {
  if (!processor) {
    return;
  }

  size_t match_count = 0;
  size_t store_count = 0;
  size_t load_count = 0;
  for (auto module : processor->GetModules()) {
    if (!module || match_count >= max_matches) {
      continue;
    }
    module->ForEachFunction([&](cpu::Function* function) {
      if (match_count >= max_matches || !function || !function->is_guest()) {
        return;
      }
      ScanPpcGlobalReferencesInFunction(
          static_cast<cpu::GuestFunction*>(function), target, max_matches,
          &match_count, &store_count, &load_count);
    });
  }

  XELOGE(
      "PPC global ref search 0x{:08X}: {} translated direct refs, {} loads, "
      "{} stores",
      target, match_count, load_count, store_count);
  if (match_count >= max_matches) {
    XELOGE("PPC global ref search 0x{:08X}: stopped after {} matches", target,
           match_count);
  }
}

void LogPpcDisasmWindow(const char* source_disasm, uint32_t guest_pc) {
  if (!source_disasm) {
    return;
  }

  const uint32_t window_start = guest_pc > 0x80 ? guest_pc - 0x80 : 0;
  const uint32_t window_end = guest_pc + 0x80;
  std::string_view text(source_disasm);
  XELOGE("PPC disassembly window around 0x{:08X}:", guest_pc);

  size_t start = 0;
  bool emitted_line = false;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    std::string_view line = text.substr(start, end - start);
    uint32_t line_address = 0;
    if (TryParsePpcDisasmAddress(line, &line_address) &&
        line_address >= window_start && line_address <= window_end) {
      XELOGE("{}", line);
      emitted_line = true;
    }
    if (end == text.size()) {
      break;
    }
    start = end + 1;
  }

  if (!emitted_line) {
    XELOGE("No PPC disassembly lines were available for crash window.");
  }
}

void LogResolvedPpcDisasmWindow(cpu::Processor* processor, uint32_t address,
                                const char* label) {
  if (!processor || !address) {
    return;
  }

  auto functions = processor->FindFunctionsWithAddress(address);
  if (functions.empty()) {
    auto function = processor->QueryFunction(address);
    if (!function) {
      function = processor->ResolveFunction(address);
    }
    if (function) {
      functions.push_back(function);
    }
  }

  if (functions.empty()) {
    XELOGE("{} PPC disassembly: no translated function contains 0x{:08X}",
           label, address);
    return;
  }

  for (auto function : functions) {
    if (!function || !function->is_guest()) {
      continue;
    }
    auto guest_function = static_cast<cpu::GuestFunction*>(function);
    XELOGE("{} function: 0x{:08X}-0x{:08X}", label,
           guest_function->address(), guest_function->end_address());
    if (guest_function->debug_info()) {
      LogPpcDisasmWindow(guest_function->debug_info()->source_disasm(),
                         address);
    } else {
      XELOGE("{} PPC disassembly: debug info unavailable for 0x{:08X}", label,
             address);
    }
  }
}

bool TryLogGuestWord(Memory* memory, uint32_t address, const char* label,
                     uint32_t* out_value = nullptr) {
  if (!memory) {
    return false;
  }

  const auto heap = memory->LookupHeap(address);
  uint32_t protect = 0;
  if (!heap || !heap->QueryProtect(address, &protect) ||
      !(protect & kMemoryProtectRead)) {
    XELOGE("Crash probe {} 0x{:08X}: unreadable", label, address);
    return false;
  }

  const uint32_t value =
      xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
  XELOGE("Crash probe {} 0x{:08X}: 0x{:08X}", label, address, value);
  if (out_value) {
    *out_value = value;
  }
  return true;
}

void LogBlueDragonThunkProbe(Memory* memory, uint32_t guest_pc) {
  if (guest_pc < 0x826A23C8 || guest_pc > 0x826A23F4) {
    return;
  }

  uint32_t object_ptr = 0;
  if (!TryLogGuestWord(memory, 0x82785548, "Blue Dragon thunk global",
                       &object_ptr) ||
      !object_ptr) {
    return;
  }

  uint32_t vtable_ptr = 0;
  if (!TryLogGuestWord(memory, object_ptr, "Blue Dragon thunk object.vtable",
                       &vtable_ptr) ||
      !vtable_ptr) {
    return;
  }
  TryLogGuestWord(memory, vtable_ptr + 0x14,
                  "Blue Dragon thunk vtable[0x14]");
}

void LogGuestStackWords(Memory* memory, uint64_t stack_pointer) {
  if (!memory) {
    return;
  }
  const uint32_t stack_address = uint32_t(stack_pointer);
  if (!stack_address) {
    return;
  }

  XELOGE("Stack words near r1=0x{:08X}:", stack_address);
  for (uint32_t offset = 0; offset < 0x40; offset += 4) {
    const uint32_t address = stack_address + offset;
    const auto heap = memory->LookupHeap(address);
    uint32_t protect = 0;
    if (!heap || !heap->QueryProtect(address, &protect) ||
        !(protect & kMemoryProtectRead)) {
      XELOGE("  [0x{:08X}] unreadable", address);
      continue;
    }

    const uint32_t value =
        xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
    XELOGE("  [0x{:08X}] 0x{:08X}", address, value);
  }
}

bool LooksLikeGuestCodeAddress(uint32_t value) {
  return value >= 0x82000000 && value < 0x83000000;
}

void LogGuestStackCodeWindows(cpu::Processor* processor, Memory* memory,
                              uint64_t stack_pointer) {
  if (!processor || !memory) {
    return;
  }
  const uint32_t stack_address = uint32_t(stack_pointer);
  if (!stack_address) {
    return;
  }

  for (uint32_t offset = 0; offset < 0x40; offset += 4) {
    const uint32_t address = stack_address + offset;
    const auto heap = memory->LookupHeap(address);
    uint32_t protect = 0;
    if (!heap || !heap->QueryProtect(address, &protect) ||
        !(protect & kMemoryProtectRead)) {
      continue;
    }

    const uint32_t value =
        xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
    if (LooksLikeGuestCodeAddress(value)) {
      std::string label = fmt::format("Stack return [0x{:08X}]", address);
      LogResolvedPpcDisasmWindow(processor, value, label.c_str());
    }
  }
}

}  // namespace

Emulator::GameConfigLoadCallback::GameConfigLoadCallback(Emulator& emulator)
    : emulator_(emulator) {
  emulator_.AddGameConfigLoadCallback(this);
}

Emulator::GameConfigLoadCallback::~GameConfigLoadCallback() {
  emulator_.RemoveGameConfigLoadCallback(this);
}

Emulator::Emulator(const std::filesystem::path& command_line,
                   const std::filesystem::path& storage_root,
                   const std::filesystem::path& content_root,
                   const std::filesystem::path& cache_root)
    : on_launch(),
      on_terminate(),
      on_exit(),
      command_line_(command_line),
      storage_root_(storage_root),
      content_root_(content_root),
      cache_root_(cache_root),
      title_name_(),
      title_version_(),
      display_window_(nullptr),
      memory_(),
      audio_system_(),
      graphics_system_(),
      input_system_(),
      export_resolver_(),
      file_system_(),
      kernel_state_(),
      main_thread_(),
      title_id_(std::nullopt),
      paused_(false),
      restoring_(false),
      restore_fence_() {}

Emulator::~Emulator() {
  // Note that we delete things in the reverse order they were initialized.

  // Give the systems time to shutdown before we delete them.
  if (graphics_system_) {
    graphics_system_->Shutdown();
  }
  if (audio_system_) {
    audio_system_->Shutdown();
  }

  input_system_.reset();
  graphics_system_.reset();
  audio_system_.reset();

  kernel_state_.reset();
  file_system_.reset();

  processor_.reset();

  export_resolver_.reset();

  ExceptionHandler::Uninstall(Emulator::ExceptionCallbackThunk, this);
}

X_STATUS Emulator::Setup(
    ui::Window* display_window, ui::ImGuiDrawer* imgui_drawer,
    bool require_cpu_backend,
    std::function<std::unique_ptr<apu::AudioSystem>(cpu::Processor*)>
        audio_system_factory,
    std::function<std::unique_ptr<gpu::GraphicsSystem>()>
        graphics_system_factory,
    std::function<std::vector<std::unique_ptr<hid::InputDriver>>(ui::Window*)>
        input_driver_factory) {
  X_STATUS result = X_STATUS_UNSUCCESSFUL;

  display_window_ = display_window;
  imgui_drawer_ = imgui_drawer;

  // Initialize clock.
  // 360 uses a 50MHz clock.
  Clock::set_guest_tick_frequency(50000000);
  // We could reset this with save state data/constant value to help replays.
  Clock::set_guest_system_time_base(Clock::QueryHostSystemTime());
  // This can be adjusted dynamically, as well.
  Clock::set_guest_time_scalar(cvars::time_scalar);

  // Before we can set thread affinity we must enable the process to use all
  // logical processors.
  xe::threading::EnableAffinityConfiguration();

  // Create memory system first, as it is required for other systems.
  memory_ = std::make_unique<Memory>();
  if (!memory_->Initialize()) {
    return false;
  }

  // Shared export resolver used to attach and query for HLE exports.
  export_resolver_ = std::make_unique<xe::cpu::ExportResolver>();

  std::unique_ptr<xe::cpu::backend::Backend> backend;
#if XE_ARCH_AMD64
  if (cvars::cpu == "x64") {
    backend.reset(new xe::cpu::backend::x64::X64Backend());
  }
#endif  // XE_ARCH_AMD64
#if XE_ARCH_ARM64
  if (cvars::cpu == "arm64") {
    backend.reset(new xe::cpu::backend::arm64::Arm64Backend());
  }
#endif  // XE_ARCH_ARM64
  if (cvars::cpu == "any") {
    if (!backend) {
#if XE_ARCH_AMD64
      backend.reset(new xe::cpu::backend::x64::X64Backend());
#elif XE_ARCH_ARM64
      backend.reset(new xe::cpu::backend::arm64::Arm64Backend());
#endif  // XE_ARCH_AMD64 || XE_ARCH_ARM64
    }
  }
  if (!backend && !require_cpu_backend) {
    backend.reset(new xe::cpu::backend::NullBackend());
  }

  // Initialize the CPU.
  processor_ = std::make_unique<xe::cpu::Processor>(memory_.get(),
                                                    export_resolver_.get());
  if (!processor_->Setup(std::move(backend))) {
    return X_STATUS_UNSUCCESSFUL;
  }

  // Initialize the APU.
  if (audio_system_factory) {
    audio_system_ = audio_system_factory(processor_.get());
    if (!audio_system_) {
      return X_STATUS_NOT_IMPLEMENTED;
    }
  }

  // Initialize the GPU.
  graphics_system_ = graphics_system_factory();
  if (!graphics_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }

  // Initialize the HID.
  input_system_ = std::make_unique<xe::hid::InputSystem>(display_window_);
  if (!input_system_) {
    return X_STATUS_NOT_IMPLEMENTED;
  }
  if (input_driver_factory) {
    auto input_drivers = input_driver_factory(display_window_);
    for (size_t i = 0; i < input_drivers.size(); ++i) {
      auto& input_driver = input_drivers[i];
      input_driver->set_is_active_callback(
          []() -> bool { return !xe::kernel::xam::xeXamIsUIActive(); });
      input_system_->AddDriver(std::move(input_driver));
    }
  }

  result = input_system_->Setup();
  if (result) {
    return result;
  }

  // Bring up the virtual filesystem used by the kernel.
  file_system_ = std::make_unique<xe::vfs::VirtualFileSystem>();

  // Shared kernel state.
  kernel_state_ = std::make_unique<xe::kernel::KernelState>(this);

  // Setup the core components.
  result = graphics_system_->Setup(
      processor_.get(), kernel_state_.get(),
      display_window_ ? &display_window_->app_context() : nullptr,
      display_window_ != nullptr);
  if (result) {
    return result;
  }

  if (audio_system_) {
    result = audio_system_->Setup(kernel_state_.get());
    if (result) {
      return result;
    }
  }

#define LOAD_KERNEL_MODULE(t) \
  static_cast<void>(kernel_state_->LoadKernelModule<kernel::t>())
  // HLE kernel modules.
  LOAD_KERNEL_MODULE(xboxkrnl::XboxkrnlModule);
  LOAD_KERNEL_MODULE(xam::XamModule);
  LOAD_KERNEL_MODULE(xbdm::XbdmModule);
#undef LOAD_KERNEL_MODULE

  // Initialize emulator fallback exception handling last.
  ExceptionHandler::Install(Emulator::ExceptionCallbackThunk, this);

  return result;
}

X_STATUS Emulator::TerminateTitle() {
  if (!is_title_open()) {
    return X_STATUS_UNSUCCESSFUL;
  }

  kernel_state_->TerminateTitle();
  title_id_ = std::nullopt;
  title_name_ = "";
  title_version_ = "";
  on_terminate();
  return X_STATUS_SUCCESS;
}

X_STATUS Emulator::LaunchPath(const std::filesystem::path& path) {
  // Launch based on file type.
  // This is a silly guess based on file extension.
  if (!path.has_extension()) {
    // Likely an STFS container.
    return LaunchStfsContainer(path);
  };
  auto extension = xe::utf8::lower_ascii(xe::path_to_utf8(path.extension()));
  if (extension == ".xex" || extension == ".elf" || extension == ".exe") {
    // Treat as a naked xex file.
    return LaunchXexFile(path);
  } else {
    // Assume a disc image.
    return LaunchDiscImage(path);
  }
}

X_STATUS Emulator::LaunchXexFile(const std::filesystem::path& path) {
  // We create a virtual filesystem pointing to its directory and symlink
  // that to the game filesystem.
  // e.g., /my/files/foo.xex will get a local fs at:
  // \\Device\\Harddisk0\\Partition1
  // and then get that symlinked to game:\, so
  // -> game:\foo.xex

  auto mount_path = "\\Device\\Harddisk0\\Partition1";

  // Register the local directory in the virtual filesystem.
  auto parent_path = path.parent_path();
  auto device =
      std::make_unique<vfs::HostPathDevice>(mount_path, parent_path, true);
  if (!device->Initialize()) {
    XELOGE("Unable to scan host path");
    return X_STATUS_NO_SUCH_FILE;
  }
  if (!file_system_->RegisterDevice(std::move(device))) {
    XELOGE("Unable to register host path");
    return X_STATUS_NO_SUCH_FILE;
  }

  // Create symlinks to the device.
  file_system_->RegisterSymbolicLink("game:", mount_path);
  file_system_->RegisterSymbolicLink("d:", mount_path);

  // Get just the filename (foo.xex).
  auto file_name = path.filename();

  // Launch the game.
  auto fs_path = "game:\\" + xe::path_to_utf8(file_name);
  return CompleteLaunch(path, fs_path);
}

X_STATUS Emulator::LaunchDiscImage(const std::filesystem::path& path) {
  auto mount_path = "\\Device\\Cdrom0";

  // Register the disc image in the virtual filesystem.
  auto device = std::make_unique<vfs::DiscImageDevice>(mount_path, path);
  if (!device->Initialize()) {
    xe::FatalError("Unable to mount disc image; file not found or corrupt.");
    return X_STATUS_NO_SUCH_FILE;
  }
  if (!file_system_->RegisterDevice(std::move(device))) {
    xe::FatalError("Unable to register disc image.");
    return X_STATUS_NO_SUCH_FILE;
  }

  // Create symlinks to the device.
  file_system_->RegisterSymbolicLink("game:", mount_path);
  file_system_->RegisterSymbolicLink("d:", mount_path);

  // Launch the game.
  auto module_path(FindLaunchModule());
  return CompleteLaunch(path, module_path);
}

X_STATUS Emulator::LaunchStfsContainer(const std::filesystem::path& path) {
  auto mount_path = "\\Device\\Cdrom0";

  // Register the container in the virtual filesystem.
  auto device = std::make_unique<vfs::StfsContainerDevice>(mount_path, path);
  if (!device->Initialize()) {
    xe::FatalError(
        "Unable to mount STFS container; file not found or corrupt.");
    return X_STATUS_NO_SUCH_FILE;
  }
  if (!file_system_->RegisterDevice(std::move(device))) {
    xe::FatalError("Unable to register STFS container.");
    return X_STATUS_NO_SUCH_FILE;
  }

  file_system_->RegisterSymbolicLink("game:", mount_path);
  file_system_->RegisterSymbolicLink("d:", mount_path);

  // Launch the game.
  auto module_path(FindLaunchModule());
  return CompleteLaunch(path, module_path);
}

void Emulator::Pause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  // Don't hold the lock on this (so any waits follow through)
  graphics_system_->Pause();
  audio_system_->Pause();

  auto lock = global_critical_region::AcquireDirect();
  auto threads =
      kernel_state()->object_table()->GetObjectsByType<kernel::XThread>(
          kernel::XObject::Type::Thread);
  auto current_thread = kernel::XThread::IsInThread()
                            ? kernel::XThread::GetCurrentThread()
                            : nullptr;
  for (auto thread : threads) {
    // Don't pause ourself or host threads.
    if (thread == current_thread || !thread->can_debugger_suspend()) {
      continue;
    }

    if (thread->is_running()) {
      thread->thread()->Suspend(nullptr);
    }
  }

  XELOGD("! EMULATOR PAUSED !");
}

void Emulator::Resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;
  XELOGD("! EMULATOR RESUMED !");

  graphics_system_->Resume();
  audio_system_->Resume();

  auto threads =
      kernel_state()->object_table()->GetObjectsByType<kernel::XThread>(
          kernel::XObject::Type::Thread);
  for (auto thread : threads) {
    if (!thread->can_debugger_suspend()) {
      // Don't pause host threads.
      continue;
    }

    if (thread->is_running()) {
      thread->thread()->Resume(nullptr);
    }
  }
}

bool Emulator::SaveToFile(const std::filesystem::path& path) {
  Pause();

  filesystem::CreateEmptyFile(path);
  auto map = MappedMemory::Open(path, MappedMemory::Mode::kReadWrite, 0, 2_GiB);
  if (!map) {
    return false;
  }

  // Save the emulator state to a file
  ByteStream stream(map->data(), map->size());
  stream.Write(kEmulatorSaveSignature);
  stream.Write(title_id_.has_value());
  if (title_id_.has_value()) {
    stream.Write(title_id_.value());
  }

  // It's important we don't hold the global lock here! XThreads need to step
  // forward (possibly through guarded regions) without worry!
  processor_->Save(&stream);
  graphics_system_->Save(&stream);
  audio_system_->Save(&stream);
  kernel_state_->Save(&stream);
  memory_->Save(&stream);
  map->Close(stream.offset());

  Resume();
  return true;
}

bool Emulator::RestoreFromFile(const std::filesystem::path& path) {
  // Restore the emulator state from a file
  auto map = MappedMemory::Open(path, MappedMemory::Mode::kReadWrite);
  if (!map) {
    return false;
  }

  restoring_ = true;

  // Terminate any loaded titles.
  Pause();
  kernel_state_->TerminateTitle();

  auto lock = global_critical_region::AcquireDirect();
  ByteStream stream(map->data(), map->size());
  if (stream.Read<uint32_t>() != kEmulatorSaveSignature) {
    return false;
  }

  auto has_title_id = stream.Read<bool>();
  std::optional<uint32_t> title_id;
  if (!has_title_id) {
    title_id = {};
  } else {
    title_id = stream.Read<uint32_t>();
  }
  if (title_id_.has_value() != title_id.has_value() ||
      title_id_.value() != title_id.value()) {
    // Swapping between titles is unsupported at the moment.
    assert_always();
    return false;
  }

  if (!processor_->Restore(&stream)) {
    XELOGE("Could not restore processor!");
    return false;
  }
  if (!graphics_system_->Restore(&stream)) {
    XELOGE("Could not restore graphics system!");
    return false;
  }
  if (!audio_system_->Restore(&stream)) {
    XELOGE("Could not restore audio system!");
    return false;
  }
  if (!kernel_state_->Restore(&stream)) {
    XELOGE("Could not restore kernel state!");
    return false;
  }
  if (!memory_->Restore(&stream)) {
    XELOGE("Could not restore memory!");
    return false;
  }

  // Update the main thread.
  auto threads =
      kernel_state_->object_table()->GetObjectsByType<kernel::XThread>();
  for (auto thread : threads) {
    if (thread->main_thread()) {
      main_thread_ = thread;
      break;
    }
  }

  Resume();

  restore_fence_.Signal();
  restoring_ = false;

  return true;
}

bool Emulator::TitleRequested() {
  auto xam = kernel_state()->GetKernelModule<kernel::xam::XamModule>("xam.xex");
  return xam->loader_data().launch_data_present;
}

void Emulator::LaunchNextTitle() {
  auto xam = kernel_state()->GetKernelModule<kernel::xam::XamModule>("xam.xex");
  auto next_title = xam->loader_data().launch_path;

  CompleteLaunch("", next_title);
}

bool Emulator::ExceptionCallbackThunk(Exception* ex, void* data) {
  return reinterpret_cast<Emulator*>(data)->ExceptionCallback(ex);
}

bool Emulator::ExceptionCallback(Exception* ex) {
  // Check to see if the exception occurred in guest code.
  auto code_cache = processor()->backend()->code_cache();
  auto code_base = code_cache->execute_base_address();
  auto code_end = code_base + code_cache->total_size();

  if (!(ex->pc() >= code_base && ex->pc() < code_end)) {
    // Didn't occur in guest code. Let it pass.
    return false;
  }

  if (!processor()->is_debugger_attached() && debugging::IsDebuggerAttached()) {
    // If Xenia's debugger isn't attached but another one is, pass it to that
    // debugger.
    return false;
  } else if (processor()->is_debugger_attached()) {
    // Let the debugger handle this exception. It may decide to continue past it
    // (if it was a stepping breakpoint, etc).
    return processor()->OnUnhandledException(ex);
  }

  // Within range. Pause the emulator and eat the exception.
  Pause();

  // Dump information into the log.
  auto current_thread = kernel::XThread::GetCurrentThread();
  assert_not_null(current_thread);

  auto guest_function = code_cache->LookupFunction(ex->pc());
  assert_not_null(guest_function);

  auto context = current_thread->thread_state()->context();

  XELOGE("==== CRASH DUMP ====");
  XELOGE("Thread ID (Host: 0x{:08X} / Guest: 0x{:08X})",
         current_thread->thread()->system_id(), current_thread->thread_id());
  XELOGE("Thread Handle: 0x{:08X}", current_thread->handle());
  const uint32_t guest_pc =
      guest_function->MapMachineCodeToGuestAddress(ex->pc());
  const uintptr_t code_offset =
      ex->pc() - reinterpret_cast<uintptr_t>(guest_function->machine_code());
  XELOGE("PC: 0x{:08X}", guest_pc);
  XELOGE("Host PC: 0x{:016X} (function code +0x{:X})", ex->pc(),
         code_offset);
  XELOGE("Function: 0x{:08X}-0x{:08X}", guest_function->address(),
         guest_function->end_address());
  if (guest_function->debug_info()) {
    LogPpcDisasmWindow(guest_function->debug_info()->source_disasm(),
                       guest_pc);
  }
  LogBlueDragonThunkProbe(memory_.get(), guest_pc);
  XELOGE("Special registers: LR=0x{:016X} CTR=0x{:016X} CR=0x{:08X}",
         context->lr, context->ctr, uint32_t(context->cr()));
  LogTranslatedPpcGlobalReferenceSearch(processor(), 0x82785548);
  LogResolvedPpcDisasmWindow(processor(), uint32_t(context->lr), "LR");
  LogResolvedPpcDisasmWindow(processor(), uint32_t(context->ctr), "CTR");
  LogGuestStackWords(memory_.get(), context->r[1]);
  LogGuestStackCodeWindows(processor(), memory_.get(), context->r[1]);
  XELOGE("Registers:");
  for (int i = 0; i < 32; i++) {
    XELOGE(" r{:<3} = {:016X}", i, context->r[i]);
  }
  for (int i = 0; i < 32; i++) {
    XELOGE(" f{:<3} = {:016X} = (double){} = (float){}", i,
           *reinterpret_cast<uint64_t*>(&context->f[i]), context->f[i],
           *(float*)&context->f[i]);
  }
  for (int i = 0; i < 128; i++) {
    XELOGE(" v{:<3} = [0x{:08X}, 0x{:08X}, 0x{:08X}, 0x{:08X}]", i,
           context->v[i].u32[0], context->v[i].u32[1], context->v[i].u32[2],
           context->v[i].u32[3]);
  }

  // Display a dialog telling the user the guest has crashed.
  if (display_window_ && imgui_drawer_) {
    display_window_->app_context().CallInUIThreadSynchronous([this]() {
      xe::ui::ImGuiDialog::ShowMessageBox(
          imgui_drawer_, "Uh-oh!",
          "The guest has crashed.\n\n"
          ""
          "Xenia has now paused itself.\n"
          "A crash dump has been written into the log.");
    });
  }

  // Now suspend ourself (we should be a guest thread).
  current_thread->Suspend(nullptr);

  // We should not arrive here!
  assert_always();
  return false;
}

void Emulator::WaitUntilExit() {
  while (true) {
    if (main_thread_) {
      xe::threading::Wait(main_thread_->thread(), false);
    }

    if (restoring_) {
      restore_fence_.Wait();
    } else {
      // Not restoring and the thread exited. We're finished.
      break;
    }
  }

  on_exit();
}

void Emulator::AddGameConfigLoadCallback(GameConfigLoadCallback* callback) {
  assert_not_null(callback);
  // Game config load callbacks handling is entirely in the UI thread.
  assert_true(!display_window_ ||
              display_window_->app_context().IsInUIThread());
  // Check if already added.
  if (std::find(game_config_load_callbacks_.cbegin(),
                game_config_load_callbacks_.cend(),
                callback) != game_config_load_callbacks_.cend()) {
    return;
  }
  game_config_load_callbacks_.push_back(callback);
}

void Emulator::RemoveGameConfigLoadCallback(GameConfigLoadCallback* callback) {
  assert_not_null(callback);
  // Game config load callbacks handling is entirely in the UI thread.
  assert_true(!display_window_ ||
              display_window_->app_context().IsInUIThread());
  auto it = std::find(game_config_load_callbacks_.cbegin(),
                      game_config_load_callbacks_.cend(), callback);
  if (it == game_config_load_callbacks_.cend()) {
    return;
  }
  if (game_config_load_callback_loop_next_index_ != SIZE_MAX) {
    // Actualize the next callback index after the erasure from the vector.
    size_t existing_index =
        size_t(std::distance(game_config_load_callbacks_.cbegin(), it));
    if (game_config_load_callback_loop_next_index_ > existing_index) {
      --game_config_load_callback_loop_next_index_;
    }
  }
  game_config_load_callbacks_.erase(it);
}

std::string Emulator::FindLaunchModule() {
  std::string path("game:\\");

  if (!cvars::launch_module.empty()) {
    return path + cvars::launch_module;
  }

  std::string default_module("default.xex");

  auto gameinfo_entry(file_system_->ResolvePath(path + "GameInfo.bin"));
  if (gameinfo_entry) {
    vfs::File* file = nullptr;
    X_STATUS result =
        gameinfo_entry->Open(vfs::FileAccess::kGenericRead, &file);
    if (XSUCCEEDED(result)) {
      std::vector<uint8_t> buffer(gameinfo_entry->size());
      size_t bytes_read = 0;
      result = file->ReadSync(buffer.data(), buffer.size(), 0, &bytes_read);
      if (XSUCCEEDED(result)) {
        kernel::util::GameInfo info(buffer);
        if (info.is_valid()) {
          XELOGI("Found virtual title {}", info.virtual_title_id());

          const std::string xna_id("584E07D1");
          auto xna_id_entry(file_system_->ResolvePath(path + xna_id));
          if (xna_id_entry) {
            default_module = xna_id + "\\" + info.module_name();
          } else {
            XELOGE("Could not find fixed XNA path {}", xna_id);
          }
        }
      }
    }
  }

  return path + default_module;
}

static std::string format_version(xex2_version version) {
  // fmt::format doesn't like bit fields
  uint32_t major, minor, build, qfe;
  major = version.major;
  minor = version.minor;
  build = version.build;
  qfe = version.qfe;
  if (qfe) {
    return fmt::format("{}.{}.{}.{}", major, minor, build, qfe);
  }
  if (build) {
    return fmt::format("{}.{}.{}", major, minor, build);
  }
  return fmt::format("{}.{}", major, minor);
}

X_STATUS Emulator::CompleteLaunch(const std::filesystem::path& path,
                                  const std::string_view module_path) {
  // Making changes to the UI (setting the icon) and executing game config load
  // callbacks which expect to be called from the UI thread.
  if (display_window_ && !display_window_->app_context().IsInUIThread()) {
    X_STATUS result = X_STATUS_UNSUCCESSFUL;
    auto path_copy = path;
    auto module_path_copy = std::string(module_path);
    if (!display_window_->app_context().CallInUIThreadSynchronous(
            [this, &result, path_copy, module_path_copy]() {
              result = CompleteLaunch(path_copy, module_path_copy);
            })) {
      return X_STATUS_UNSUCCESSFUL;
    }
    return result;
  }
  assert_true(display_window_->app_context().IsInUIThread());

  // Setup NullDevices for raw HDD partition accesses
  // Cache/STFC code baked into games tries reading/writing to these
  // By using a NullDevice that just returns success to all IO requests it
  // should allow games to believe cache/raw disk was accessed successfully

  // NOTE: this should probably be moved to xenia_main.cc, but right now we need
  // to register the \Device\Harddisk0\ NullDevice _after_ the
  // \Device\Harddisk0\Partition1 HostPathDevice, otherwise requests to
  // Partition1 will go to this. Registering during CompleteLaunch allows us to
  // make sure any HostPathDevices are ready beforehand.
  // (see comment above cache:\ device registration for more info about why)
  auto null_paths = {std::string("\\Partition0"), std::string("\\Cache0"),
                     std::string("\\Cache1")};
  auto null_device =
      std::make_unique<vfs::NullDevice>("\\Device\\Harddisk0", null_paths);
  if (null_device->Initialize()) {
    file_system_->RegisterDevice(std::move(null_device));
  }

  // Reset state.
  title_id_ = std::nullopt;
  title_name_ = "";
  title_version_ = "";
  display_window_->SetIcon(nullptr, 0);

  // Allow xam to request module loads.
  auto xam = kernel_state()->GetKernelModule<kernel::xam::XamModule>("xam.xex");

  XELOGI("Launching module {}", module_path);
  auto module = kernel_state_->LoadUserModule(module_path);
  if (!module) {
    XELOGE("Failed to load user module {}", xe::path_to_utf8(path));
    return X_STATUS_NOT_FOUND;
  }

  // Grab the current title ID.
  xex2_opt_execution_info* info = nullptr;
  module->GetOptHeader(XEX_HEADER_EXECUTION_INFO, &info);

  if (!info) {
    title_id_ = 0;
  } else {
    title_id_ = info->title_id;
    auto title_version = info->version();
    if (title_version.value != 0) {
      title_version_ = format_version(title_version);
    }
  }

  // Try and load the resource database (xex only).
  if (module->title_id()) {
    auto title_id = fmt::format("{:08X}", module->title_id());

    // Load the per-game configuration file and make sure updates are handled by
    // the callbacks.
    config::LoadGameConfig(title_id);
    assert_true(game_config_load_callback_loop_next_index_ == SIZE_MAX);
    game_config_load_callback_loop_next_index_ = 0;
    while (game_config_load_callback_loop_next_index_ <
           game_config_load_callbacks_.size()) {
      game_config_load_callbacks_[game_config_load_callback_loop_next_index_++]
          ->PostGameConfigLoad();
    }
    game_config_load_callback_loop_next_index_ = SIZE_MAX;

    const kernel::util::XdbfGameData db = kernel_state_->module_xdbf(module);
    if (db.is_valid()) {
      XLanguage language =
          db.GetExistingLanguage(static_cast<XLanguage>(cvars::user_language));
      title_name_ = db.title(language);

      XELOGI("-------------------- ACHIEVEMENTS --------------------");
      const std::vector<kernel::util::XdbfAchievementTableEntry>
          achievement_list = db.GetAchievements();
      for (const kernel::util::XdbfAchievementTableEntry& entry :
           achievement_list) {
        std::string label = db.GetStringTableEntry(language, entry.label_id);
        std::string desc =
            db.GetStringTableEntry(language, entry.description_id);

        XELOGI("{} - {} - {} - {}", entry.id, label, desc, entry.gamerscore);
      }
      XELOGI("----------------- END OF ACHIEVEMENTS ----------------");

      auto icon_block = db.icon();
      if (icon_block) {
        display_window_->SetIcon(icon_block.buffer, icon_block.size);
      }
    }
  }

#if XE_PLATFORM_ANDROID
  // The Android ARM64 bring-up currently needs to reach guest CPU translation
  // before spending time in prelaunch GPU cache work.
  XELOGW("Skipping blocking shader storage initialization on Android");
#else
  // Initializing the shader storage in a blocking way so the user doesn't miss
  // the initial seconds - for instance, sound from an intro video may start
  // playing before the video can be seen if doing this in parallel with the
  // main thread.
  on_shader_storage_initialization(true);
  graphics_system_->InitializeShaderStorage(cache_root_, title_id_.value(),
                                            true);
  on_shader_storage_initialization(false);
#endif  // XE_PLATFORM_ANDROID

  auto main_thread = kernel_state_->LaunchModule(module);
  if (!main_thread) {
    return X_STATUS_UNSUCCESSFUL;
  }
  main_thread_ = main_thread;
  on_launch(title_id_.value(), title_name_);

  return X_STATUS_SUCCESS;
}

}  // namespace xe
