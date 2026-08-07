/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/audio_system.h"

#include "xenia/apu/apu_flags.h"
#include "xenia/apu/audio_driver.h"
#include "xenia/apu/xma_decoder.h"
#include "xenia/base/assert.h"
#include "xenia/base/byte_stream.h"
#if XE_PLATFORM_ANDROID
#include <sys/resource.h>
#include <cerrno>
#endif

#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/profiling.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/string_buffer.h"
#include "xenia/base/threading.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/cpu/thread_state.h"

// As with normal Microsoft, there are like twelve different ways to access
// the audio APIs. Early games use XMA*() methods almost exclusively to touch
// decoders. Later games use XAudio*() and direct memory writes to the XMA
// structures (as opposed to the XMA* calls), meaning that we have to support
// both.
//
// For ease of implementation, most audio related processing is handled in
// AudioSystem, and the functions here call off to it.
// The XMA*() functions just manipulate the audio system in the guest context
// and let the normal AudioSystem handling take it, to prevent duplicate
// implementations. They can be found in xboxkrnl_audio_xma.cc

namespace xe {
namespace apu {

AudioSystem::AudioSystem(cpu::Processor* processor)
    : memory_(processor->memory()),
      processor_(processor),
      worker_running_(false) {
  std::memset(clients_, 0, sizeof(clients_));

  for (size_t i = 0; i < kMaximumClientCount; ++i) {
    client_semaphores_[i] =
        xe::threading::Semaphore::Create(0, kMaximumQueuedFrames);
    assert_not_null(client_semaphores_[i]);
    wait_handles_[i] = client_semaphores_[i].get();
  }
  shutdown_event_ = xe::threading::Event::CreateAutoResetEvent(false);
  assert_not_null(shutdown_event_);
  wait_handles_[kMaximumClientCount] = shutdown_event_.get();

  xma_decoder_ = std::make_unique<xe::apu::XmaDecoder>(processor_);

  resume_event_ = xe::threading::Event::CreateAutoResetEvent(false);
  assert_not_null(resume_event_);
}

AudioSystem::~AudioSystem() {
  if (xma_decoder_) {
    xma_decoder_->Shutdown();
  }
}

X_STATUS AudioSystem::Setup(kernel::KernelState* kernel_state) {
  X_STATUS result = xma_decoder_->Setup(kernel_state);
  if (result) {
    return result;
  }

  worker_running_ = true;
  // System process (xenia-edge parity): this runs before a title is loaded, and
  // the title process's X_KPROCESS is still zeroed until SetExecutableModule.
  worker_thread_ = kernel::object_ref<kernel::XHostThread>(
      new kernel::XHostThread(
          kernel_state, 128 * 1024, 0,
          [this]() {
            WorkerThreadMain();
            return 0;
          },
          kernel_state->GetSystemProcess()));
  // As we run audio callbacks the debugger must be able to suspend us.
  worker_thread_->set_can_debugger_suspend(true);
  worker_thread_->set_name("Audio Worker");
  worker_thread_->Create();

  return X_STATUS_SUCCESS;
}

void AudioSystem::WorkerThreadMain() {
  // Initialize driver and ringbuffer.
  Initialize();

#if XE_PLATFORM_ANDROID
  // Raise the audio worker's nice level, and do it from INSIDE the thread:
  // XThread applies its own priority after Create(), so anything set at the
  // creation site is overwritten. XenDroid bc257ce49 makes the same point.
  //
  // It matters more here than upstream: our threading_posix set_priority goes
  // through pthread_setschedparam, which the comment at threading_posix.cc:703
  // records as failing with EPERM for guest threads on Android - so this worker
  // currently gets NO effective priority at all while it is the thread feeding
  // a real-time AAudio callback. setpriority() is the API that does work for
  // nice values, and PRIO_PROCESS with pid 0 applies to the CALLING TASK on
  // Linux, i.e. this thread rather than the process.
  //
  // Failure is deliberately ignored: a denied nice request must not take the
  // audio system down, and the driver still runs (just more underrun-prone).
  if (setpriority(PRIO_PROCESS, 0, -12) != 0) {
    XELOGW("AudioSystem: could not raise audio worker priority (errno {})",
           errno);
  }
#endif

  // Main run loop.
  while (worker_running_) {
    // These handles signify the number of submitted samples. Once we reach
    // 64 samples, we wait until our audio backend releases a semaphore
    // (signaling a sample has finished playing)
    auto result =
        xe::threading::WaitAny(wait_handles_, xe::countof(wait_handles_), true);
    if (result.first == xe::threading::WaitResult::kFailed) {
      // TODO: Assert?
      continue;
    }

    if (result.first == threading::WaitResult::kSuccess &&
        result.second == kMaximumClientCount) {
      // Shutdown event signaled.
      if (paused_) {
        pause_fence_.Signal();
        threading::Wait(resume_event_.get(), false);
      }

      continue;
    }

    // Number of clients pumped
    bool pumped = false;
    if (result.first == xe::threading::WaitResult::kSuccess) {
      auto index = result.second;

      auto global_lock = global_critical_region_.Acquire();
      uint32_t client_callback = clients_[index].callback;
      uint32_t client_callback_arg = clients_[index].wrapped_callback_arg;
      global_lock.unlock();

      if (client_callback) {
        SCOPE_profile_cpu_i("apu", "xe::apu::AudioSystem->client_callback");
        uint64_t args[] = {client_callback_arg};
        processor_->Execute(worker_thread_->thread_state(), client_callback,
                            args, xe::countof(args));
      }

      pumped = true;
    }

    if (!worker_running_) {
      break;
    }

    if (!pumped) {
      SCOPE_profile_cpu_i("apu", "Sleep");
      xe::threading::Sleep(std::chrono::milliseconds(500));
    }
  }
  worker_running_ = false;

  // TODO(benvanik): call module API to kill?
}

int AudioSystem::FindFreeClient() {
  for (int i = 0; i < kMaximumClientCount; i++) {
    auto& client = clients_[i];
    if (!client.in_use) {
      return i;
    }
  }

  return -1;
}

void AudioSystem::Initialize() {}

void AudioSystem::Shutdown() {
  worker_running_ = false;
  shutdown_event_->Set();
  if (worker_thread_) {
    worker_thread_->Wait(0, 0, 0, nullptr);
    worker_thread_.reset();
  }

  // Unregister any clients the guest left active so their audio drivers are
  // torn down before this AudioSystem's client semaphores are destroyed -
  // otherwise a driver's OnBufferEnd can fire on a freed semaphore and crash
  // during shutdown. Mirrors UnregisterClient. Ported from xenia-canary 1662c7570.
  {
    auto global_lock = global_critical_region_.Acquire();
    for (size_t i = 0; i < kMaximumClientCount; ++i) {
      if (clients_[i].in_use) {
        DestroyDriver(clients_[i].driver);
        memory()->SystemHeapFree(clients_[i].wrapped_callback_arg);
        clients_[i] = {0};
      }
    }
  }
}

X_STATUS AudioSystem::RegisterClient(uint32_t callback, uint32_t callback_arg,
                                     size_t* out_index) {
  auto global_lock = global_critical_region_.Acquire();

  auto index = FindFreeClient();
  assert_true(index >= 0);

  auto client_semaphore = client_semaphores_[index].get();
  auto ret = client_semaphore->Release(kMaximumQueuedFrames, nullptr);
  assert_true(ret);

  AudioDriver* driver;
  auto result = CreateDriver(index, client_semaphore, &driver);
  if (XFAILED(result)) {
    return result;
  }
  assert_not_null(driver);

  uint32_t ptr = memory()->SystemHeapAlloc(0x4);
  xe::store_and_swap<uint32_t>(memory()->TranslateVirtual(ptr), callback_arg);

  clients_[index] = {driver, callback, callback_arg, ptr, true};

  if (out_index) {
    *out_index = index;
  }

  return X_STATUS_SUCCESS;
}

void AudioSystem::SubmitFrame(size_t index, uint32_t samples_ptr) {
  SCOPE_profile_cpu_f("apu");

  auto global_lock = global_critical_region_.Acquire();
  // Drop the frame gracefully instead of asserting / dereferencing a null
  // driver if the guest submits for an invalid or no-longer-registered client
  // (e.g. during teardown) - prevents a crash. Ported from xenia-canary
  // 64c59d3d0 (the silence-submit there is for a float* driver; ours takes a
  // guest sample pointer, and a truly-invalid client has no live semaphore).
  if (index >= kMaximumClientCount || !clients_[index].driver) {
    XELOGW(
        "AudioSystem::SubmitFrame: invalid client index {} (driver={}); "
        "dropping frame",
        index,
        index < kMaximumClientCount
            ? static_cast<void*>(clients_[index].driver)
            : nullptr);
    if (index < kMaximumClientCount) {
      ++clients_[index].frames_dropped;
    }
    return;
  }
  ++clients_[index].frames_submitted;
  ++clients_[index].frames_processed;
  (clients_[index].driver)->SubmitFrame(samples_ptr);
}

void AudioSystem::SubmitFrame(size_t index, float* samples) {
  // Edge kernel-port: the kernel shim translates the guest sample block to a
  // host pointer before calling us, but our AudioDriver interface consumes the
  // guest address (the drivers do their own TranslateVirtual). Translate back
  // so both paths converge on the guest-pointer implementation.
  if (!samples) {
    XELOGW("AudioSystem::SubmitFrame: null sample block for client {}", index);
    auto global_lock = global_critical_region_.Acquire();
    if (index < kMaximumClientCount) {
      ++clients_[index].frames_dropped;
    }
    return;
  }
  SubmitFrame(index, memory_->HostToGuestVirtual(samples));
}

bool AudioSystem::GetClientPerformance(size_t index,
                                       ClientPerformance* out_perf) {
  if (index >= kMaximumClientCount || !out_perf) {
    return false;
  }

  auto global_lock = global_critical_region_.Acquire();
  if (!clients_[index].in_use) {
    return false;
  }

  out_perf->frames_submitted = clients_[index].frames_submitted;
  out_perf->frames_processed = clients_[index].frames_processed;
  out_perf->frames_dropped = clients_[index].frames_dropped;
  return true;
}

void AudioSystem::UnregisterClient(size_t index) {
  SCOPE_profile_cpu_f("apu");

  auto global_lock = global_critical_region_.Acquire();
  assert_true(index < kMaximumClientCount);
  DestroyDriver(clients_[index].driver);
  memory()->SystemHeapFree(clients_[index].wrapped_callback_arg);
  clients_[index] = {0};

  // Drain the semaphore of its count.
  auto client_semaphore = client_semaphores_[index].get();
  xe::threading::WaitResult wait_result;
  do {
    wait_result = xe::threading::Wait(client_semaphore, false,
                                      std::chrono::milliseconds(0));
  } while (wait_result == xe::threading::WaitResult::kSuccess);
  assert_true(wait_result == xe::threading::WaitResult::kTimeout);
}

bool AudioSystem::Save(ByteStream* stream) {
  stream->Write(kAudioSaveSignature);

  // Count the number of used clients first.
  // Any gaps should be handled gracefully.
  uint32_t used_clients = 0;
  for (int i = 0; i < kMaximumClientCount; i++) {
    if (clients_[i].in_use) {
      used_clients++;
    }
  }

  stream->Write(used_clients);
  for (uint32_t i = 0; i < kMaximumClientCount; i++) {
    auto& client = clients_[i];
    if (!client.in_use) {
      continue;
    }

    stream->Write(i);
    stream->Write(client.callback);
    stream->Write(client.callback_arg);
    stream->Write(client.wrapped_callback_arg);
  }

  return true;
}

bool AudioSystem::Restore(ByteStream* stream) {
  if (stream->Read<uint32_t>() != kAudioSaveSignature) {
    XELOGE("AudioSystem::Restore - Invalid magic value!");
    return false;
  }

  uint32_t num_clients = stream->Read<uint32_t>();
  for (uint32_t i = 0; i < num_clients; i++) {
    auto id = stream->Read<uint32_t>();
    assert_true(id < kMaximumClientCount);

    auto& client = clients_[id];

    // Reset the semaphore and recreate the driver ourselves.
    if (client.driver) {
      UnregisterClient(id);
    }

    client.callback = stream->Read<uint32_t>();
    client.callback_arg = stream->Read<uint32_t>();
    client.wrapped_callback_arg = stream->Read<uint32_t>();

    client.in_use = true;

    auto client_semaphore = client_semaphores_[id].get();
    auto ret = client_semaphore->Release(kMaximumQueuedFrames, nullptr);
    assert_true(ret);

    AudioDriver* driver = nullptr;
    auto status = CreateDriver(id, client_semaphore, &driver);
    if (XFAILED(status)) {
      XELOGE(
          "AudioSystem::Restore - Call to CreateDriver failed with status "
          "{:08X}",
          status);
      return false;
    }

    assert_not_null(driver);
    client.driver = driver;
  }

  return true;
}

void AudioSystem::Pause() {
  if (paused_) {
    return;
  }
  paused_ = true;

  // Kind of a hack, but it works.
  shutdown_event_->Set();
  pause_fence_.Wait();

  xma_decoder_->Pause();
}

void AudioSystem::Resume() {
  if (!paused_) {
    return;
  }
  paused_ = false;

  resume_event_->Set();

  xma_decoder_->Resume();
}

}  // namespace apu
}  // namespace xe
