/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/gpu/command_processor.h"

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cmath>
#include <cstring>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/byte_stream.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/memory.h"
#include "xenia/base/profiling.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/gpu/gpu_flags.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/gpu/sampler_info.h"
#include "xenia/gpu/texture_info.h"
#include "xenia/gpu/xenos.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"

namespace xe {
namespace gpu {

using namespace xe::gpu::xenos;

namespace {

constexpr uint32_t kCpRbRptrRegister = 0x01C4;
constexpr uint32_t kCpRbWptrRegister = 0x01C5;

std::atomic<int32_t> gpu_packet_trace_count{0};
std::atomic<int32_t> gpu_interrupt_packet_trace_count{0};
std::atomic<int32_t> gpu_swap_frontbuffer_checksum_count{0};
std::atomic<int32_t> gpu_swap_render_targets_trace_count{0};
std::atomic<int32_t> gpu_unknown_register_write_log_count{0};
std::atomic<bool> gpu_swap_probe_cvars_logged{false};

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

bool ShouldTraceGpuPacket() {
  return cvars::gpu_trace_swap &&
         gpu_packet_trace_count.fetch_add(1) < cvars::gpu_trace_packet_budget;
}

bool ShouldTraceGpuInterruptPacket() {
  if (!cvars::gpu_trace_interrupts) {
    return false;
  }
  int32_t budget = cvars::gpu_trace_interrupts_budget;
  return budget < 0 || gpu_interrupt_packet_trace_count.fetch_add(1) < budget;
}

bool ShouldTraceSwapFrontbufferChecksum() {
  if (!cvars::gpu_trace_swap_frontbuffer_checksum) {
    return false;
  }
  int32_t budget = cvars::gpu_trace_swap_frontbuffer_checksum_budget;
  return budget < 0 ||
         gpu_swap_frontbuffer_checksum_count.fetch_add(1) < budget;
}

bool ShouldTraceSwapRenderTargets() {
  if (!cvars::gpu_trace_swap_render_targets) {
    return false;
  }
  int32_t budget = cvars::gpu_trace_swap_render_targets_budget;
  return budget < 0 ||
         gpu_swap_render_targets_trace_count.fetch_add(1) < budget;
}

void TraceSwapRenderTargets(const RegisterFile* regs, uint32_t frontbuffer_ptr,
                            uint32_t frontbuffer_width,
                            uint32_t frontbuffer_height) {
  if (!ShouldTraceSwapRenderTargets() || !regs) {
    return;
  }

  auto rb_surface_info = regs->Get<reg::RB_SURFACE_INFO>();
  auto rb_modecontrol = regs->Get<reg::RB_MODECONTROL>();
  auto rb_colorcontrol = regs->Get<reg::RB_COLORCONTROL>();
  auto rb_depth_info = regs->Get<reg::RB_DEPTH_INFO>();
  auto rb_copy_control = regs->Get<reg::RB_COPY_CONTROL>();
  uint32_t rb_copy_dest_base = (*regs)[XE_GPU_REG_RB_COPY_DEST_BASE];
  auto rb_copy_dest_pitch = regs->Get<reg::RB_COPY_DEST_PITCH>();
  uint32_t rb_color_mask = (*regs)[XE_GPU_REG_RB_COLOR_MASK];

  XELOGI(
      "GPU swap RT trace: frontbuffer={:08X} size={}x{} rb_surface={:08X} "
      "surface_pitch={} msaa={} rb_mode={:08X} edram_mode={} "
      "rb_colorcontrol={:08X} rb_color_mask={:04X} depth_info={:08X} "
      "depth_base={} depth_base_bit11={} depth_format={} copy_control={:08X} "
      "copy_src={} copy_command={} copy_dest_base={:08X} copy_dest_pitch={:08X}",
      frontbuffer_ptr, frontbuffer_width, frontbuffer_height,
      rb_surface_info.value, rb_surface_info.surface_pitch,
      uint32_t(rb_surface_info.msaa_samples), rb_modecontrol.value,
      uint32_t(rb_modecontrol.edram_mode), rb_colorcontrol.value,
      rb_color_mask & 0xFFFF, rb_depth_info.value, rb_depth_info.depth_base,
      rb_depth_info.depth_base_bit_11, uint32_t(rb_depth_info.depth_format),
      rb_copy_control.value, rb_copy_control.copy_src_select,
      uint32_t(rb_copy_control.copy_command), rb_copy_dest_base,
      rb_copy_dest_pitch.value);

  for (uint32_t i = 0; i < xenos::kMaxColorRenderTargets; ++i) {
    auto color_info = regs->Get<reg::RB_COLOR_INFO>(
        reg::RB_COLOR_INFO::rt_register_indices[i]);
    uint32_t write_mask = (rb_color_mask >> (i * 4)) & 0xF;
    XELOGI(
        "GPU swap RT trace: color{} info={:08X} base_tiles={} "
        "base_bit11={} format={}({}) exp_bias={} write_mask={:X} "
        "pitch_pixels={} approx_edram_byte_offset={:08X}",
        i, color_info.value, color_info.color_base,
        color_info.color_base_bit_11, uint32_t(color_info.color_format),
        xenos::GetColorRenderTargetFormatName(color_info.color_format),
        color_info.color_exp_bias, write_mask, rb_surface_info.surface_pitch,
        color_info.color_base * xenos::kEdramTileWidthSamples *
            xenos::kEdramTileHeightSamples * uint32_t(sizeof(uint32_t)));
  }
}

void TraceSwapProbeCvarsOnce() {
  bool expected = false;
  if (!gpu_swap_probe_cvars_logged.compare_exchange_strong(expected, true)) {
    return;
  }
  XELOGI(
      "GPU swap probe cvars: gpu_trace_swap={} frontbuffer_checksum={} "
      "frontbuffer_budget={} render_targets={} render_targets_budget={}",
      cvars::gpu_trace_swap, cvars::gpu_trace_swap_frontbuffer_checksum,
      cvars::gpu_trace_swap_frontbuffer_checksum_budget,
      cvars::gpu_trace_swap_render_targets,
      cvars::gpu_trace_swap_render_targets_budget);
}

void TraceSwapFrontbufferChecksum(Memory* memory, uint32_t frontbuffer_ptr,
                                  uint32_t frontbuffer_width,
                                  uint32_t frontbuffer_height) {
  if (!ShouldTraceSwapFrontbufferChecksum()) {
    return;
  }
  if (!memory || !frontbuffer_ptr || !frontbuffer_width ||
      !frontbuffer_height) {
    XELOGI(
        "GPU swap trace: frontbuffer checksum skipped pa={:08X} size={}x{}",
        frontbuffer_ptr, frontbuffer_width, frontbuffer_height);
    return;
  }

  uint64_t byte_count =
      uint64_t(frontbuffer_width) * uint64_t(frontbuffer_height) * 4;
  if (!byte_count) {
    return;
  }
  const uint8_t* bytes =
      memory->TranslatePhysical<const uint8_t*>(frontbuffer_ptr);
  constexpr uint64_t kSampleStride = 4096;
  uint64_t checksum = 1469598103934665603ull;
  uint32_t samples = 0;
  uint32_t nonzero_samples = 0;
  uint64_t first_nonzero_offset = UINT64_MAX;
  uint32_t first_nonzero_value = 0;
  for (uint64_t offset = 0; offset + sizeof(uint32_t) <= byte_count;
       offset += kSampleStride) {
    uint32_t word = 0;
    std::memcpy(&word, bytes + offset, sizeof(word));
    checksum ^= uint64_t(word) + (offset << 1);
    checksum *= 1099511628211ull;
    ++samples;
    if (word) {
      ++nonzero_samples;
      if (first_nonzero_offset == UINT64_MAX) {
        first_nonzero_offset = offset;
        first_nonzero_value = word;
      }
    }
  }

  uint32_t first_words[8] = {};
  uint32_t first_word_count =
      uint32_t(std::min<uint64_t>(xe::countof(first_words), byte_count / 4));
  for (uint32_t i = 0; i < first_word_count; ++i) {
    std::memcpy(&first_words[i], bytes + i * sizeof(uint32_t),
                sizeof(uint32_t));
  }

  XELOGI(
      "GPU swap trace: frontbuffer checksum pa={:08X} size={}x{} bytes={} "
      "stride={} samples={} nonzero={} checksum={:016X} first_nonzero={} "
      "first_nonzero_value={:08X} first={:08X},{:08X},{:08X},{:08X},"
      "{:08X},{:08X},{:08X},{:08X}",
      frontbuffer_ptr, frontbuffer_width, frontbuffer_height, byte_count,
      kSampleStride, samples, nonzero_samples, checksum,
      first_nonzero_offset == UINT64_MAX ? -1
                                         : int64_t(first_nonzero_offset),
      first_nonzero_value, first_words[0], first_words[1], first_words[2],
      first_words[3], first_words[4], first_words[5], first_words[6],
      first_words[7]);
}

const char* Type3OpcodeName(uint32_t opcode) {
  switch (opcode) {
    case PM4_ME_INIT:
      return "PM4_ME_INIT";
    case PM4_NOP:
      return "PM4_NOP";
    case PM4_INTERRUPT:
      return "PM4_INTERRUPT";
    case PM4_XE_SWAP:
      return "PM4_XE_SWAP";
    case PM4_INDIRECT_BUFFER:
      return "PM4_INDIRECT_BUFFER";
    case PM4_INDIRECT_BUFFER_PFD:
      return "PM4_INDIRECT_BUFFER_PFD";
    case PM4_WAIT_REG_MEM:
      return "PM4_WAIT_REG_MEM";
    case PM4_REG_RMW:
      return "PM4_REG_RMW";
    case PM4_REG_TO_MEM:
      return "PM4_REG_TO_MEM";
    case PM4_MEM_WRITE:
      return "PM4_MEM_WRITE";
    case PM4_COND_WRITE:
      return "PM4_COND_WRITE";
    case PM4_EVENT_WRITE:
      return "PM4_EVENT_WRITE";
    case PM4_EVENT_WRITE_SHD:
      return "PM4_EVENT_WRITE_SHD";
    case PM4_EVENT_WRITE_EXT:
      return "PM4_EVENT_WRITE_EXT";
    case PM4_EVENT_WRITE_ZPD:
      return "PM4_EVENT_WRITE_ZPD";
    case PM4_DRAW_INDX:
      return "PM4_DRAW_INDX";
    case PM4_DRAW_INDX_2:
      return "PM4_DRAW_INDX_2";
    case PM4_SET_CONSTANT:
      return "PM4_SET_CONSTANT";
    case PM4_SET_CONSTANT2:
      return "PM4_SET_CONSTANT2";
    case PM4_LOAD_ALU_CONSTANT:
      return "PM4_LOAD_ALU_CONSTANT";
    case PM4_SET_SHADER_CONSTANTS:
      return "PM4_SET_SHADER_CONSTANTS";
    case PM4_IM_LOAD:
      return "PM4_IM_LOAD";
    case PM4_IM_LOAD_IMMEDIATE:
      return "PM4_IM_LOAD_IMMEDIATE";
    case PM4_INVALIDATE_STATE:
      return "PM4_INVALIDATE_STATE";
    case PM4_VIZ_QUERY:
      return "PM4_VIZ_QUERY";
    case PM4_SET_BIN_MASK_LO:
      return "PM4_SET_BIN_MASK_LO";
    case PM4_SET_BIN_MASK_HI:
      return "PM4_SET_BIN_MASK_HI";
    case PM4_SET_BIN_SELECT_LO:
      return "PM4_SET_BIN_SELECT_LO";
    case PM4_SET_BIN_SELECT_HI:
      return "PM4_SET_BIN_SELECT_HI";
    case PM4_SET_BIN_MASK:
      return "PM4_SET_BIN_MASK";
    case PM4_SET_BIN_SELECT:
      return "PM4_SET_BIN_SELECT";
    case PM4_CONTEXT_UPDATE:
      return "PM4_CONTEXT_UPDATE";
    case PM4_WAIT_FOR_IDLE:
      return "PM4_WAIT_FOR_IDLE";
    default:
      return "PM4_UNKNOWN";
  }
}

}  // namespace

CommandProcessor::CommandProcessor(GraphicsSystem* graphics_system,
                                   kernel::KernelState* kernel_state)
    : memory_(graphics_system->memory()),
      kernel_state_(kernel_state),
      graphics_system_(graphics_system),
      register_file_(graphics_system_->register_file()),
      trace_writer_(graphics_system->memory()->physical_membase()),
      worker_running_(true),
      write_ptr_index_event_(xe::threading::Event::CreateAutoResetEvent(false)),
      write_ptr_index_(0) {
  assert_not_null(write_ptr_index_event_);
}

CommandProcessor::~CommandProcessor() = default;

bool CommandProcessor::Initialize() {
  // Initialize the gamma ramps to their default (linear) values - taken from
  // what games set when starting with the sRGB (return value 1)
  // VdGetCurrentDisplayGamma.
  for (uint32_t i = 0; i < 256; ++i) {
    uint32_t value = i * 0x3FF / 0xFF;
    reg::DC_LUT_30_COLOR& gamma_ramp_entry = gamma_ramp_256_entry_table_[i];
    gamma_ramp_entry.color_10_blue = value;
    gamma_ramp_entry.color_10_green = value;
    gamma_ramp_entry.color_10_red = value;
  }
  for (uint32_t i = 0; i < 128; ++i) {
    reg::DC_LUT_PWL_DATA gamma_ramp_entry = {};
    gamma_ramp_entry.base = (i * 0xFFFF / 0x7F) & ~UINT32_C(0x3F);
    gamma_ramp_entry.delta = i < 0x7F ? 0x200 : 0;
    for (uint32_t j = 0; j < 3; ++j) {
      gamma_ramp_pwl_rgb_[i][j] = gamma_ramp_entry;
    }
  }

  worker_running_ = true;
  worker_thread_ = kernel::object_ref<kernel::XHostThread>(
      new kernel::XHostThread(kernel_state_, 128 * 1024, 0, [this]() {
        WorkerThreadMain();
        return 0;
      }));
  worker_thread_->set_name("GPU Commands");
  worker_thread_->Create();

  return true;
}

void CommandProcessor::Shutdown() {
  EndTracing();

  worker_running_ = false;
  write_ptr_index_event_->Set();
  worker_thread_->Wait(0, 0, 0, nullptr);
  worker_thread_.reset();
}

void CommandProcessor::InitializeShaderStorage(
    const std::filesystem::path& cache_root, uint32_t title_id, bool blocking) {
}

void CommandProcessor::RequestFrameTrace(
    const std::filesystem::path& root_path) {
  if (trace_state_ == TraceState::kStreaming) {
    XELOGE("Streaming trace; cannot also trace frame.");
    return;
  }
  if (trace_state_ == TraceState::kSingleFrame) {
    XELOGE("Frame trace already pending; ignoring.");
    return;
  }
  trace_state_ = TraceState::kSingleFrame;
  trace_frame_path_ = root_path;
}

void CommandProcessor::BeginTracing(const std::filesystem::path& root_path) {
  if (trace_state_ == TraceState::kStreaming) {
    XELOGE("Streaming already active; ignoring request.");
    return;
  }
  if (trace_state_ == TraceState::kSingleFrame) {
    XELOGE("Frame trace pending; ignoring streaming request.");
    return;
  }
  // Streaming starts on the next primary buffer execute.
  trace_state_ = TraceState::kStreaming;
  trace_stream_path_ = root_path;
}

void CommandProcessor::EndTracing() {
  if (!trace_writer_.is_open()) {
    return;
  }
  assert_true(trace_state_ == TraceState::kStreaming);
  trace_state_ = TraceState::kDisabled;
  trace_writer_.Close();
}

void CommandProcessor::RestoreRegisters(uint32_t first_register,
                                        const uint32_t* register_values,
                                        uint32_t register_count,
                                        bool execute_callbacks) {
  if (first_register > RegisterFile::kRegisterCount ||
      RegisterFile::kRegisterCount - first_register < register_count) {
    XELOGW(
        "CommandProcessor::RestoreRegisters out of bounds (0x{:X} registers "
        "starting with 0x{:X}, while a total of 0x{:X} registers are stored)",
        register_count, first_register, RegisterFile::kRegisterCount);
    if (first_register > RegisterFile::kRegisterCount) {
      return;
    }
    register_count =
        std::min(uint32_t(RegisterFile::kRegisterCount) - first_register,
                 register_count);
  }
  if (execute_callbacks) {
    for (uint32_t i = 0; i < register_count; ++i) {
      WriteRegister(first_register + i, register_values[i]);
    }
  } else {
    std::memcpy(register_file_->values + first_register, register_values,
                sizeof(uint32_t) * register_count);
  }
}

void CommandProcessor::RestoreGammaRamp(
    const reg::DC_LUT_30_COLOR* new_gamma_ramp_256_entry_table,
    const reg::DC_LUT_PWL_DATA* new_gamma_ramp_pwl_rgb,
    uint32_t new_gamma_ramp_rw_component) {
  std::memcpy(gamma_ramp_256_entry_table_, new_gamma_ramp_256_entry_table,
              sizeof(reg::DC_LUT_30_COLOR) * 256);
  std::memcpy(gamma_ramp_pwl_rgb_, new_gamma_ramp_pwl_rgb,
              sizeof(reg::DC_LUT_PWL_DATA) * 3 * 128);
  gamma_ramp_rw_component_ = new_gamma_ramp_rw_component;
  OnGammaRamp256EntryTableValueWritten();
  OnGammaRampPWLValueWritten();
}

void CommandProcessor::CallInThread(std::function<void()> fn) {
  if (pending_fns_.empty() &&
      kernel::XThread::IsInThread(worker_thread_.get())) {
    fn();
  } else {
    pending_fns_.push(std::move(fn));
  }
}

void CommandProcessor::ClearCaches() {}

void CommandProcessor::SetDesiredSwapPostEffect(
    SwapPostEffect swap_post_effect) {
  if (swap_post_effect_desired_ == swap_post_effect) {
    return;
  }
  swap_post_effect_desired_ = swap_post_effect;
  CallInThread([this, swap_post_effect]() {
    swap_post_effect_actual_ = swap_post_effect;
  });
}

void CommandProcessor::WorkerThreadMain() {
  if (!SetupContext()) {
    xe::FatalError("Unable to setup command processor internal state");
    return;
  }

  while (worker_running_) {
    while (!pending_fns_.empty()) {
      auto fn = std::move(pending_fns_.front());
      pending_fns_.pop();
      fn();
    }

    uint32_t write_ptr_index = write_ptr_index_.load();
    if (write_ptr_index == 0xBAADF00D || read_ptr_index_ == write_ptr_index) {
      SCOPE_profile_cpu_i("gpu", "xe::gpu::CommandProcessor::Stall");
      // We've run out of commands to execute.
      // We spin here waiting for new ones, as the overhead of waiting on our
      // event is too high.
      PrepareForWait();
      uint32_t loop_count = 0;
      do {
        // If we spin around too much, revert to a "low-power" state.
        if (loop_count > 500) {
          const int wait_time_ms = 5;
          xe::threading::Wait(write_ptr_index_event_.get(), true,
                              std::chrono::milliseconds(wait_time_ms));
        }

        xe::threading::MaybeYield();
        loop_count++;
        write_ptr_index = write_ptr_index_.load();
      } while (worker_running_ && pending_fns_.empty() &&
               (write_ptr_index == 0xBAADF00D ||
                read_ptr_index_ == write_ptr_index));
      ReturnFromWait();
      if (!worker_running_ || !pending_fns_.empty()) {
        continue;
      }
    }
    assert_true(read_ptr_index_ != write_ptr_index);

    // Execute. Note that we handle wraparound transparently.
    if (cvars::gpu_trace_swap) {
      XELOGI(
          "GPU swap trace: CommandProcessor ExecutePrimaryBuffer begin "
          "read_ptr={:08X} write_ptr={:08X} primary_ptr={:08X}",
          read_ptr_index_, write_ptr_index, primary_buffer_ptr_);
    }
    read_ptr_index_ = ExecutePrimaryBuffer(read_ptr_index_, write_ptr_index);
    if (cvars::gpu_trace_swap) {
      XELOGI(
          "GPU swap trace: CommandProcessor ExecutePrimaryBuffer end "
          "read_ptr={:08X} write_ptr={:08X}",
          read_ptr_index_, write_ptr_index);
    }
    UpdatePrimaryReadPointer(read_ptr_index_, "primary_buffer_end");

    if (cvars::gpu_interrupt_on_ring_idle) {
      if (ShouldTraceGpuInterruptPacket()) {
        XELOGI(
            "GPU interrupt trace: ring-idle source=1 dispatch "
            "read_ptr={:08X} write_ptr={:08X} primary_ptr={:08X}",
            read_ptr_index_, write_ptr_index, primary_buffer_ptr_);
      }
      graphics_system_->DispatchInterruptCallback(1, 2);
    }

    // FIXME: We're supposed to process the WAIT_UNTIL register at this point,
    // but no games seem to actually use it.
  }

  ShutdownContext();
}

void CommandProcessor::Pause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  threading::Fence fence;
  CallInThread([&fence]() {
    fence.Signal();
    threading::Thread::GetCurrentThread()->Suspend();
  });

