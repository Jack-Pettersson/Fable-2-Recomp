// fable_2 - Keyboard -> guest gamepad input bridge
//
// ReXGlue's guest input path is:
//   game (PPC) -> __imp__XamInputGetState -> rex::input::InputSystem::GetState
//   -> each InputDriver::GetDeviceState -> MergeInto (buttons OR, triggers max)
//
// The SDK ships an SDL driver (real gamepads) plus a NOP stand-in. This file
// adds one more driver: a *synthetic* "keyboard gamepad". Synthetic devices
// are routed to guest user 0 by the default assignment and are OR-merged with
// the physical pad, so holding a mapped key injects the corresponding guest
// button on top of whatever the real pad reports (see state_merge.h /
// device_assignment.h). The physical pad keeps working; the keyboard just
// adds buttons.
//
// The keyboard mapping is data-driven via the `keyboard_gamepad_map` cvar
// (defined in main.cpp): "Key:Button,Key:Button,..." where Key is a host key
// name understood by rex::ui::ParseVirtualKey ("E", "Space", "LeftShift", ...)
// or a mouse-button name ("LMB"/"RMB"/"MMB", plus "XMB1"/"XMB2" for the side
// buttons; mouse names are also accepted case-insensitively, see
// input_detail::ParseKey). Mouse-button state is polled with
// GetAsyncKeyState, the same path as keyboard keys. Button is a guest gamepad
// button name (A/B/X/Y/LB/RB/LT/RT/Up/Down/Left/Right/Start/Back/L3/R3) or an
// action name (MoveForward/MoveBack/MoveLeft/MoveRight, also spelled StickUp/
// StickDown/StickLeft/StickRight); see input_actions.h.
// The cvar's default is seeded from fable2_config.toml [input] at startup
// (src/core/fable2_config.{h,cpp} + Fable2App::OnPostInitLogging), so remapping
// can be done in the config file without a rebuild or CLI arg.
//
// The mouse is mapped to the right stick (camera look) via the `mouse_look`
// and `mouse_look_scale` cvars (defaults seeded from fable2_config.toml
// [input] at startup, same as the keyboard map above); see mouse_camera.h.
//
// The `keyboard_mouse` cvar turns keyboard and mouse support on or off. When off
// the device stays connected but reports a neutral pad and leaves the cursor
// alone. A physical controller works either way, and F5 and the debug-menu key
// are still handled.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/ui/keybinds.h>

#include "fable2_config.h"  // direct_mouse_look toggle
#include "input_actions.h"  // bindings and actions
#include "mouse_camera.h"   // mouse -> right stick
#include "mouse_look.h"     // fable2::mouselook (accumulates the mouse delta)

#ifdef _WIN32
#include <windows.h>
#endif

// Declared here, defined in main.cpp (REXCVAR_DEFINE_* must live in a .cpp).
REXCVAR_DECLARE(bool, keyboard_mouse);
REXCVAR_DECLARE(std::string, keyboard_gamepad_map);
REXCVAR_DECLARE(bool, mouse_look);
REXCVAR_DECLARE(int32_t, mouse_look_scale);
REXCVAR_DECLARE(std::string, mouse_unlock_key);

namespace fable2 {

// Bring the SDK input types into scope so the driver overrides below read
// cleanly. (X_STATUS / X_RESULT live in rex::; the rest in rex::input::.)
using rex::X_RESULT;
using rex::X_STATUS;
using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_KEYSTROKE;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_VIBRATION;
using rex::input::XINPUT_DEVTYPE_GAMEPAD;
using rex::input::X_INPUT_CAPS_FFB_SUPPORTED;

/// Synthetic device that exposes held host keys as guest gamepad buttons.
class KeyboardGamepadDriver final : public rex::input::InputDriver {
 public:
  // InputDriver's constructor is protected, so expose a public one.
  KeyboardGamepadDriver(rex::ui::Window* window, size_t window_z_order)
      : rex::input::InputDriver(window, window_z_order) {}

  ~KeyboardGamepadDriver() override { mouse_camera_.ReleaseCursor(GameWindow()); }

