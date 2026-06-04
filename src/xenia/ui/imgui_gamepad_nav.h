/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Thor research fork contributors.                       *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IMGUI_GAMEPAD_NAV_H_
#define XENIA_UI_IMGUI_GAMEPAD_NAV_H_

#include <functional>

namespace xe {
namespace ui {

// A snapshot of gamepad navigation input for platforms whose controller input
// does NOT flow through the window/input-event system (Android routes the pad
// straight to the guest HID driver). When a provider is registered, ImGuiDrawer
// feeds these into ImGui's gamepad navigation each frame WHILE a dialog is open
// - so a physical controller can dismiss host ImGui dialogs such as guest XAM
// message boxes. ImGuiDrawer::Draw() early-outs when no dialog is open, so this
// never affects in-game controller input.
struct ImGuiGamepadNav {
  bool dpad_up = false;
  bool dpad_down = false;
  bool dpad_left = false;
  bool dpad_right = false;
  bool activate = false;  // bottom face button (Xbox A / cross) = press item
  bool cancel = false;    // right face button (Xbox B / circle) = back/cancel
};

// Register (or clear, with nullptr) the platform gamepad-nav source. Set once
// by the platform input layer at startup.
void SetImGuiGamepadNavProvider(std::function<ImGuiGamepadNav()> provider);

// True if a provider is registered (desktop leaves this false, so its ImGui
// gamepad handling is entirely untouched).
bool HasImGuiGamepadNavProvider();

// Current snapshot from the provider, or a released/empty state if none.
ImGuiGamepadNav PollImGuiGamepadNav();

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_IMGUI_GAMEPAD_NAV_H_
