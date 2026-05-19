/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/graphics_system.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "xenia/base/byte_stream.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/clock.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/threading.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/ui/graphics_provider.h"
#include "xenia/ui/window.h"
#include "xenia/ui/windowed_app_context.h"

DEFINE_bool(
    store_shaders, true,
    "Store shaders persistently and load them when loading games to avoid "
    "runtime spikes and freezes when playing the game not for the first time.",
    "GPU");

namespace xe {
namespace gpu {

namespace {

std::atomic<int32_t> blue_dragon_wait_token_kick_log_count{0};
std::atomic<int32_t> gpu_interrupt_trace_count{0};
std::atomic<int32_t> gpu_source0_interrupt_trace_count{0};
std::atomic<int32_t> gpu_mark_vblank_trace_count{0};
std::atomic<int32_t> gpu_unknown_register_read_log_count{0};
std::atomic<int32_t> gpu_unknown_register_write_log_count{0};

bool ShouldLogUnknownGpuRegister(std::atomic<int32_t>& counter) {
  int32_t budget = cvars::gpu_unknown_register_log_budget;
  if (budget < 0) {
    return true;
  }
  if (budget == 0) {
    return false;
  }
  return counter.fetch_add(1) < budget;
}

bool ShouldTraceGpuInterrupt(uint32_t source) {
  if (!cvars::gpu_trace_interrupts) {
    return false;
  }
  int32_t budget = cvars::gpu_trace_interrupts_budget;
  if (source == 0) {
    int32_t source0_budget = budget < 0 ? 64 : std::min<int32_t>(budget, 64);
    return gpu_source0_interrupt_trace_count.fetch_add(1) < source0_budget;
  }
  return budget < 0 || gpu_interrupt_trace_count.fetch_add(1) < budget;
}

bool ShouldTraceMarkVblank() {
  if (!cvars::gpu_trace_interrupts) {
    return false;
  }
  int32_t budget = cvars::gpu_trace_interrupts_budget;
  int32_t vblank_budget = budget < 0 ? 32 : std::min<int32_t>(budget, 32);
  return gpu_mark_vblank_trace_count.fetch_add(1) < vblank_budget;
}

uint32_t MaybeReadGuestU32(Memory* memory, uint32_t address) {
  if (!memory || !address || address >= 0xF0000000) {
    return 0;
  }
  return xe::load_and_swap<uint32_t>(memory->TranslateVirtual(address));
}

}  // namespace

// Nvidia Optimus/AMD PowerXpress support.
// These exports force the process to trigger the discrete GPU in multi-GPU
// systems.
// https://developer.download.nvidia.com/devzone/devcenter/gamegraphics/files/OptimusRenderingPolicies.pdf
// https://stackoverflow.com/questions/17458803/amd-equivalent-to-nvoptimusenablement
#if XE_PLATFORM_WIN32
extern "C" {
__declspec(dllexport) uint32_t NvOptimusEnablement = 0x00000001;
__declspec(dllexport) uint32_t AmdPowerXpressRequestHighPerformance = 1;
}  // extern "C"
#endif  // XE_PLATFORM_WIN32

GraphicsSystem::GraphicsSystem() : vsync_worker_running_(false) {}

GraphicsSystem::~GraphicsSystem() = default;

X_STATUS GraphicsSystem::Setup(cpu::Processor* processor,
                               kernel::KernelState* kernel_state,
                               ui::WindowedAppContext* app_context,
                               bool with_presentation) {
  memory_ = processor->memory();
  processor_ = processor;
  kernel_state_ = kernel_state;
  app_context_ = app_context;

  if (with_presentation && provider_) {
    // Safe if either the UI thread call or the presenter creation fails.
    if (app_context_) {
      app_context_->CallInUIThreadSynchronous([this]() {
        presenter_ = provider_->CreatePresenter(
            [this](bool is_responsible, bool statically_from_ui_thread) {
              OnHostGpuLossFromAnyThread(is_responsible);
            });
      });
    } else {
      // May be needed for offscreen use, such as capturing the guest output
      // image.
      presenter_ = provider_->CreatePresenter(
          [this](bool is_responsible, bool statically_from_ui_thread) {
            OnHostGpuLossFromAnyThread(is_responsible);
          });
    }
  }

  // Create command processor. This will spin up a thread to process all
  // incoming ringbuffer packets.
  command_processor_ = CreateCommandProcessor();
  if (!command_processor_->Initialize()) {
    XELOGE("Unable to initialize command processor");
    return X_STATUS_UNSUCCESSFUL;
  }

  // Let the processor know we want register access callbacks.
  memory_->AddVirtualMappedRange(
      0x7FC80000, 0xFFFF0000, 0x0000FFFF, this,
      reinterpret_cast<cpu::MMIOReadCallback>(ReadRegisterThunk),
      reinterpret_cast<cpu::MMIOWriteCallback>(WriteRegisterThunk));

  // 60hz vsync timer.
  vsync_worker_running_ = true;
  vsync_worker_thread_ = kernel::object_ref<kernel::XHostThread>(
      new kernel::XHostThread(kernel_state_, 128 * 1024, 0, [this]() {
        uint64_t vsync_duration = cvars::vsync ? 16 : 1;
        uint64_t last_frame_time = Clock::QueryGuestTickCount();
        while (vsync_worker_running_) {
          uint64_t current_time = Clock::QueryGuestTickCount();
          uint64_t elapsed = (current_time - last_frame_time) /
                             (Clock::guest_tick_frequency() / 1000);
          if (elapsed >= vsync_duration) {
            MarkVblank();
            last_frame_time = current_time;
          }
          xe::threading::Sleep(std::chrono::milliseconds(1));
        }
        return 0;
      }));
  // As we run vblank interrupts the debugger must be able to suspend us.
  vsync_worker_thread_->set_can_debugger_suspend(true);
  vsync_worker_thread_->set_name("GPU VSync");
  vsync_worker_thread_->Create();

  if (cvars::trace_gpu_stream) {
    BeginTracing();
  }

  return X_STATUS_SUCCESS;
}

void GraphicsSystem::Shutdown() {
  if (command_processor_) {
    EndTracing();
    command_processor_->Shutdown();
    command_processor_.reset();
  }

  if (vsync_worker_thread_) {
    vsync_worker_running_ = false;
    vsync_worker_thread_->Wait(0, 0, 0, nullptr);
    vsync_worker_thread_.reset();
  }

  if (presenter_) {
    if (app_context_) {
      app_context_->CallInUIThreadSynchronous([this]() { presenter_.reset(); });
    }
    // If there's no app context (thus the presenter is owned by the thread that
    // initialized the GraphicsSystem) or can't be queueing UI thread calls
    // anymore, shutdown anyway.
    presenter_.reset();
  }

  provider_.reset();
}

void GraphicsSystem::OnHostGpuLossFromAnyThread(
    [[maybe_unused]] bool is_responsible) {
  // TODO(Triang3l): Somehow gain exclusive ownership of the Provider (may be
  // used by the command processor, the presenter, and possibly anything else,
  // it's considered free-threaded, except for lifetime management which will be
  // involved in this case) and reset it so a new host GPU API device is
  // created. Then ask the command processor to reset itself in its thread, and
  // ask the UI thread to reset the Presenter (the UI thread manages its
  // lifetime - but if there's no WindowedAppContext, either don't reset it as
  // in this case there's no user who needs uninterrupted gameplay, or somehow
  // protect it with a mutex so any thread can be considered a UI thread and
  // reset).
  if (host_gpu_loss_reported_.test_and_set(std::memory_order_relaxed)) {
    return;
  }
  xe::FatalError("Graphics device lost (probably due to an internal error)");
}

uint32_t GraphicsSystem::ReadRegisterThunk(void* ppc_context,
                                           GraphicsSystem* gs, uint32_t addr) {
  return gs->ReadRegister(addr);
}

void GraphicsSystem::WriteRegisterThunk(void* ppc_context, GraphicsSystem* gs,
                                        uint32_t addr, uint32_t value) {
  gs->WriteRegister(addr, value);
}

uint32_t GraphicsSystem::ReadRegister(uint32_t addr) {
  uint32_t r = (addr & 0xFFFF) / 4;

  switch (r) {
    case 0x0F00:  // RB_EDRAM_TIMING
      return 0x08100748;
    case 0x0F01:  // RB_BC_CONTROL
      return 0x0000200E;
    case 0x194C:  // R500_D1MODE_V_COUNTER
      return 0x000002D0;
    case 0x1951:  // interrupt status
      return 1;   // vblank
    case 0x1961:  // AVIVO_D1MODE_VIEWPORT_SIZE
                  // Screen res - 1280x720
                  // maximum [width(0x0FFF), height(0x0FFF)]
      return 0x050002D0;
    case 0x01C4:  // CP_RB_RPTR
    case 0x01C5:  // CP_RB_WPTR
      break;
    default:
      if (!register_file_.GetRegisterInfo(r) &&
          ShouldLogUnknownGpuRegister(gpu_unknown_register_read_log_count)) {
        XELOGE("GPU: Read from unknown register ({:04X})", r);
      }
  }

  assert_true(r < RegisterFile::kRegisterCount);
  return register_file_.values[r];
}

void GraphicsSystem::WriteRegister(uint32_t addr, uint32_t value) {
  uint32_t r = (addr & 0xFFFF) / 4;

  switch (r) {
    case 0x01C5:  // CP_RB_WPTR
      command_processor_->UpdateWritePointer(value);
      break;
    case 0x1844:  // AVIVO_D1GRPH_PRIMARY_SURFACE_ADDRESS
      break;
    default:
      if (ShouldLogUnknownGpuRegister(gpu_unknown_register_write_log_count)) {
        XELOGW("Unknown GPU register {:04X} write: {:08X}", r, value);
      }
      break;
  }

  assert_true(r < RegisterFile::kRegisterCount);
  register_file_.values[r] = value;
}

void GraphicsSystem::InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) {
  command_processor_->InitializeRingBuffer(ptr, size_log2);
}

void GraphicsSystem::EnableReadPointerWriteBack(uint32_t ptr,
                                                uint32_t block_size_log2) {
  command_processor_->EnableReadPointerWriteBack(ptr, block_size_log2);
}

void GraphicsSystem::SetInterruptCallback(uint32_t callback,
                                          uint32_t user_data) {
  interrupt_callback_ = callback;
  interrupt_callback_data_ = user_data;
  XELOGGPU("SetInterruptCallback({:08X}, {:08X})", callback, user_data);
  if (cvars::gpu_trace_interrupts) {
    XELOGI(
        "GPU interrupt trace: callback set callback={:08X} user_data={:08X}",
        callback, user_data);
  }
}

void GraphicsSystem::DispatchInterruptCallback(uint32_t source, uint32_t cpu) {
  if (!interrupt_callback_) {
    return;
  }

  auto thread = kernel::XThread::GetCurrentThread();
  assert_not_null(thread);

  // Pick a CPU, if needed. We're going to guess 2. Because.
  if (cpu == 0xFFFFFFFF) {
    cpu = 2;
  }
  thread->SetActiveCpu(cpu);

  // XELOGGPU("Dispatching GPU interrupt at {:08X} w/ mode {} on cpu {}",
  //          interrupt_callback_, source, cpu);

  bool trace_interrupt = ShouldTraceGpuInterrupt(source);
  uint32_t token_ptr_before = 0;
  uint32_t token_before = 0;
  if (trace_interrupt) {
    token_ptr_before = interrupt_callback_data_
                           ? MaybeReadGuestU32(memory_,
                                               interrupt_callback_data_ + 0x2A10)
                           : 0;
    token_before = MaybeReadGuestU32(memory_, token_ptr_before);
    XELOGI(
        "GPU interrupt trace: dispatch begin source={} cpu={} callback={:08X} "
        "user_data={:08X} thread_id={:08X} thread='{}' kthread={:08X} "
        "guest_ms={} host_ms={} token_ptr={:08X} token={:08X}",
        source, cpu, interrupt_callback_, interrupt_callback_data_,
        thread->thread_id(), thread->name(), thread->guest_object(),
        Clock::QueryGuestUptimeMillis(), Clock::QueryHostUptimeMillis(),
        token_ptr_before, token_before);
  }

  uint64_t args[] = {source, interrupt_callback_data_};
  processor_->ExecuteInterrupt(thread->thread_state(), interrupt_callback_,
                               args, xe::countof(args));

  if (trace_interrupt) {
    uint32_t token_after = MaybeReadGuestU32(memory_, token_ptr_before);
    XELOGI(
        "GPU interrupt trace: dispatch end source={} cpu={} callback={:08X} "
        "thread_id={:08X} guest_ms={} host_ms={} token_ptr={:08X} "
        "token={:08X}->{:08X}",
        source, cpu, interrupt_callback_, thread->thread_id(),
        Clock::QueryGuestUptimeMillis(), Clock::QueryHostUptimeMillis(),
        token_ptr_before, token_before, token_after);
  }

  if (cvars::gpu_blue_dragon_kick_wait_token && source == 1 &&
      interrupt_callback_data_) {
    uint32_t token_ptr = xe::load_and_swap<uint32_t>(
        memory_->TranslateVirtual(interrupt_callback_data_ + 0x2A10));
    if (token_ptr) {
      auto token_host = memory_->TranslateVirtual(token_ptr);
      uint32_t token = xe::load_and_swap<uint32_t>(token_host);
      xe::store_and_swap<uint32_t>(token_host, token + 1);

      int32_t log_index =
          blue_dragon_wait_token_kick_log_count.fetch_add(1);
      if (log_index < cvars::gpu_blue_dragon_kick_wait_token_budget) {
        XELOGI(
            "Blue Dragon wait token kick source {} user_data {:08X} "
            "token_ptr {:08X} token {:08X}->{:08X}",
            source, interrupt_callback_data_, token_ptr, token, token + 1);
      }
    }
  }
}

void GraphicsSystem::MarkVblank() {
  SCOPE_profile_cpu_f("gpu");

  // Increment vblank counter (so the game sees us making progress).
  uint32_t counter_before = command_processor_->counter();
  command_processor_->increment_counter();
  if (ShouldTraceMarkVblank()) {
    XELOGI(
        "GPU interrupt trace: MarkVblank counter {:08X}->{:08X} "
        "dispatching source=0",
        counter_before, command_processor_->counter());
  }

  // TODO(benvanik): we shouldn't need to do the dispatch here, but there's
  //     something wrong and the CP will block waiting for code that
  //     needs to be run in the interrupt.
  DispatchInterruptCallback(0, 2);
}

void GraphicsSystem::ClearCaches() {
  command_processor_->CallInThread(
      [&]() { command_processor_->ClearCaches(); });
}

void GraphicsSystem::InitializeShaderStorage(
    const std::filesystem::path& cache_root, uint32_t title_id, bool blocking) {
  if (!cvars::store_shaders) {
    return;
  }
  if (blocking) {
    if (command_processor_->is_paused()) {
      // Safe to run on any thread while the command processor is paused, no
      // race condition.
      command_processor_->InitializeShaderStorage(cache_root, title_id, true);
    } else {
      xe::threading::Fence fence;
      command_processor_->CallInThread([this, cache_root, title_id, &fence]() {
        command_processor_->InitializeShaderStorage(cache_root, title_id, true);
        fence.Signal();
      });
      fence.Wait();
    }
  } else {
    command_processor_->CallInThread([this, cache_root, title_id]() {
      command_processor_->InitializeShaderStorage(cache_root, title_id, false);
    });
  }
}

void GraphicsSystem::RequestFrameTrace() {
  command_processor_->RequestFrameTrace(cvars::trace_gpu_prefix);
}

void GraphicsSystem::BeginTracing() {
  command_processor_->BeginTracing(cvars::trace_gpu_prefix);
}

void GraphicsSystem::EndTracing() { command_processor_->EndTracing(); }

void GraphicsSystem::Pause() {
  paused_ = true;

  command_processor_->Pause();
}

void GraphicsSystem::Resume() {
  paused_ = false;

  command_processor_->Resume();
}

bool GraphicsSystem::Save(ByteStream* stream) {
  stream->Write<uint32_t>(interrupt_callback_);
  stream->Write<uint32_t>(interrupt_callback_data_);

  return command_processor_->Save(stream);
}

bool GraphicsSystem::Restore(ByteStream* stream) {
  interrupt_callback_ = stream->Read<uint32_t>();
  interrupt_callback_data_ = stream->Read<uint32_t>();

  return command_processor_->Restore(stream);
}

}  // namespace gpu
}  // namespace xe
