/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_IMGUI_DRAWER_H_
#define XENIA_UI_IMGUI_DRAWER_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

// Edge kernel-port: Edge's imgui_drawer.h exposes ImGui types by value to its
// consumers (xam/ui dialogs), so the real header must be visible here rather
// than only forward-declared.
#include "third_party/imgui/imgui.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_listener.h"

struct ImDrawData;
struct ImGuiContext;
struct ImGuiIO;
enum ImGuiKey : int;

namespace xe {
namespace ui {

class ImGuiDialog;
class Window;

class ImGuiNotification;

// Edge kernel-port: image blobs (PNG bytes) keyed by an image id, uploaded to
// the immediate drawer in bulk by ImGuiDrawer::LoadIcons().
using IconsData = std::map<uint32_t, std::span<const uint8_t>>;

// Edge kernel-port: default on-screen size of profile/achievement/title icons.
constexpr ImVec2 default_image_icon_size = ImVec2(64.f, 64.f);

// NOTE(kernel-port): third_party/imgui in this tree is 1.89, which predates
// ImGui::SeparatorText() (added in 1.89.2) and ImGuiHoveredFlags_ForTooltip
// (added in 1.89.7). The two shims below reproduce the behavior with 1.89
// primitives so the ported xam/ui dialogs build.
// Re-enable path: upgrade third_party/imgui to >= 1.89.7, then replace
// xe::ui::SeparatorText() with ImGui::SeparatorText() and
// xe::ui::kHoveredFlagsForTooltip with ImGuiHoveredFlags_ForTooltip at the
// call sites (currently only src/xenia/kernel/xam/ui/gamercard_ui.cc), and
// delete these two declarations plus the definition in imgui_drawer.cc.
void SeparatorText(const char* label);
constexpr ImGuiHoveredFlags kHoveredFlagsForTooltip =
    ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled;

class ImGuiDrawer : public WindowInputListener, public UIDrawer {
 public:
  ImGuiDrawer(Window* window, size_t z_order);
  ~ImGuiDrawer();

  ImGuiIO& GetIO();

  void AddDialog(ImGuiDialog* dialog);
  // Edge kernel-port: guest/host notifications register here and are drawn
  // alongside dialogs (they derive from ImGuiDialog in this fork's tree).
  void AddNotification(ImGuiNotification* notification);
  void RemoveNotification(ImGuiNotification* notification);
  void EnableNotifications(bool enable) { are_notifications_enabled_ = enable; }
  void RemoveDialog(ImGuiDialog* dialog);

  // SetPresenter may be called from the destructor.
  void SetPresenter(Presenter* new_presenter);
  void SetImmediateDrawer(ImmediateDrawer* new_immediate_drawer);
  void SetPresenterAndImmediateDrawer(Presenter* new_presenter,
                                      ImmediateDrawer* new_immediate_drawer) {
    SetPresenter(new_presenter);
    SetImmediateDrawer(new_immediate_drawer);
  }

  void Draw(UIDrawContext& ui_draw_context) override;

  // Edge kernel-port: decodes an in-memory image (PNG/JPG/...) and uploads it
  // as an immediate-drawer texture. Returns null if there is no immediate
  // drawer attached yet or the blob could not be decoded.
  std::unique_ptr<ImmediateTexture> LoadImGuiIcon(std::span<const uint8_t> data);
  std::map<uint32_t, std::unique_ptr<ImmediateTexture>> LoadIcons(
      const IconsData& data);

  // Edge kernel-port: larger font used for headings in the xam/ui dialogs.
  // Falls back to the default font if the title font failed to load.
  ImFont* GetTitleFont() {
    ImFontAtlas* fonts = GetIO().Fonts;
    if (fonts->Fonts.Size < 2 || !fonts->Fonts[1]->IsLoaded()) {
      return fonts->Fonts[0];
    }
    return fonts->Fonts[1];
  }

 protected:
  void OnKeyDown(KeyEvent& e) override;
  void OnKeyUp(KeyEvent& e) override;
  void OnKeyChar(KeyEvent& e) override;
  void OnMouseDown(MouseEvent& e) override;
  void OnMouseMove(MouseEvent& e) override;
  void OnMouseUp(MouseEvent& e) override;
  void OnMouseWheel(MouseEvent& e) override;
  void OnTouchEvent(TouchEvent& e) override;
  // For now, no need for OnDpiChanged because redrawing is done continuously.

 private:
  void Initialize();

  void SetupFontTexture();

  void RenderDrawLists(ImDrawData* data, UIDrawContext& ui_draw_context);

  void ClearInput();
  void OnKey(KeyEvent& e, bool is_down);
  void UpdateMousePosition(float x, float y);
  void SwitchToPhysicalMouseAndUpdateMousePosition(const MouseEvent& e);

  bool IsDrawingDialogs() const { return dialog_loop_next_index_ != SIZE_MAX; }
  void DetachIfLastDialogRemoved();

  std::optional<ImGuiKey> VirtualKeyToImGuiKey(VirtualKey vkey);

  Window* window_;
  size_t z_order_;

  ImGuiContext* internal_state_ = nullptr;

  // All currently-attached dialogs that get drawn.
  std::vector<ImGuiDialog*> dialogs_;
  bool are_notifications_enabled_ = true;
  std::vector<ImGuiNotification*> notifications_;
  // Using an index, not an iterator, because after the erasure, the adjustment
  // must be done for the vector element indices that would be in the iterator
  // range that would be invalidated.
  // SIZE_MAX if not currently in the dialog loop.
  size_t dialog_loop_next_index_ = SIZE_MAX;

  Presenter* presenter_ = nullptr;

  ImmediateDrawer* immediate_drawer_ = nullptr;
  // Resources specific to an immediate drawer - must be destroyed before
  // detaching the presenter.

 public:
  // Edge kernel-port: guest/host notifications ask the drawer for per-user
  // icons. Edge loads an embedded icon atlas we did not port; return null so
  // notifications render text-only (ImGui::Image is skipped on null).
  ImmediateTexture* GetNotificationIcon(uint8_t user_index) { return nullptr; }
  ImmediateTexture* GetLockedAchievementIcon() { return nullptr; }
  ImmediateTexture* GetAchievementIcon() { return nullptr; }

 private:
  std::unique_ptr<ImmediateTexture> font_texture_;

  // If there's an active pointer, the ImGui mouse is controlled by this touch.
  // If it's TouchEvent::kPointerIDNone, the ImGui mouse is controlled by the
  // mouse.
  uint32_t touch_pointer_id_ = TouchEvent::kPointerIDNone;
  // Whether after the next frame (since the mouse up event needs to be handled
  // with the correct mouse position still), the ImGui mouse position should be
  // reset (for instance, after releasing a touch), so it's not hovering over
  // anything.
  bool reset_mouse_position_after_next_frame_ = false;

  double frame_time_tick_frequency_;
  uint64_t last_frame_time_ticks_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_IMGUI_DRAWER_H_
