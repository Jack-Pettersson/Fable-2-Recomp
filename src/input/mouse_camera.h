// fable_2 - Mouse -> right stick camera look
//
// Owns the cursor while looking: it is hidden and pinned to the window center,
// and each poll's displacement becomes that poll's right-stick deflection, so the
// camera rotates continuously and the cursor never hits the screen edge.

#pragma once

#include <cstdint>

#include <rex/ui/window.h>

#include "input_actions.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace fable2 {

class MouseCamera {
 public:
  // Call once per poll. With looking false the cursor is released and the stick
  // stays neutral.
  input_detail::StickXY Poll(rex::ui::Window* win, bool looking, int32_t scale) {
    input_detail::StickXY stick;
#ifdef _WIN32
    // Hide/restore the cursor via the SDK Window. This is a thread-safe
    // "desired state" that the UI thread applies, so it actually takes effect
    // (raw ShowCursor from this guest thread was overwritten by the SDK's own
    // cursor management).
    if (looking && !cursor_hidden_) {
      win->SetCursorVisibility(rex::ui::Window::CursorVisibility::kHidden);
      cursor_hidden_ = true;
    } else if (!looking) {
      ReleaseCursor(win);
    }

    if (!looking) {
      mouse_centered_ = false;  // re-prime on (re)focus to avoid a jump
      return stick;
    }

    HWND hwnd = reinterpret_cast<HWND>(win->GetNativeWindowHandle());
    RECT rc;
    POINT p;
    POINT c;
    if (GetClientRect(hwnd, &rc) && GetCursorPos(&p)) {
      c.x = rc.left + rc.right / 2;
      c.y = rc.top + rc.bottom / 2;
      if (ClientToScreen(hwnd, &c)) {
        if (mouse_centered_) {
          // Direct-mouse-look separation DISABLED: the mouse always drives
          // the synthetic pad's right stick (the native path) so the game's
          // own stick pipeline produces the look 1:1 with a real right stick.
          // The raw-displacement path that fed fable2_hook_direct_mouse_look
          // is commented out below (preserved for reference).
          // if (fable2::config::Get().direct_mouse_look) {
          //   // Direct mouse look: hand the raw displacement to the camera
          //   // hook (fable2_hook_direct_mouse_look) and leave the pad's right
          //   // stick neutral, so the mouse no longer drives the joystick.
          //   fable2::mouselook::add_delta(dx, dy);
          // }
          stick = input_detail::MouseDeltaToStick(p.x - c.x, p.y - c.y, scale);
        }
        SetCursorPos(c.x, c.y);  // recenter: consume this frame's movement
        mouse_centered_ = true;
      }
    }
#endif
    return stick;
  }

  void ReleaseCursor(rex::ui::Window* win) {
    if (cursor_hidden_ && win) {
      win->SetCursorVisibility(rex::ui::Window::CursorVisibility::kVisible);
      cursor_hidden_ = false;
    }
  }

 private:
  bool mouse_centered_ = false;  // set once the cursor is first pinned
  bool cursor_hidden_ = false;   // set while the SDK window cursor is hidden
};

}  // namespace fable2