  fence.Wait();
}

void CommandProcessor::Resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;

  worker_thread_->thread()->Resume();
}

bool CommandProcessor::Save(ByteStream* stream) {
  assert_true(paused_);

  stream->Write<uint32_t>(primary_buffer_ptr_);
  stream->Write<uint32_t>(primary_buffer_size_);
  stream->Write<uint32_t>(read_ptr_index_);
  stream->Write<uint32_t>(read_ptr_update_freq_);
  stream->Write<uint32_t>(read_ptr_writeback_ptr_);
  stream->Write<uint32_t>(write_ptr_index_.load());

  return true;
}

bool CommandProcessor::Restore(ByteStream* stream) {
  assert_true(paused_);

  primary_buffer_ptr_ = stream->Read<uint32_t>();
  primary_buffer_size_ = stream->Read<uint32_t>();
  read_ptr_index_ = stream->Read<uint32_t>();
  read_ptr_update_freq_ = stream->Read<uint32_t>();
  read_ptr_writeback_ptr_ = stream->Read<uint32_t>();
  write_ptr_index_.store(stream->Read<uint32_t>());
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbRptrRegister]) = read_ptr_index_;
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbWptrRegister]) = write_ptr_index_.load();

  return true;
}

bool CommandProcessor::SetupContext() { return true; }

