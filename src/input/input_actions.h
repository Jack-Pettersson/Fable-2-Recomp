// fable_2 - Host key -> guest input bindings
//
// A binding targets either a raw guest pad input (button or trigger) or a named
// Action. A raw target copies the controller layout. An Action says what the
// player wants to do, and ResolveBindings decides how that reaches the guest, so
// an action can behave differently from the pad input it is built on.
//
// To add an action: add it to the enum, name it in FillGuestTarget, handle it in
// ResolveBindings and add its name to ButtonNames in the launcher's
// Fable2ConfigFile.cs.

#pragma once

#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <rex/input/input.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>

namespace fable2::input_detail {

// Full thumbstick deflection (XInput range is -32768..32767).
constexpr int32_t kStickMax = 32767;
// Per-axis deflection on a diagonal (kStickMax / sqrt(2), rounded down), so two held
// directions stay inside the circular range of a real stick.
constexpr int32_t kStickDiagonal = 23169;

inline int32_t ClampStick(int32_t v) {
  if (v > kStickMax) return kStickMax;
  if (v < -kStickMax) return -kStickMax;
  return v;
}

enum class Action : uint8_t {
  kNone,
  kMoveForward,
  kMoveBack,
  kMoveLeft,
  kMoveRight,
};

// One host-key -> guest-input binding. Exactly one of button/trigger/action is
// set.
struct Mapping {
  uint16_t vk = 0;               // host virtual-key code (0 == invalid)
  uint16_t button = 0;           // guest X_INPUT_GAMEPAD_* button mask
  char trigger = 0;              // 0 = not a trigger, 'L' = left, 'R' = right
  Action action = Action::kNone;
  bool valid() const { return button != 0 || trigger != 0 || action != Action::kNone; }
};

// Case-insensitive upper helper (shared by the name parsers below).
inline std::string ToUpper(std::string_view s) {
  std::string u;
  u.reserve(s.size());
  for (char c : s) u.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  return u;
}

// Case-insensitive guest-input name -> mapping fragment. Recognizes buttons,
// triggers and actions. The Stick* names are the original spelling of the move
// actions, kept so existing configs still load. Unknown names reset the fragment
// to invalid.
inline void FillGuestTarget(Mapping& m, std::string_view name) {
  std::string_view n = name;
  while (!n.empty() && (n.front() == ' ' || n.front() == '\t')) n.remove_prefix(1);
  while (!n.empty() && (n.back() == ' ' || n.back() == '\t')) n.remove_suffix(1);
  const std::string u = ToUpper(n);

  using B = rex::input::X_INPUT_GAMEPAD_BUTTON;
  if (u == "A") {
    m.button = B::X_INPUT_GAMEPAD_A;
  } else if (u == "B") {
    m.button = B::X_INPUT_GAMEPAD_B;
  } else if (u == "X") {
    m.button = B::X_INPUT_GAMEPAD_X;
  } else if (u == "Y") {
    m.button = B::X_INPUT_GAMEPAD_Y;
  } else if (u == "LB" || u == "LSHOULDER") {
    m.button = B::X_INPUT_GAMEPAD_LEFT_SHOULDER;
  } else if (u == "RB" || u == "RSHOULDER") {
    m.button = B::X_INPUT_GAMEPAD_RIGHT_SHOULDER;
  } else if (u == "UP" || u == "DUP") {
    m.button = B::X_INPUT_GAMEPAD_DPAD_UP;
  } else if (u == "DOWN" || u == "DDOWN") {
    m.button = B::X_INPUT_GAMEPAD_DPAD_DOWN;
  } else if (u == "LEFT" || u == "DLEFT") {
    m.button = B::X_INPUT_GAMEPAD_DPAD_LEFT;
  } else if (u == "RIGHT" || u == "DRIGHT") {
    m.button = B::X_INPUT_GAMEPAD_DPAD_RIGHT;
  } else if (u == "START" || u == "PAUSE") {
    m.button = B::X_INPUT_GAMEPAD_START;
  } else if (u == "BACK" || u == "VIEW" || u == "SELECT") {
    m.button = B::X_INPUT_GAMEPAD_BACK;
  } else if (u == "L3" || u == "LTHUMB") {
    m.button = B::X_INPUT_GAMEPAD_LEFT_THUMB;
  } else if (u == "R3" || u == "RTHUMB") {
    m.button = B::X_INPUT_GAMEPAD_RIGHT_THUMB;
  } else if (u == "LT" || u == "LTRIGGER") {
    m.trigger = 'L';
  } else if (u == "RT" || u == "RTRIGGER") {
    m.trigger = 'R';
  } else if (u == "MOVEFORWARD" || u == "STICKUP" || u == "LU") {
    m.action = Action::kMoveForward;
  } else if (u == "MOVEBACK" || u == "STICKDOWN" || u == "LD") {
    m.action = Action::kMoveBack;
  } else if (u == "MOVELEFT" || u == "STICKLEFT" || u == "LL") {
    m.action = Action::kMoveLeft;
  } else if (u == "MOVERIGHT" || u == "STICKRIGHT" || u == "LR") {
    m.action = Action::kMoveRight;
  } else {
    m = Mapping{};  // unknown -> invalid
  }
}

// Host key-name -> VirtualKey. The SDK table (rex::ui::ParseVirtualKey) is
// authoritative and already recognizes LMB/RMB/MMB; this fallback additionally
// accepts mouse-button names case-insensitively ("lmb") and covers the side
// buttons the SDK table does not spell out (VK_XBUTTON1/2). Returns kNone when
// the name is not a known key.
inline rex::ui::VirtualKey ParseKey(std::string_view name) {
  // Tolerate stray spaces/tabs around the name ("LMB", " LMB ").
  while (!name.empty() && (name.front() == ' ' || name.front() == '\t')) name.remove_prefix(1);
  while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.remove_suffix(1);
  auto vk = rex::ui::ParseVirtualKey(name);
  if (vk != rex::ui::VirtualKey::kNone) return vk;
  using V = rex::ui::VirtualKey;
  std::string u = ToUpper(name);
  if (u == "LMB" || u == "LEFTMOUSE") return V::kLButton;   // VK_LBUTTON
  if (u == "RMB" || u == "RIGHTMOUSE") return V::kRButton;  // VK_RBUTTON
  if (u == "MMB" || u == "MIDDLEMOUSE") return V::kMButton;  // VK_MBUTTON
  if (u == "XMB1" || u == "XBUTTON1" || u == "SIDEBUTTON1")
    return V::kXButton1;  // mouse side button 1
  if (u == "XMB2" || u == "XBUTTON2" || u == "SIDEBUTTON2")
    return V::kXButton2;  // mouse side button 2
  return V::kNone;
}

// Parse "Key:Button,Key:Button,...". Entries whose key or target name does not
// parse are skipped. Returns an empty vector on empty input.
inline std::vector<Mapping> ParseMap(std::string_view text) {
  std::vector<Mapping> out;
  size_t start = 0;
  while (start <= text.size()) {
    size_t comma = text.find(',', start);
    std::string_view entry =
        text.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
    if (!entry.empty()) {
      size_t colon = entry.find(':');
      if (colon != std::string_view::npos) {
        auto vk = ParseKey(entry.substr(0, colon));
        if (vk != rex::ui::VirtualKey::kNone) {
          Mapping m;
          m.vk = static_cast<uint16_t>(vk);
          FillGuestTarget(m, entry.substr(colon + 1));
          if (m.valid()) out.push_back(m);
        }
      }
    }
    if (comma == std::string_view::npos) break;
    start = comma + 1;
  }
  return out;
}

// Guest pad input produced by the bindings for one poll.
struct PadOutput {
  uint16_t buttons = 0;
  uint8_t left_trigger = 0;
  uint8_t right_trigger = 0;
  int32_t lx = 0;
  int32_t ly = 0;
};

// Evaluate the bindings against the held keys. is_held(vk) -> bool.
template <typename IsHeld>
PadOutput ResolveBindings(const std::vector<Mapping>& bindings, IsHeld&& is_held) {
  PadOutput out;
  // Opposing directions cancel; two keys for one direction count once.
  int32_t move_x = 0;
  int32_t move_y = 0;
  for (const auto& m : bindings) {
    if (!is_held(m.vk)) continue;
    out.buttons |= m.button;
    if (m.trigger == 'L') out.left_trigger = 0xFF;
    if (m.trigger == 'R') out.right_trigger = 0xFF;
    switch (m.action) {
      // Fable 2 reads ly positive as forward.
      case Action::kMoveForward: ++move_y; break;
      case Action::kMoveBack: --move_y; break;
      case Action::kMoveLeft: --move_x; break;
      case Action::kMoveRight: ++move_x; break;
      case Action::kNone: break;
    }
  }
  const int32_t sign_x = (move_x > 0) - (move_x < 0);
  const int32_t sign_y = (move_y > 0) - (move_y < 0);
  const int32_t magnitude = (sign_x != 0 && sign_y != 0) ? kStickDiagonal : kStickMax;
  out.lx = sign_x * magnitude;
  out.ly = sign_y * magnitude;
  return out;
}

struct StickXY {
  int32_t x = 0;
  int32_t y = 0;
};

// Mouse displacement (screen pixels, dx>0 = right, dy>0 = down) -> right stick.
// Fable 2 inverts Y, so mouse-up (dy<0) maps to +y (look up).
inline StickXY MouseDeltaToStick(int32_t dx, int32_t dy, int32_t scale) {
  return {ClampStick(dx * scale), ClampStick(-dy * scale)};
}

}  // namespace fable2::input_detail
