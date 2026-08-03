/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_HID_INPUT_SYSTEM_H_
#define XENIA_HID_INPUT_SYSTEM_H_

#include <array>
#include <atomic>
#include <bitset>
#include <memory>
#include <mutex>
#include <vector>

#include "xenia/hid/input.h"
#include "xenia/hid/input_driver.h"
#include "xenia/hid/portal/portal.h"
#include "xenia/xbox.h"

namespace xe {
namespace ui {
class Window;
}  // namespace ui
}  // namespace xe

namespace xe {
namespace hid {

class InputSystem {
 public:
  explicit InputSystem(xe::ui::Window* window);
  ~InputSystem();

  xe::ui::Window* window() const { return window_; }

  X_STATUS Setup();

  void AddDriver(std::unique_ptr<InputDriver> driver);

  X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                           X_INPUT_CAPABILITIES* out_caps);
  // Edge signature: `flags` selects the device class the caller is asking for.
  // This fork's drivers do not advertise an input type (Edge's InputDriver has
  // GetInputType()/EnumerateDevices(); ours does not), so `flags` is accepted
  // and ignored - every driver is polled, as before.
  X_RESULT GetState(uint32_t user_index, uint32_t flags,
                    X_INPUT_STATE* out_state);
  // Legacy 2-argument form kept for hid_demo and any in-tree callers.
  X_RESULT GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
    return GetState(user_index, X_INPUT_FLAG_GAMEPAD, out_state);
  }
  // GetState variant for UI that bypasses the input blocker.
  X_RESULT GetStateForUI(uint32_t user_index, uint32_t flags,
                         X_INPUT_STATE* out_state);
  X_RESULT SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration);
  X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                        X_INPUT_KEYSTROKE* out_keystroke);

  // Block/unblock input to the game (for UI dialogs).
  void AddUIInputBlocker();
  void RemoveUIInputBlocker();

  // Slots that have reported a connected device at least once and have not
  // since reported a disconnect.
  const std::bitset<XUserMaxUserCount> GetConnectedSlots() const {
    return connected_slots_;
  }

  // Last slot that produced input; XamProfileManager resolves
  // XUserIndexLatest through this.
  uint32_t GetLastUsedSlot() const { return last_used_slot_; }

  // NOTE(kernel-port): always nullptr in this fork - see portal/portal.h for
  // the re-enable path.
  Portal* GetPortal() { return portal_.get(); }

  // Edge guards the whole input system with an xe_unlikely_mutex spinlock and
  // hands callers a lock via this method; the kernel input shims take it
  // around every InputSystem call. This fork has no xe_unlikely_mutex in
  // base/mutex.h, so a std::recursive_mutex is used instead (recursive so a
  // nested kernel path that re-locks cannot deadlock). Callers only ever use
  // it as `auto lock = input_system->lock();`.
  std::unique_lock<std::recursive_mutex> lock();

 private:
  // Tracks connect/disconnect edges for a guest slot.
  void UpdateUsedSlot(uint32_t slot, bool connected);

  const char* const controller_slot_state_change_message_[2] = {
      "Controller disconnected from slot {}.",
      "New controller connected to slot {}."};

  xe::ui::Window* window_ = nullptr;

  std::vector<std::unique_ptr<InputDriver>> drivers_;

  std::unique_ptr<Portal> portal_;

  std::bitset<XUserMaxUserCount> connected_slots_ = {};
  uint32_t last_used_slot_ = 0;

  std::recursive_mutex lock_;

  // Reference count for UI elements blocking game input.
  std::atomic<int> ui_input_blockers_{0};

  // Buttons that should be masked from game input until released (per slot).
  // This prevents button presses used to close UI dialogs from being seen by
  // the game immediately after the dialog closes.
  std::array<uint16_t, XUserMaxUserCount> consumed_buttons_{};
};

}  // namespace hid
}  // namespace xe

#endif  // XENIA_HID_INPUT_SYSTEM_H_