void CommandProcessor::ShutdownContext() {}

void CommandProcessor::InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) {
  read_ptr_index_ = 0;
  primary_buffer_ptr_ = ptr;
  primary_buffer_size_ = uint32_t(1) << (size_log2 + 3);
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: CommandProcessor InitializeRingBuffer ptr={:08X} "
        "size_log2={} primary_size={} current_wptr={:08X}",
        primary_buffer_ptr_, size_log2, primary_buffer_size_,
        write_ptr_index_.load());
  }
  std::memset(kernel_state_->memory()->TranslatePhysical(primary_buffer_ptr_),
              0, primary_buffer_size_);
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbRptrRegister]) = read_ptr_index_;
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbWptrRegister]) = write_ptr_index_.load();
}

void CommandProcessor::EnableReadPointerWriteBack(uint32_t ptr,
                                                  uint32_t block_size_log2) {
  // CP_RB_RPTR_ADDR Ring Buffer Read Pointer Address 0x70C
  // ptr = RB_RPTR_ADDR, pointer to write back the address to.
  read_ptr_writeback_ptr_ = ptr;
  // CP_RB_CNTL Ring Buffer Control 0x704
  // block_size = RB_BLKSZ, log2 of number of quadwords read between updates of
  //              the read pointer.
  read_ptr_update_freq_ = uint32_t(1) << block_size_log2 >> 2;
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: CommandProcessor EnableReadPointerWriteBack "
        "ptr={:08X} block_size_log2={} update_freq={}",
        read_ptr_writeback_ptr_, block_size_log2, read_ptr_update_freq_);
  }
}

void CommandProcessor::UpdateWritePointer(uint32_t value) {
  uint32_t old_value = write_ptr_index_.load();
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: CommandProcessor UpdateWritePointer {:08X}->{:08X} "
        "read_ptr={:08X} primary_ptr={:08X} primary_size={}",
        old_value, value, read_ptr_index_, primary_buffer_ptr_,
        primary_buffer_size_);
  }
  write_ptr_index_ = value;
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbWptrRegister]) = value;
  write_ptr_index_event_->Set();
}

void CommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  RegisterFile& regs = *register_file_;
  if (index >= RegisterFile::kRegisterCount) {
    XELOGW("CommandProcessor::WriteRegister index out of bounds: {}", index);
    return;
  }

  // Volatile for the WAIT_REG_MEM loop.
  const_cast<volatile uint32_t&>(regs.values[index]) = value;
  // GetRegisterInfo is a large switch executed on EVERY register write, and the
  // guest issues a huge volume of these (~10,600 draws/frame in Blue Dragon).
  // It is only needed to gate the unknown-register warning. When that logging is
  // suppressed (budget == 0) skip the lookup entirely. With logging enabled the
  // behavior is unchanged: the lookup runs, and the rate-limit budget is only
  // consumed (atomic fetch_add) for actually-unknown registers as before.
  // (Minor win - only removes the per-write lookup when budget==0; the cost is
  // inherent to the unknown-register check otherwise. The dominant per-frame
  // cost is the sheer draw volume, not this.)
  if (cvars::gpu_unknown_register_log_budget != 0 &&
      !regs.GetRegisterInfo(index) &&
      ShouldLogUnknownGpuRegister(gpu_unknown_register_write_log_count)) {
    XELOGW("GPU: Write to unknown register ({:04X} = {:08X})", index, value);
  }

  // Scratch register writeback.
  if (index >= XE_GPU_REG_SCRATCH_REG0 && index <= XE_GPU_REG_SCRATCH_REG7) {
    uint32_t scratch_reg = index - XE_GPU_REG_SCRATCH_REG0;
    if ((1 << scratch_reg) & regs.values[XE_GPU_REG_SCRATCH_UMSK]) {
      // Enabled - write to address.
      uint32_t scratch_addr = regs.values[XE_GPU_REG_SCRATCH_ADDR];
      uint32_t mem_addr = scratch_addr + (scratch_reg * 4);
      xe::store_and_swap<uint32_t>(memory_->TranslatePhysical(mem_addr), value);
    }
  } else {
    switch (index) {
      // If this is a COHER register, set the dirty flag.
      // This will block the command processor the next time it WAIT_REG_MEMs
      // and allow us to synchronize the memory.
      case XE_GPU_REG_COHER_STATUS_HOST: {
        const_cast<volatile uint32_t&>(regs.values[index]) |=
            UINT32_C(0x80000000);
      } break;

      case XE_GPU_REG_DC_LUT_RW_INDEX: {
        // Reset the sequential read / write component index (see the M56
        // DC_LUT_SEQ_COLOR documentation).
        gamma_ramp_rw_component_ = 0;
      } break;

      case XE_GPU_REG_DC_LUT_SEQ_COLOR: {
        // Should be in the 256-entry table writing mode.
        assert_zero(regs[XE_GPU_REG_DC_LUT_RW_MODE] & 0b1);
        auto gamma_ramp_rw_index = regs.Get<reg::DC_LUT_RW_INDEX>();
        // DC_LUT_SEQ_COLOR is in the red, green, blue order, but the write
        // enable mask is blue, green, red.
        bool write_gamma_ramp_component =
            (regs[XE_GPU_REG_DC_LUT_WRITE_EN_MASK] &
             (UINT32_C(1) << (2 - gamma_ramp_rw_component_))) != 0;
        if (write_gamma_ramp_component) {
          reg::DC_LUT_30_COLOR& gamma_ramp_entry =
              gamma_ramp_256_entry_table_[gamma_ramp_rw_index.rw_index];
          // Bits 0:5 are hardwired to zero.
          uint32_t gamma_ramp_seq_color =
              regs.Get<reg::DC_LUT_SEQ_COLOR>().seq_color >> 6;
          switch (gamma_ramp_rw_component_) {
            case 0:
              gamma_ramp_entry.color_10_red = gamma_ramp_seq_color;
              break;
            case 1:
              gamma_ramp_entry.color_10_green = gamma_ramp_seq_color;
              break;
            case 2:
              gamma_ramp_entry.color_10_blue = gamma_ramp_seq_color;
              break;
          }
        }
        if (++gamma_ramp_rw_component_ >= 3) {
          gamma_ramp_rw_component_ = 0;
          reg::DC_LUT_RW_INDEX new_gamma_ramp_rw_index = gamma_ramp_rw_index;
          ++new_gamma_ramp_rw_index.rw_index;
          WriteRegister(
              XE_GPU_REG_DC_LUT_RW_INDEX,
              xe::memory::Reinterpret<uint32_t>(new_gamma_ramp_rw_index));
        }
        if (write_gamma_ramp_component) {
          OnGammaRamp256EntryTableValueWritten();
        }
      } break;

      case XE_GPU_REG_DC_LUT_PWL_DATA: {
        // Should be in the PWL writing mode.
        assert_not_zero(regs[XE_GPU_REG_DC_LUT_RW_MODE] & 0b1);
        auto gamma_ramp_rw_index = regs.Get<reg::DC_LUT_RW_INDEX>();
        // Bit 7 of the index is ignored for PWL.
        uint32_t gamma_ramp_rw_index_pwl = gamma_ramp_rw_index.rw_index & 0x7F;
        // DC_LUT_PWL_DATA is likely in the red, green, blue order because
        // DC_LUT_SEQ_COLOR is, but the write enable mask is blue, green, red.
        bool write_gamma_ramp_component =
            (regs[XE_GPU_REG_DC_LUT_WRITE_EN_MASK] &
             (UINT32_C(1) << (2 - gamma_ramp_rw_component_))) != 0;
        if (write_gamma_ramp_component) {
          reg::DC_LUT_PWL_DATA& gamma_ramp_entry =
              gamma_ramp_pwl_rgb_[gamma_ramp_rw_index_pwl]
                                 [gamma_ramp_rw_component_];
          auto gamma_ramp_value = regs.Get<reg::DC_LUT_PWL_DATA>();
          // Bits 0:5 are hardwired to zero.
          gamma_ramp_entry.base = gamma_ramp_value.base & ~UINT32_C(0x3F);
          gamma_ramp_entry.delta = gamma_ramp_value.delta & ~UINT32_C(0x3F);
        }
        if (++gamma_ramp_rw_component_ >= 3) {
          gamma_ramp_rw_component_ = 0;
          reg::DC_LUT_RW_INDEX new_gamma_ramp_rw_index = gamma_ramp_rw_index;
          // TODO(Triang3l): Should this increase beyond 7 bits for PWL?
          // Direct3D 9 explicitly sets rw_index to 0x80 after writing the last
          // PWL entry. However, the DC_LUT_RW_INDEX documentation says that for
          // PWL, the bit 7 is ignored.
          new_gamma_ramp_rw_index.rw_index =
              (gamma_ramp_rw_index.rw_index & ~UINT32_C(0x7F)) |
              ((gamma_ramp_rw_index_pwl + 1) & 0x7F);
          WriteRegister(
              XE_GPU_REG_DC_LUT_RW_INDEX,
              xe::memory::Reinterpret<uint32_t>(new_gamma_ramp_rw_index));
        }
        if (write_gamma_ramp_component) {
          OnGammaRampPWLValueWritten();
        }
      } break;

      case XE_GPU_REG_DC_LUT_30_COLOR: {
        // Should be in the 256-entry table writing mode.
        assert_zero(regs[XE_GPU_REG_DC_LUT_RW_MODE] & 0b1);
        auto gamma_ramp_rw_index = regs.Get<reg::DC_LUT_RW_INDEX>();
        uint32_t gamma_ramp_write_enable_mask =
            regs[XE_GPU_REG_DC_LUT_WRITE_EN_MASK] & 0b111;
        if (gamma_ramp_write_enable_mask) {
          reg::DC_LUT_30_COLOR& gamma_ramp_entry =
              gamma_ramp_256_entry_table_[gamma_ramp_rw_index.rw_index];
          auto gamma_ramp_value = regs.Get<reg::DC_LUT_30_COLOR>();
          if (gamma_ramp_write_enable_mask & 0b001) {
            gamma_ramp_entry.color_10_blue = gamma_ramp_value.color_10_blue;
          }
          if (gamma_ramp_write_enable_mask & 0b010) {
            gamma_ramp_entry.color_10_green = gamma_ramp_value.color_10_green;
          }
          if (gamma_ramp_write_enable_mask & 0b100) {
            gamma_ramp_entry.color_10_red = gamma_ramp_value.color_10_red;
          }
        }
        // TODO(Triang3l): Should this reset the component write index? If this
        // increase is assumed to behave like a full DC_LUT_RW_INDEX write, it
        // probably should. Currently this also calls WriteRegister for
        // DC_LUT_RW_INDEX, which resets gamma_ramp_rw_component_ as well.
        gamma_ramp_rw_component_ = 0;
        reg::DC_LUT_RW_INDEX new_gamma_ramp_rw_index = gamma_ramp_rw_index;
        ++new_gamma_ramp_rw_index.rw_index;
        WriteRegister(
            XE_GPU_REG_DC_LUT_RW_INDEX,
            xe::memory::Reinterpret<uint32_t>(new_gamma_ramp_rw_index));
        if (gamma_ramp_write_enable_mask) {
          OnGammaRamp256EntryTableValueWritten();
        }
      } break;
    }
  }
}