  X_STATUS Setup() override { return X_STATUS_SUCCESS; }

  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    RefreshBindings();
    if (bindings_.empty() && !REXCVAR_GET(mouse_look)) return;  // nothing to do
    DeviceInfo info;
    info.id = kDeviceId;
    info.name = "Keyboard/Mouse (gamepad)";
    info.guid = "fable2-keyboard-gamepad";
    info.synthetic = true;  // routed to guest user 0 by the default assignment
    out.push_back(info);
  }

  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override {
    if (id != kDeviceId) return X_ERROR_DEVICE_NOT_CONNECTED;
    RefreshBindings();

    input_detail::PadOutput pad;
    input_detail::StickXY look;
#ifdef _WIN32
    rex::ui::Window* win = GameWindow();
    HWND hwnd = win ? reinterpret_cast<HWND>(win->GetNativeWindowHandle()) : nullptr;
    // Focus: the game window is the foreground (top-level) window. Gates all
    // host input so nothing fires while alt-tabbed away.
    bool focused = hwnd != nullptr &&
                   (GetForegroundWindow() == GetAncestor(hwnd, GA_ROOT));

    // F5 (host) -> run the external Lua file (fable2_f5_lua.h). Poll per frame;
    // the actual run happens on the guest main thread via the RunScript binding
    // probe, so this just latches a flag. Runs even while the console (F4) is
    // open (F5 is a host-level developer command, not gamepad input).
    fable2::f5lua::poll_f5();

    // Debug-console toggle key (default F4). Pressing it opens/closes the menu;
    // while the menu is open we release the mouse lock (free cursor for the
    // menu) and re-engage it when the menu closes. It's excluded from the
    // gamepad map so it doesn't also act as a button (F4 was the d-pad Right).
    uint16_t unlock_vk = static_cast<uint16_t>(
        rex::ui::ParseVirtualKey(REXCVAR_GET(mouse_unlock_key)));
    bool has_unlock_key = unlock_vk != 0;
    if (focused) {
      bool unlock_down = has_unlock_key && (GetAsyncKeyState(unlock_vk) & 0x8000);
      if (unlock_down && !unlock_down_prev_) console_open_ = !console_open_;
      unlock_down_prev_ = unlock_down;
    }

    const bool enabled = REXCVAR_GET(keyboard_mouse);
    if (focused && enabled) {
      pad = input_detail::ResolveBindings(bindings_, [&](uint16_t vk) {
        if (has_unlock_key && vk == unlock_vk) return false;  // not gamepad input
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
      });
    }

    // Gated on the game window being focused (so alt-tabbing away releases the
    // lock) and on the debug menu being closed (F4) so the cursor is free while
    // the menu is open.
    const bool looking = focused && enabled && REXCVAR_GET(mouse_look) && !console_open_;
    look = mouse_camera_.Poll(win, looking, REXCVAR_GET(mouse_look_scale));
#endif

    out_state->packet_number.set(packet_number_++);
    out_state->gamepad.buttons.set(pad.buttons);
    out_state->gamepad.left_trigger = pad.left_trigger;
    out_state->gamepad.right_trigger = pad.right_trigger;
    out_state->gamepad.thumb_lx.set(static_cast<int16_t>(pad.lx));
    out_state->gamepad.thumb_ly.set(static_cast<int16_t>(pad.ly));
    out_state->gamepad.thumb_rx.set(static_cast<int16_t>(look.x));
    out_state->gamepad.thumb_ry.set(static_cast<int16_t>(look.y));
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t flags,
                                 X_INPUT_CAPABILITIES* out_caps) override {
    (void)flags;
    if (id != kDeviceId) return X_ERROR_DEVICE_NOT_CONNECTED;
    *out_caps = X_INPUT_CAPABILITIES{};
    out_caps->type = XINPUT_DEVTYPE_GAMEPAD;
    out_caps->sub_type = 0x05;  // XINPUT_SUBTYPE_GAMEPAD
    out_caps->flags.set(X_INPUT_CAPS_FFB_SUPPORTED);
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION* vibration) override {
    (void)vibration;
    return id == kDeviceId ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t flags,
                              X_INPUT_KEYSTROKE* out_keystroke) override {
    (void)flags;
    (void)out_keystroke;
    return X_ERROR_EMPTY;  // never has queued keystrokes
  }

 private:
  // Re-parse the cvar only when its text changed (cheap for a short string),
  // so the mapping is hot-reloadable from the console without a rebuild.
  void RefreshBindings() {
    std::string text = REXCVAR_GET(keyboard_gamepad_map);
    if (text == cached_text_) return;
    cached_text_ = std::move(text);
    bindings_ = input_detail::ParseMap(cached_text_);
  }

  // Device id for this synthetic pad. MUST NOT collide with the SDL driver's
  // sequential ids (they start at 1): InputSystem::DriverForDevice() resolves
  // an id by first match, so sharing id 1 with the first physical pad
  // connected silently shadows this driver -- the guest only sees the real
  // pad and this driver is never polled (dead keyboard, no mouse lock).
  static constexpr DeviceId kDeviceId{1ull << 60};
  std::vector<input_detail::Mapping> bindings_;
  std::string cached_text_;
  uint32_t packet_number_ = 0;
  MouseCamera mouse_camera_;
  bool console_open_ = false;    // debug menu open -> mouse lock released
  bool unlock_down_prev_ = false;  // prev-poll state of the unlock key
  // The game window (from the runtime's display window), used for recentering
  // and cursor visibility.
  rex::ui::Window* GameWindow() {
    auto* rt = rex::Runtime::instance();
    if (!rt) return nullptr;
    return rt->display_window();
  }
};

}  // namespace fable2