void CommandProcessor::MakeCoherent() {
  SCOPE_profile_cpu_f("gpu");

  // Status host often has 0x01000000 or 0x03000000.
  // This is likely toggling VC (vertex cache) or TC (texture cache).
  // Or, it also has a direction in here maybe - there is probably
  // some way to check for dest coherency (what all the COHER_DEST_BASE_*
  // registers are for).
  // Best docs I've found on this are here:
  // https://web.archive.org/web/20160711162346/https://amd-dev.wpengine.netdna-cdn.com/wordpress/media/2013/10/R6xx_R7xx_3D.pdf
  // https://cgit.freedesktop.org/xorg/driver/xf86-video-radeonhd/tree/src/r6xx_accel.c?id=3f8b6eccd9dba116cc4801e7f80ce21a879c67d2#n454

  // Volatile because this may be called from the WAIT_REG_MEM loop.
  volatile uint32_t* regs_volatile = register_file_->values;
  auto status_host = xe::memory::Reinterpret<reg::COHER_STATUS_HOST>(
      uint32_t(regs_volatile[XE_GPU_REG_COHER_STATUS_HOST]));
  uint32_t base_host = regs_volatile[XE_GPU_REG_COHER_BASE_HOST];
  uint32_t size_host = regs_volatile[XE_GPU_REG_COHER_SIZE_HOST];

  if (!status_host.status) {
    return;
  }

  const char* action = "N/A";
  if (status_host.vc_action_ena && status_host.tc_action_ena) {
    action = "VC | TC";
  } else if (status_host.tc_action_ena) {
    action = "TC";
  } else if (status_host.vc_action_ena) {
    action = "VC";
  }

  // TODO(benvanik): notify resource cache of base->size and type.
  XELOGD("Make {:08X} -> {:08X} ({}b) coherent, action = {}", base_host,
         base_host + size_host, size_host, action);

  // Mark coherent.
  regs_volatile[XE_GPU_REG_COHER_STATUS_HOST] = 0;
}

void CommandProcessor::PrepareForWait() { trace_writer_.Flush(); }

void CommandProcessor::ReturnFromWait() {}

uint32_t CommandProcessor::ExecutePrimaryBuffer(uint32_t read_index,
                                                uint32_t write_index) {
  SCOPE_profile_cpu_f("gpu");

  // If we have a pending trace stream open it now. That way we ensure we get
  // all commands.
  if (!trace_writer_.is_open() && trace_state_ == TraceState::kStreaming) {
    uint32_t title_id = kernel_state_->GetExecutableModule()
                            ? kernel_state_->GetExecutableModule()->title_id()
                            : 0;
    auto file_name = fmt::format("{:08X}_stream.xtr", title_id);
    auto path = trace_stream_path_ / file_name;
    trace_writer_.Open(path, title_id);
    InitializeTrace();
  }

  // Adjust pointer base.
  uint32_t start_ptr = primary_buffer_ptr_ + read_index * sizeof(uint32_t);
  start_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (start_ptr & 0x1FFFFFFF);
  uint32_t end_ptr = primary_buffer_ptr_ + write_index * sizeof(uint32_t);
  end_ptr = (primary_buffer_ptr_ & ~0x1FFFFFFF) | (end_ptr & 0x1FFFFFFF);

  trace_writer_.WritePrimaryBufferStart(start_ptr, write_index - read_index);

  // Execute commands!
  RingBuffer reader(memory_->TranslatePhysical(primary_buffer_ptr_),
                    primary_buffer_size_);
  reader.set_read_offset(read_index * sizeof(uint32_t));
  reader.set_write_offset(write_index * sizeof(uint32_t));
  do {
    if (!ExecutePacket(&reader)) {
      // This probably should be fatal - but we're going to continue anyways.
      XELOGE("**** PRIMARY RINGBUFFER: Failed to execute packet.");
      assert_always();
      break;
    }
  } while (reader.read_count());

  OnPrimaryBufferEnd();

  trace_writer_.WritePrimaryBufferEnd();

  return write_index;
}

void CommandProcessor::UpdatePrimaryReadPointer(uint32_t read_index,
                                                const char* reason) {
  read_ptr_index_ = read_index;
  const_cast<volatile uint32_t&>(
      register_file_->values[kCpRbRptrRegister]) = read_ptr_index_;

  // TODO(benvanik): use reader->Read_update_freq_ and only issue after moving
  //     that many indices.
  if (read_ptr_writeback_ptr_) {
    xe::store_and_swap<uint32_t>(
        memory_->TranslatePhysical(read_ptr_writeback_ptr_), read_ptr_index_);
  }
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: CommandProcessor read pointer writeback "
        "read_ptr={:08X} ptr={:08X} reason={}",
        read_ptr_index_, read_ptr_writeback_ptr_, reason ? reason : "");
  }
}

void CommandProcessor::ExecuteIndirectBuffer(uint32_t ptr, uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  trace_writer_.WriteIndirectBufferStart(ptr, count * sizeof(uint32_t));
  if (cvars::gpu_trace_swap) {
    uint32_t* dwords =
        reinterpret_cast<uint32_t*>(memory_->TranslatePhysical(ptr));
    uint32_t dword_0 = count > 0 ? xe::byte_swap(dwords[0]) : 0;
    uint32_t dword_1 = count > 1 ? xe::byte_swap(dwords[1]) : 0;
    uint32_t dword_2 = count > 2 ? xe::byte_swap(dwords[2]) : 0;
    uint32_t dword_3 = count > 3 ? xe::byte_swap(dwords[3]) : 0;
    XELOGI(
        "GPU swap trace: ExecuteIndirectBuffer begin ptr={:08X} "
        "count={} first={:08X},{:08X},{:08X},{:08X}",
        ptr, count, dword_0, dword_1, dword_2, dword_3);
  }

  // Execute commands!
  RingBuffer reader(memory_->TranslatePhysical(ptr), count * sizeof(uint32_t));
  reader.set_write_offset(count * sizeof(uint32_t));
  do {
    if (!ExecutePacket(&reader)) {
      // Return up a level if we encounter a bad packet.
      XELOGE("**** INDIRECT RINGBUFFER: Failed to execute packet.");
      assert_always();
      break;
    }
  } while (reader.read_count());

  trace_writer_.WriteIndirectBufferEnd();
  if (cvars::gpu_trace_swap) {
    XELOGI("GPU swap trace: ExecuteIndirectBuffer end ptr={:08X} count={}",
           ptr, count);
  }
}

void CommandProcessor::ExecutePacket(uint32_t ptr, uint32_t count) {
  // Execute commands!
  RingBuffer reader(memory_->TranslatePhysical(ptr), count * sizeof(uint32_t));
  reader.set_write_offset(count * sizeof(uint32_t));
  do {
    if (!ExecutePacket(&reader)) {
      XELOGE("**** ExecutePacket: Failed to execute packet.");
      assert_always();
      break;
    }
  } while (reader.read_count());
}

bool CommandProcessor::ExecutePacket(RingBuffer* reader) {
  const uint32_t packet = reader->ReadAndSwap<uint32_t>();
  const uint32_t packet_type = packet >> 30;
  const uint32_t packet_ptr = uint32_t(reader->read_ptr() - 4);
  if (packet_type != 3 && ShouldTraceGpuPacket()) {
    XELOGI(
        "GPU swap trace: PM4 packet ptr={:08X} type={} word={:08X} "
        "remaining_bytes={}",
        packet_ptr, packet_type, packet, reader->read_count());
  }
  if (packet == 0) {
    trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1);
    trace_writer_.WritePacketEnd();
    return true;
  }

  if (packet == 0xCDCDCDCD) {
    XELOGW("GPU packet is CDCDCDCD - probably read uninitialized memory!");
  }

  switch (packet_type) {
    case 0x00:
      return ExecutePacketType0(reader, packet);
    case 0x01:
      return ExecutePacketType1(reader, packet);
    case 0x02:
      return ExecutePacketType2(reader, packet);
    case 0x03:
      return ExecutePacketType3(reader, packet);
    default:
      assert_unhandled_case(packet_type);
      return false;
  }
}

bool CommandProcessor::ExecutePacketType0(RingBuffer* reader, uint32_t packet) {
  // Type-0 packet.
  // Write count registers in sequence to the registers starting at
  // (base_index << 2).

  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  if (reader->read_count() < count * sizeof(uint32_t)) {
    XELOGE(
        "ExecutePacketType0 overflow (read count {:08X}, packet count {:08X})",
        reader->read_count(), count * sizeof(uint32_t));
    return false;
  }

  trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1 + count);

  uint32_t base_index = (packet & 0x7FFF);
  uint32_t write_one_reg = (packet >> 15) & 0x1;
  if (cvars::gpu_bulk_pm4_type0 && !write_one_reg) {
    // Hot path for draw-heavy guests (e.g. Blue Dragon, ~10k draws/frame): the
    // contiguous register run dominates. Byte-swap the whole dword run at once
    // with the NEON-vectorized copy_and_swap (vqtbl, 4 dwords/iter on ARM64)
    // instead of paying the per-dword ReadAndSwap overhead. WriteRegister still
    // runs per register so all per-register side effects are preserved (scratch
    // writeback, COHER dirty, and the Vulkan override's constant/texture
    // invalidation).
    uint32_t swapped[256];
    uint32_t remaining = count;
    uint32_t reg = base_index;
    while (remaining) {
      uint32_t chunk = std::min<uint32_t>(remaining, 256);
      size_t chunk_bytes = chunk * sizeof(uint32_t);
      if (reader->read_offset() + chunk_bytes <= reader->capacity()) {
        // No ring wrap: read the contiguous dword block directly and bulk-swap.
        xe::copy_and_swap_32_unaligned(
            swapped, reinterpret_cast<const void*>(reader->read_ptr()), chunk);
        reader->AdvanceRead(chunk_bytes);
      } else {
        // Block straddles the ring tail; fall back to per-dword read for it.
        for (uint32_t m = 0; m < chunk; ++m) {
          swapped[m] = reader->ReadAndSwap<uint32_t>();
        }
      }
      for (uint32_t m = 0; m < chunk; ++m) {
        WriteRegister(reg + m, swapped[m]);
      }
      reg += chunk;
      remaining -= chunk;
    }
  } else {
    for (uint32_t m = 0; m < count; m++) {
      uint32_t reg_data = reader->ReadAndSwap<uint32_t>();
      uint32_t target_index = write_one_reg ? base_index : base_index + m;
      WriteRegister(target_index, reg_data);
    }
  }

  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType1(RingBuffer* reader, uint32_t packet) {
  // Type-1 packet.
  // Contains two registers of data. Type-0 should be more common.
  trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 3);
  uint32_t reg_index_1 = packet & 0x7FF;
  uint32_t reg_index_2 = (packet >> 11) & 0x7FF;
  uint32_t reg_data_1 = reader->ReadAndSwap<uint32_t>();
  uint32_t reg_data_2 = reader->ReadAndSwap<uint32_t>();
  WriteRegister(reg_index_1, reg_data_1);
  WriteRegister(reg_index_2, reg_data_2);
  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType2(RingBuffer* reader, uint32_t packet) {
  // Type-2 packet.
  // No-op. Do nothing.
  trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1);
  trace_writer_.WritePacketEnd();
  return true;
}

bool CommandProcessor::ExecutePacketType3(RingBuffer* reader, uint32_t packet) {
  // Type-3 packet.
  uint32_t opcode = (packet >> 8) & 0x7F;
  uint32_t count = ((packet >> 16) & 0x3FFF) + 1;
  auto data_start_offset = reader->read_offset();
  if (ShouldTraceGpuPacket()) {
    XELOGI(
        "GPU swap trace: PM4 packet ptr={:08X} type=3 opcode={}({:02X}) "
        "count={} predicated={} word={:08X} remaining_bytes={}",
        uint32_t(reader->read_ptr() - 4), Type3OpcodeName(opcode), opcode,
        count, (packet & 1) != 0, packet, reader->read_count());
  }

  if (reader->read_count() < count * sizeof(uint32_t)) {
    XELOGE(
        "ExecutePacketType3 overflow (read count {:08X}, packet count {:08X})",
        reader->read_count(), count * sizeof(uint32_t));
    return false;
  }

  // To handle nesting behavior when tracing we special case indirect buffers.
  if (opcode == PM4_INDIRECT_BUFFER) {
    trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 2);
  } else {
    trace_writer_.WritePacketStart(uint32_t(reader->read_ptr() - 4), 1 + count);
  }

  // & 1 == predicate - when set, we do bin check to see if we should execute
  // the packet. Only type 3 packets are affected.
  // We also skip predicated swaps, as they are never valid (probably?).
  if (packet & 1) {
    bool any_pass = (bin_select_ & bin_mask_) != 0;
    if (!any_pass || opcode == PM4_XE_SWAP) {
      reader->AdvanceRead(count * sizeof(uint32_t));
      trace_writer_.WritePacketEnd();
      return true;
    }
  }

  bool result = false;
  switch (opcode) {
    case PM4_ME_INIT:
      result = ExecutePacketType3_ME_INIT(reader, packet, count);
      break;
    case PM4_NOP:
      result = ExecutePacketType3_NOP(reader, packet, count);
      break;
    case PM4_INTERRUPT:
      result = ExecutePacketType3_INTERRUPT(reader, packet, count);
      break;
    case PM4_XE_SWAP:
      result = ExecutePacketType3_XE_SWAP(reader, packet, count);
      break;
    case PM4_INDIRECT_BUFFER:
    case PM4_INDIRECT_BUFFER_PFD:
      result = ExecutePacketType3_INDIRECT_BUFFER(reader, packet, count);
      break;
    case PM4_WAIT_REG_MEM:
      result = ExecutePacketType3_WAIT_REG_MEM(reader, packet, count);
      break;
    case PM4_REG_RMW:
      result = ExecutePacketType3_REG_RMW(reader, packet, count);
      break;
    case PM4_REG_TO_MEM:
      result = ExecutePacketType3_REG_TO_MEM(reader, packet, count);
      break;
    case PM4_MEM_WRITE:
      result = ExecutePacketType3_MEM_WRITE(reader, packet, count);
      break;
    case PM4_COND_WRITE:
      result = ExecutePacketType3_COND_WRITE(reader, packet, count);
      break;
    case PM4_EVENT_WRITE:
      result = ExecutePacketType3_EVENT_WRITE(reader, packet, count);
      break;
    case PM4_EVENT_WRITE_SHD:
      result = ExecutePacketType3_EVENT_WRITE_SHD(reader, packet, count);
      break;
    case PM4_EVENT_WRITE_EXT:
      result = ExecutePacketType3_EVENT_WRITE_EXT(reader, packet, count);
      break;
    case PM4_EVENT_WRITE_ZPD:
      result = ExecutePacketType3_EVENT_WRITE_ZPD(reader, packet, count);
      break;
    case PM4_DRAW_INDX:
      result = ExecutePacketType3_DRAW_INDX(reader, packet, count);
      break;
    case PM4_DRAW_INDX_2:
      result = ExecutePacketType3_DRAW_INDX_2(reader, packet, count);
      break;
    case PM4_SET_CONSTANT:
      result = ExecutePacketType3_SET_CONSTANT(reader, packet, count);
      break;
    case PM4_SET_CONSTANT2:
      result = ExecutePacketType3_SET_CONSTANT2(reader, packet, count);
      break;
    case PM4_LOAD_ALU_CONSTANT:
      result = ExecutePacketType3_LOAD_ALU_CONSTANT(reader, packet, count);
      break;
    case PM4_SET_SHADER_CONSTANTS:
      result = ExecutePacketType3_SET_SHADER_CONSTANTS(reader, packet, count);
      break;
    case PM4_IM_LOAD:
      result = ExecutePacketType3_IM_LOAD(reader, packet, count);
      break;
    case PM4_IM_LOAD_IMMEDIATE:
      result = ExecutePacketType3_IM_LOAD_IMMEDIATE(reader, packet, count);
      break;
    case PM4_INVALIDATE_STATE:
      result = ExecutePacketType3_INVALIDATE_STATE(reader, packet, count);
      break;
    case PM4_VIZ_QUERY:
      result = ExecutePacketType3_VIZ_QUERY(reader, packet, count);
      break;

    case PM4_SET_BIN_MASK_LO: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_mask_ = (bin_mask_ & 0xFFFFFFFF00000000ull) | value;
      result = true;
    } break;
    case PM4_SET_BIN_MASK_HI: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_mask_ =
          (bin_mask_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_LO: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_select_ = (bin_select_ & 0xFFFFFFFF00000000ull) | value;
      result = true;
    } break;
    case PM4_SET_BIN_SELECT_HI: {
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      bin_select_ =
          (bin_select_ & 0xFFFFFFFFull) | (static_cast<uint64_t>(value) << 32);
      result = true;
    } break;
    case PM4_SET_BIN_MASK: {
      assert_true(count == 2);
      uint64_t val_hi = reader->ReadAndSwap<uint32_t>();
      uint64_t val_lo = reader->ReadAndSwap<uint32_t>();
      bin_mask_ = (val_hi << 32) | val_lo;
      result = true;
    } break;
    case PM4_SET_BIN_SELECT: {
      assert_true(count == 2);
      uint64_t val_hi = reader->ReadAndSwap<uint32_t>();
      uint64_t val_lo = reader->ReadAndSwap<uint32_t>();
      bin_select_ = (val_hi << 32) | val_lo;
      result = true;
    } break;
    case PM4_CONTEXT_UPDATE: {
      assert_true(count == 1);
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      XELOGGPU("GPU context update = {:08X}", value);
      assert_true(value == 0);
      result = true;
      break;
    }
    case PM4_WAIT_FOR_IDLE: {
      // This opcode is used by 5454084E while going / being ingame.
      assert_true(count == 1);
      uint32_t value = reader->ReadAndSwap<uint32_t>();
      XELOGGPU("GPU wait for idle = {:08X}", value);
      result = true;
      break;
    }

    default:
      XELOGGPU("Unimplemented GPU OPCODE: 0x{:02X}\t\tCOUNT: {}\n", opcode,
               count);
      assert_always();
      reader->AdvanceRead(count * sizeof(uint32_t));
      break;
  }

  trace_writer_.WritePacketEnd();
  if (opcode == PM4_XE_SWAP) {
    // End the trace writer frame.
    if (trace_writer_.is_open()) {
      trace_writer_.WriteEvent(EventCommand::Type::kSwap);
      trace_writer_.Flush();
      if (trace_state_ == TraceState::kSingleFrame) {
        trace_state_ = TraceState::kDisabled;
        trace_writer_.Close();
      }
    } else if (trace_state_ == TraceState::kSingleFrame) {
      // New trace request - we only start tracing at the beginning of a frame.
      uint32_t title_id = kernel_state_->GetExecutableModule()->title_id();
      auto file_name = fmt::format("{:08X}_{}.xtr", title_id, counter_ - 1);
      auto path = trace_frame_path_ / file_name;
      trace_writer_.Open(path, title_id);
      InitializeTrace();
    }
  }

  assert_true(reader->read_offset() ==
              (data_start_offset + (count * sizeof(uint32_t))) %
                  reader->capacity());
  return result;
}

bool CommandProcessor::ExecutePacketType3_ME_INIT(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // initialize CP's micro-engine
  me_bin_.clear();
  for (uint32_t i = 0; i < count; i++) {
    me_bin_.push_back(reader->ReadAndSwap<uint32_t>());
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3_NOP(RingBuffer* reader,
                                              uint32_t packet, uint32_t count) {
  // skip N 32-bit words to get to the next packet
  // No-op, ignore some data.
  reader->AdvanceRead(count * sizeof(uint32_t));
  return true;
}

bool CommandProcessor::ExecutePacketType3_INTERRUPT(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // generate interrupt from the command stream
  uint32_t cpu_mask = reader->ReadAndSwap<uint32_t>();
  if (ShouldTraceGpuInterruptPacket()) {
    XELOGI(
        "GPU interrupt trace: PM4_INTERRUPT cpu_mask={:08X} count={} "
        "read_ptr={:08X} read_offset={:08X} counter={:08X} "
        "rb_rptr={:08X} rb_wptr={:08X}",
        cpu_mask, count, read_ptr_index_, uint32_t(reader->read_offset()),
        counter_, uint32_t(register_file_->values[kCpRbRptrRegister]),
        uint32_t(register_file_->values[kCpRbWptrRegister]));
  }
  for (int n = 0; n < 6; n++) {
    if (cpu_mask & (1 << n)) {
      graphics_system_->DispatchInterruptCallback(1, n);
    }
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_XE_SWAP(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  if (cvars::gpu_trace_swap) {
    XELOGI("XE_SWAP");
  }

  Profiler::Flip();

  // Xenia-specific VdSwap hook.
  // VdSwap will post this to tell us we need to swap the screen/fire an
  // interrupt.
  // 63 words here, but only the first has any data.
  uint32_t magic = reader->ReadAndSwap<fourcc_t>();
  assert_true(magic == kSwapSignature);

  // TODO(benvanik): only swap frontbuffer ptr.
  uint32_t frontbuffer_ptr = reader->ReadAndSwap<uint32_t>();
  uint32_t frontbuffer_width = reader->ReadAndSwap<uint32_t>();
  uint32_t frontbuffer_height = reader->ReadAndSwap<uint32_t>();
  uint32_t display_width = frontbuffer_width;
  uint32_t display_height = frontbuffer_height;
  if (count >= 6) {
    display_width = reader->ReadAndSwap<uint32_t>();
    display_height = reader->ReadAndSwap<uint32_t>();
    reader->AdvanceRead((count - 6) * sizeof(uint32_t));
  } else if (count > 4) {
    reader->AdvanceRead((count - 4) * sizeof(uint32_t));
  }
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: XE_SWAP packet magic={:08X} frontbuffer={:08X} "
        "size={}x{} display={}x{} count={} read_ptr={:08X} "
        "read_offset={:08X}",
        magic, frontbuffer_ptr, frontbuffer_width, frontbuffer_height,
        display_width, display_height, count, uint32_t(reader->read_ptr()),
        uint32_t(reader->read_offset()));
  }
  TraceSwapProbeCvarsOnce();
  TraceSwapFrontbufferChecksum(memory_, frontbuffer_ptr, frontbuffer_width,
                               frontbuffer_height);
  TraceSwapRenderTargets(register_file_, frontbuffer_ptr, frontbuffer_width,
                         frontbuffer_height);

  IssueSwap(frontbuffer_ptr, frontbuffer_width, frontbuffer_height,
            display_width, display_height);

  ++counter_;
  if (ShouldTraceGpuInterruptPacket()) {
    XELOGI(
        "GPU interrupt trace: XE_SWAP complete frontbuffer={:08X} size={}x{} "
        "counter={:08X} read_ptr={:08X} read_offset={:08X} "
        "swap_interrupt={}",
        frontbuffer_ptr, frontbuffer_width, frontbuffer_height, counter_,
        read_ptr_index_, uint32_t(reader->read_offset()),
        cvars::gpu_interrupt_on_swap);
  }
  if (cvars::gpu_interrupt_on_swap) {
    graphics_system_->DispatchInterruptCallback(1, 2);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_INDIRECT_BUFFER(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  // indirect buffer dispatch
  uint32_t raw_list_ptr = reader->ReadAndSwap<uint32_t>();
  uint32_t list_ptr = CpuToGpu(raw_list_ptr);
  uint32_t list_length = reader->ReadAndSwap<uint32_t>();
  assert_zero(list_length & ~0xFFFFF);
  list_length &= 0xFFFFF;
  bool primary_reader =
      reader->buffer() == memory_->TranslatePhysical(primary_buffer_ptr_);
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: INDIRECT_BUFFER raw={:08X} gpu={:08X} cpu={:08X} "
        "length={} primary={} read_offset={:08X}",
        raw_list_ptr, list_ptr, GpuToCpu(list_ptr), list_length,
        primary_reader, uint32_t(reader->read_offset()));
  }
  if (cvars::gpu_early_primary_read_pointer_writeback && primary_reader) {
    UpdatePrimaryReadPointer(uint32_t(reader->read_offset() / sizeof(uint32_t)),
                             "before_indirect_buffer");
  }
  ExecuteIndirectBuffer(GpuToCpu(list_ptr), list_length);
  return true;
}

bool CommandProcessor::ExecutePacketType3_WAIT_REG_MEM(RingBuffer* reader,
                                                       uint32_t packet,
                                                       uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // wait until a register or memory location is a specific value

  uint32_t wait_info = reader->ReadAndSwap<uint32_t>();
  uint32_t poll_reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t ref = reader->ReadAndSwap<uint32_t>();
  uint32_t mask = reader->ReadAndSwap<uint32_t>();
  uint32_t wait = reader->ReadAndSwap<uint32_t>();

  bool is_memory = (wait_info & 0x10) != 0;

  assert_true(is_memory || poll_reg_addr < RegisterFile::kRegisterCount);
  const volatile uint32_t& value_ref =
      is_memory ? *reinterpret_cast<uint32_t*>(memory_->TranslatePhysical(
                      poll_reg_addr & ~uint32_t(0x3)))
                : register_file_->values[poll_reg_addr];

  bool matched = false;
  uint32_t wait_loops = 0;
  uint32_t last_value = 0;
  do {
    uint32_t value = value_ref;
    if (is_memory) {
      trace_writer_.WriteMemoryRead(CpuToGpu(poll_reg_addr & ~uint32_t(0x3)),
                                    sizeof(uint32_t));
      value = xenos::GpuSwap(value,
                             static_cast<xenos::Endian>(poll_reg_addr & 0x3));
    } else {
      if (poll_reg_addr == XE_GPU_REG_COHER_STATUS_HOST) {
        MakeCoherent();
        value = value_ref;
      }
    }
    last_value = value;
    switch (wait_info & 0x7) {
      case 0x0:  // Never.
        matched = false;
        break;
      case 0x1:  // Less than reference.
        matched = (value & mask) < ref;
        break;
      case 0x2:  // Less than or equal to reference.
        matched = (value & mask) <= ref;
        break;
      case 0x3:  // Equal to reference.
        matched = (value & mask) == ref;
        break;
      case 0x4:  // Not equal to reference.
        matched = (value & mask) != ref;
        break;
      case 0x5:  // Greater than or equal to reference.
        matched = (value & mask) >= ref;
        break;
      case 0x6:  // Greater than reference.
        matched = (value & mask) > ref;
        break;
      case 0x7:  // Always
        matched = true;
        break;
    }
    if (!matched) {
      ++wait_loops;
      // Wait.
      if (wait >= 0x100) {
        PrepareForWait();
        if (!cvars::vsync) {
          // User wants it fast and dangerous.
          xe::threading::MaybeYield();
        } else {
          xe::threading::Sleep(std::chrono::milliseconds(wait / 0x100));
        }
        xe::threading::SyncMemory();
        ReturnFromWait();

        if (!worker_running_) {
          // Short-circuited exit.
          return false;
        }
      } else {
        xe::threading::MaybeYield();
      }
    }
  } while (!matched);

  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: WAIT_REG_MEM info={:08X} poll={:08X} ref={:08X} "
        "mask={:08X} wait={:08X} memory={} final={:08X} loops={}",
        wait_info, poll_reg_addr, ref, mask, wait, is_memory, last_value,
        wait_loops);
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3_REG_RMW(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  // register read/modify/write
  // ? (used during shader upload and edram setup)
  uint32_t rmw_info = reader->ReadAndSwap<uint32_t>();
  uint32_t and_mask = reader->ReadAndSwap<uint32_t>();
  uint32_t or_mask = reader->ReadAndSwap<uint32_t>();
  uint32_t value = register_file_->values[rmw_info & 0x1FFF];
  if ((rmw_info >> 31) & 0x1) {
    // & reg
    value &= register_file_->values[and_mask & 0x1FFF];
  } else {
    // & imm
    value &= and_mask;
  }
  if ((rmw_info >> 30) & 0x1) {
    // | reg
    value |= register_file_->values[or_mask & 0x1FFF];
  } else {
    // | imm
    value |= or_mask;
  }
  WriteRegister(rmw_info & 0x1FFF, value);
  return true;
}

bool CommandProcessor::ExecutePacketType3_REG_TO_MEM(RingBuffer* reader,
                                                     uint32_t packet,
                                                     uint32_t count) {
  // Copy Register to Memory (?)
  // Count is 2, assuming a Register Addr and a Memory Addr.

  uint32_t reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t mem_addr = reader->ReadAndSwap<uint32_t>();

  uint32_t reg_val;

  assert_true(reg_addr < RegisterFile::kRegisterCount);
  reg_val = register_file_->values[reg_addr];

  auto endianness = static_cast<xenos::Endian>(mem_addr & 0x3);
  mem_addr &= ~0x3;
  reg_val = GpuSwap(reg_val, endianness);
  xe::store(memory_->TranslatePhysical(mem_addr), reg_val);
  trace_writer_.WriteMemoryWrite(CpuToGpu(mem_addr), 4);

  return true;
}

bool CommandProcessor::ExecutePacketType3_MEM_WRITE(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  uint32_t write_addr = reader->ReadAndSwap<uint32_t>();
  for (uint32_t i = 0; i < count - 1; i++) {
    uint32_t write_data = reader->ReadAndSwap<uint32_t>();

    auto endianness = static_cast<xenos::Endian>(write_addr & 0x3);
    auto addr = write_addr & ~0x3;
    write_data = GpuSwap(write_data, endianness);
    xe::store(memory_->TranslatePhysical(addr), write_data);
    trace_writer_.WriteMemoryWrite(CpuToGpu(addr), 4);
    write_addr += 4;
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3_COND_WRITE(RingBuffer* reader,
                                                     uint32_t packet,
                                                     uint32_t count) {
  // conditional write to memory or register
  uint32_t wait_info = reader->ReadAndSwap<uint32_t>();
  uint32_t poll_reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t ref = reader->ReadAndSwap<uint32_t>();
  uint32_t mask = reader->ReadAndSwap<uint32_t>();
  uint32_t write_reg_addr = reader->ReadAndSwap<uint32_t>();
  uint32_t write_data = reader->ReadAndSwap<uint32_t>();
  uint32_t value;
  if (wait_info & 0x10) {
    // Memory.
    auto endianness = static_cast<xenos::Endian>(poll_reg_addr & 0x3);
    poll_reg_addr &= ~0x3;
    trace_writer_.WriteMemoryRead(CpuToGpu(poll_reg_addr), 4);
    value = xe::load<uint32_t>(memory_->TranslatePhysical(poll_reg_addr));
    value = GpuSwap(value, endianness);
  } else {
    // Register.
    assert_true(poll_reg_addr < RegisterFile::kRegisterCount);
    value = register_file_->values[poll_reg_addr];
  }
  bool matched = false;
  switch (wait_info & 0x7) {
    case 0x0:  // Never.
      matched = false;
      break;
    case 0x1:  // Less than reference.
      matched = (value & mask) < ref;
      break;
    case 0x2:  // Less than or equal to reference.
      matched = (value & mask) <= ref;
      break;
    case 0x3:  // Equal to reference.
      matched = (value & mask) == ref;
      break;
    case 0x4:  // Not equal to reference.
      matched = (value & mask) != ref;
      break;
    case 0x5:  // Greater than or equal to reference.
      matched = (value & mask) >= ref;
      break;
    case 0x6:  // Greater than reference.
      matched = (value & mask) > ref;
      break;
    case 0x7:  // Always
      matched = true;
      break;
  }
  if (matched) {
    // Write.
    if (wait_info & 0x100) {
      // Memory.
      auto endianness = static_cast<xenos::Endian>(write_reg_addr & 0x3);
      write_reg_addr &= ~0x3;
      write_data = GpuSwap(write_data, endianness);
      xe::store(memory_->TranslatePhysical(write_reg_addr), write_data);
      trace_writer_.WriteMemoryWrite(CpuToGpu(write_reg_addr), 4);
    } else {
      // Register.
      WriteRegister(write_reg_addr, write_data);
    }
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE(RingBuffer* reader,
                                                      uint32_t packet,
                                                      uint32_t count) {
  // generate an event that creates a write to memory when completed
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  if (count == 1) {
    // Just an event flag? Where does this write?
  } else {
    // Write to an address.
    assert_always();
    reader->AdvanceRead((count - 1) * sizeof(uint32_t));
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_SHD(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  // generate a VS|PS_done event
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  uint32_t address = reader->ReadAndSwap<uint32_t>();
  uint32_t value = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  uint32_t data_value;
  if ((initiator >> 31) & 0x1) {
    // Write counter (GPU vblank counter?).
    data_value = counter_;
  } else {
    // Write value.
    data_value = value;
  }
  auto endianness = static_cast<xenos::Endian>(address & 0x3);
  address &= ~0x3;
  data_value = GpuSwap(data_value, endianness);
  xe::store(memory_->TranslatePhysical(address), data_value);
  trace_writer_.WriteMemoryWrite(CpuToGpu(address), 4);
  if (cvars::gpu_trace_swap) {
    XELOGI(
        "GPU swap trace: EVENT_WRITE_SHD initiator={:08X} address={:08X} "
        "value={:08X} stored={:08X} endian={}",
        initiator, address, value, data_value, uint32_t(endianness));
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_EXT(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  // generate a screen extent event
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  uint32_t address = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);
  auto endianness = static_cast<xenos::Endian>(address & 0x3);
  address &= ~0x3;

  // Let us hope we can fake this.
  // This callback tells the driver the xy coordinates affected by a previous
  // drawcall.
  // https://www.google.com/patents/US20060055701
  uint16_t extents[] = {
      0 >> 3,                                    // min x
      xenos::kTexture2DCubeMaxWidthHeight >> 3,  // max x
      0 >> 3,                                    // min y
      xenos::kTexture2DCubeMaxWidthHeight >> 3,  // max y
      0,                                         // min z
      1,                                         // max z
  };
  assert_true(endianness == xenos::Endian::k8in16);
  xe::copy_and_swap_16_unaligned(memory_->TranslatePhysical(address), extents,
                                 xe::countof(extents));
  trace_writer_.WriteMemoryWrite(CpuToGpu(address), sizeof(extents));
  return true;
}

bool CommandProcessor::ExecutePacketType3_EVENT_WRITE_ZPD(RingBuffer* reader,
                                                          uint32_t packet,
                                                          uint32_t count) {
  // Set by D3D as BE but struct ABI is LE
  const uint32_t kQueryFinished = xe::byte_swap(0xFFFFFEED);
  assert_true(count == 1);
  uint32_t initiator = reader->ReadAndSwap<uint32_t>();
  // Writeback initiator.
  WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, initiator & 0x3F);

  // Occlusion queries:
  // This command is send on query begin and end.
  // As a workaround report some fixed amount of passed samples.
  auto fake_sample_count = cvars::query_occlusion_fake_sample_count;
  if (fake_sample_count >= 0) {
    auto* pSampleCounts =
        memory_->TranslatePhysical<xe_gpu_depth_sample_counts*>(
            register_file_->values[XE_GPU_REG_RB_SAMPLE_COUNT_ADDR]);
    // 0xFFFFFEED is written to this two locations by D3D only on D3DISSUE_END
    // and used to detect a finished query.
    bool is_end_via_z_pass = pSampleCounts->ZPass_A == kQueryFinished &&
                             pSampleCounts->ZPass_B == kQueryFinished;
    // Older versions of D3D also checks for ZFail (4D5307D5).
    bool is_end_via_z_fail = pSampleCounts->ZFail_A == kQueryFinished &&
                             pSampleCounts->ZFail_B == kQueryFinished;
    std::memset(pSampleCounts, 0, sizeof(xe_gpu_depth_sample_counts));
    if (is_end_via_z_pass || is_end_via_z_fail) {
      pSampleCounts->ZPass_A = fake_sample_count;
      pSampleCounts->Total_A = fake_sample_count;
    }
  }

  return true;
}

bool CommandProcessor::ExecutePacketType3Draw(RingBuffer* reader,
                                              uint32_t packet,
                                              const char* opcode_name,
                                              uint32_t viz_query_condition,
                                              uint32_t count_remaining) {
  // if viz_query_condition != 0, this is a conditional draw based on viz query.
  // This ID matches the one issued in PM4_VIZ_QUERY
  // uint32_t viz_id = viz_query_condition & 0x3F;
  // when true, render conditionally based on query result
  // uint32_t viz_use = viz_query_condition & 0x100;

  assert_not_zero(count_remaining);
  if (!count_remaining) {
    XELOGE("{}: Packet too small, can't read VGT_DRAW_INITIATOR", opcode_name);
    return false;
  }
  reg::VGT_DRAW_INITIATOR vgt_draw_initiator;
  vgt_draw_initiator.value = reader->ReadAndSwap<uint32_t>();
  --count_remaining;
  WriteRegister(XE_GPU_REG_VGT_DRAW_INITIATOR, vgt_draw_initiator.value);

  bool draw_succeeded = true;
  // TODO(Triang3l): Remove IndexBufferInfo and replace handling of all this
  // with PrimitiveProcessor when the old Vulkan renderer is removed.
  bool is_indexed = false;
  IndexBufferInfo index_buffer_info;
  switch (vgt_draw_initiator.source_select) {
    case xenos::SourceSelect::kDMA: {
      // Indexed draw.
      is_indexed = true;

      // Two separate bounds checks so if there's only one missing register
      // value out of two, one uint32_t will be skipped in the command buffer,
      // not two.
      assert_not_zero(count_remaining);
      if (!count_remaining) {
        XELOGE("{}: Packet too small, can't read VGT_DMA_BASE", opcode_name);
        return false;
      }
      uint32_t vgt_dma_base = reader->ReadAndSwap<uint32_t>();
      --count_remaining;
      WriteRegister(XE_GPU_REG_VGT_DMA_BASE, vgt_dma_base);
      reg::VGT_DMA_SIZE vgt_dma_size;
      assert_not_zero(count_remaining);
      if (!count_remaining) {
        XELOGE("{}: Packet too small, can't read VGT_DMA_SIZE", opcode_name);
        return false;
      }
      vgt_dma_size.value = reader->ReadAndSwap<uint32_t>();
      --count_remaining;
      WriteRegister(XE_GPU_REG_VGT_DMA_SIZE, vgt_dma_size.value);

      uint32_t index_size_bytes =
          vgt_draw_initiator.index_size == xenos::IndexFormat::kInt16
              ? sizeof(uint16_t)
              : sizeof(uint32_t);
      // The base address must already be word-aligned according to the R6xx
      // documentation, but for safety.
      index_buffer_info.guest_base = vgt_dma_base & ~(index_size_bytes - 1);
      index_buffer_info.endianness = vgt_dma_size.swap_mode;
      index_buffer_info.format = vgt_draw_initiator.index_size;
      index_buffer_info.length = vgt_dma_size.num_words * index_size_bytes;
      index_buffer_info.count = vgt_draw_initiator.num_indices;
    } break;
    case xenos::SourceSelect::kImmediate: {
      // TODO(Triang3l): VGT_IMMED_DATA.
      XELOGE(
          "{}: Using immediate vertex indices, which are not supported yet. "
          "Report the game to Xenia developers!",
          opcode_name, uint32_t(vgt_draw_initiator.source_select));
      draw_succeeded = false;
      assert_always();
    } break;
    case xenos::SourceSelect::kAutoIndex: {
      // Auto draw.
      index_buffer_info.guest_base = 0;
      index_buffer_info.length = 0;
    } break;
    default: {
      // Invalid source selection.
      draw_succeeded = false;
      assert_unhandled_case(vgt_draw_initiator.source_select);
    } break;
  }

  // Skip to the next command, for example, if there are immediate indexes that
  // we don't support yet.
  reader->AdvanceRead(count_remaining * sizeof(uint32_t));

  if (draw_succeeded) {
    auto viz_query = register_file_->Get<reg::PA_SC_VIZ_QUERY>();
    if (!(viz_query.viz_query_ena && viz_query.kill_pix_post_hi_z)) {
      // TODO(Triang3l): Don't drop the draw call completely if the vertex
      // shader has memexport.
      // TODO(Triang3l || JoelLinn): Handle this properly in the render
      // backends.
      draw_succeeded = IssueDraw(
          vgt_draw_initiator.prim_type, vgt_draw_initiator.num_indices,
          is_indexed ? &index_buffer_info : nullptr,
          xenos::IsMajorModeExplicit(vgt_draw_initiator.major_mode,
                                     vgt_draw_initiator.prim_type));
      if (!draw_succeeded) {
        XELOGE("{}({}, {}, {}): Failed in backend", opcode_name,
               vgt_draw_initiator.num_indices,
               uint32_t(vgt_draw_initiator.prim_type),
               uint32_t(vgt_draw_initiator.source_select));
      }
    }
  }

  // If read the packed correctly, but merely couldn't execute it (because of,
  // for instance, features not supported by the host), don't terminate command
  // buffer processing as that would leave rendering in a way more inconsistent
  // state than just a single dropped draw command.
  return true;
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  // "initiate fetch of index buffer and draw"
  // Generally used by Xbox 360 Direct3D 9 for kDMA and kAutoIndex sources.
  // With a viz query token as the first one.
  uint32_t count_remaining = count;
  assert_not_zero(count_remaining);
  if (!count_remaining) {
    XELOGE("PM4_DRAW_INDX: Packet too small, can't read the viz query token");
    return false;
  }
  uint32_t viz_query_condition = reader->ReadAndSwap<uint32_t>();
  --count_remaining;
  return ExecutePacketType3Draw(reader, packet, "PM4_DRAW_INDX",
                                viz_query_condition, count_remaining);
}

bool CommandProcessor::ExecutePacketType3_DRAW_INDX_2(RingBuffer* reader,
                                                      uint32_t packet,
                                                      uint32_t count) {
  // "draw using supplied indices in packet"
  // Generally used by Xbox 360 Direct3D 9 for kAutoIndex source.
  // No viz query token.
  return ExecutePacketType3Draw(reader, packet, "PM4_DRAW_INDX_2", 0, count);
}

bool CommandProcessor::ExecutePacketType3_SET_CONSTANT(RingBuffer* reader,
                                                       uint32_t packet,
                                                       uint32_t count) {
  // load constant into chip and to memory
  // PM4_REG(reg) ((0x4 << 16) | (GSL_HAL_SUBBLOCK_OFFSET(reg)))
  //                                     reg - 0x2000
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0x7FF;
  uint32_t type = (offset_type >> 16) & 0xFF;
  switch (type) {
    case 0:  // ALU
      index += 0x4000;
      break;
    case 1:  // FETCH
      index += 0x4800;
      break;
    case 2:  // BOOL
      index += 0x4900;
      break;
    case 3:  // LOOP
      index += 0x4908;
      break;
    case 4:  // REGISTERS
      index += 0x2000;
      break;
    default:
      assert_always();
      reader->AdvanceRead((count - 1) * sizeof(uint32_t));
      return true;
  }
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->ReadAndSwap<uint32_t>();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_SET_CONSTANT2(RingBuffer* reader,
                                                        uint32_t packet,
                                                        uint32_t count) {
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0xFFFF;
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->ReadAndSwap<uint32_t>();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_LOAD_ALU_CONSTANT(RingBuffer* reader,
                                                            uint32_t packet,
                                                            uint32_t count) {
  // load constants from memory
  uint32_t address = reader->ReadAndSwap<uint32_t>();
  address &= 0x3FFFFFFF;
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0x7FF;
  uint32_t size_dwords = reader->ReadAndSwap<uint32_t>();
  size_dwords &= 0xFFF;
  uint32_t type = (offset_type >> 16) & 0xFF;
  switch (type) {
    case 0:  // ALU
      index += 0x4000;
      break;
    case 1:  // FETCH
      index += 0x4800;
      break;
    case 2:  // BOOL
      index += 0x4900;
      break;
    case 3:  // LOOP
      index += 0x4908;
      break;
    case 4:  // REGISTERS
      index += 0x2000;
      break;
    default:
      assert_always();
      return true;
  }
  trace_writer_.WriteMemoryRead(CpuToGpu(address), size_dwords * 4);
  for (uint32_t n = 0; n < size_dwords; n++, index++) {
    uint32_t data = xe::load_and_swap<uint32_t>(
        memory_->TranslatePhysical(address + n * 4));
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_SET_SHADER_CONSTANTS(
    RingBuffer* reader, uint32_t packet, uint32_t count) {
  uint32_t offset_type = reader->ReadAndSwap<uint32_t>();
  uint32_t index = offset_type & 0xFFFF;
  for (uint32_t n = 0; n < count - 1; n++, index++) {
    uint32_t data = reader->ReadAndSwap<uint32_t>();
    WriteRegister(index, data);
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_IM_LOAD(RingBuffer* reader,
                                                  uint32_t packet,
                                                  uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // load sequencer instruction memory (pointer-based)
  uint32_t addr_type = reader->ReadAndSwap<uint32_t>();
  auto shader_type = static_cast<xenos::ShaderType>(addr_type & 0x3);
  uint32_t addr = addr_type & ~0x3;
  uint32_t start_size = reader->ReadAndSwap<uint32_t>();
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  trace_writer_.WriteMemoryRead(CpuToGpu(addr), size_dwords * 4);
  auto shader =
      LoadShader(shader_type, addr, memory_->TranslatePhysical<uint32_t*>(addr),
                 size_dwords);
  switch (shader_type) {
    case xenos::ShaderType::kVertex:
      active_vertex_shader_ = shader;
      break;
    case xenos::ShaderType::kPixel:
      active_pixel_shader_ = shader;
      break;
    default:
      assert_unhandled_case(shader_type);
      return false;
  }
  return true;
}

bool CommandProcessor::ExecutePacketType3_IM_LOAD_IMMEDIATE(RingBuffer* reader,
                                                            uint32_t packet,
                                                            uint32_t count) {
  SCOPE_profile_cpu_f("gpu");

  // load sequencer instruction memory (code embedded in packet)
  uint32_t dword0 = reader->ReadAndSwap<uint32_t>();
  uint32_t dword1 = reader->ReadAndSwap<uint32_t>();
  auto shader_type = static_cast<xenos::ShaderType>(dword0);
  uint32_t start_size = dword1;
  uint32_t start = start_size >> 16;
  uint32_t size_dwords = start_size & 0xFFFF;  // dwords
  assert_true(start == 0);
  assert_true(reader->read_count() >= size_dwords * 4);
  assert_true(count - 2 >= size_dwords);
  auto shader =
      LoadShader(shader_type, uint32_t(reader->read_ptr()),
                 reinterpret_cast<uint32_t*>(reader->read_ptr()), size_dwords);
  switch (shader_type) {
    case xenos::ShaderType::kVertex:
      active_vertex_shader_ = shader;
      break;
    case xenos::ShaderType::kPixel:
      active_pixel_shader_ = shader;
      break;
    default:
      assert_unhandled_case(shader_type);
      return false;
  }
  reader->AdvanceRead(size_dwords * sizeof(uint32_t));
  return true;
}

bool CommandProcessor::ExecutePacketType3_INVALIDATE_STATE(RingBuffer* reader,
                                                           uint32_t packet,
                                                           uint32_t count) {
  // selective invalidation of state pointers
  /*uint32_t mask =*/reader->ReadAndSwap<uint32_t>();
  // driver_->InvalidateState(mask);
  return true;
}

bool CommandProcessor::ExecutePacketType3_VIZ_QUERY(RingBuffer* reader,
                                                    uint32_t packet,
                                                    uint32_t count) {
  // begin/end initiator for viz query extent processing
  // https://www.google.com/patents/US20050195186
  assert_true(count == 1);

  uint32_t dword0 = reader->ReadAndSwap<uint32_t>();

  uint32_t id = dword0 & 0x3F;
  uint32_t end = dword0 & 0x100;
  if (!end) {
    // begin a new viz query @ id
    // On hardware this clears the internal state of the scan converter (which
    // is different to the register)
    WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, VIZQUERY_START);
    XELOGGPU("Begin viz query ID {:02X}", id);
  } else {
    // end the viz query
    WriteRegister(XE_GPU_REG_VGT_EVENT_INITIATOR, VIZQUERY_END);
    XELOGGPU("End viz query ID {:02X}", id);
    // The scan converter writes the internal result back to the register here.
    // We just fake it and say it was visible in case it is read back.
    if (id < 32) {
      register_file_->values[XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_0] |= uint32_t(1)
                                                                     << id;
    } else {
      register_file_->values[XE_GPU_REG_PA_SC_VIZ_QUERY_STATUS_1] |=
          uint32_t(1) << (id - 32);
    }
  }

  return true;
}

void CommandProcessor::InitializeTrace() {
  // Write the initial register values, to be loaded directly into the
  // RegisterFile since all registers, including those that may have side
  // effects on setting, will be saved.
  trace_writer_.WriteRegisters(0, register_file_->values,
                               RegisterFile::kRegisterCount, false);

  trace_writer_.WriteGammaRamp(gamma_ramp_256_entry_table(),
                               gamma_ramp_pwl_rgb(), gamma_ramp_rw_component_);
}

}  // namespace gpu
}  // namespace xe
