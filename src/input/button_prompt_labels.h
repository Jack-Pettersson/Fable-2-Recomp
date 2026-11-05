// fable_2 - What each controller prompt shows for the keyboard map
//
// Labels come from the player's own keyboard layout where Windows can tell
// (so the key the map calls Semicolon reads Ö on a German keyboard), otherwise
// from the key's name, shortened to fit on a key.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <rex/ui/virtual_key.h>

#include "button_prompt_types.h"
#include "keyboard_gamepad.h"

namespace fable2::prompts {

inline std::string Utf8(char32_t c) {
  std::string out;
  if (c < 0x80) {
    out += char(c);
  } else if (c < 0x800) {
    out += char(0xC0 | (c >> 6));
    out += char(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    out += char(0xE0 | (c >> 12));
    out += char(0x80 | ((c >> 6) & 0x3F));
    out += char(0x80 | (c & 0x3F));
  } else {
    out += char(0xF0 | (c >> 18));
    out += char(0x80 | ((c >> 12) & 0x3F));
    out += char(0x80 | ((c >> 6) & 0x3F));
    out += char(0x80 | (c & 0x3F));
  }
  return out;
}

inline size_t CodepointCount(std::string_view s) {
  size_t count = 0;
  for (char c : s) count += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
  return count;
}

inline std::string KeyLabel(rex::ui::VirtualKey vk) {
  using V = rex::ui::VirtualKey;
  switch (vk) {
    case V::kLButton: return "LMB";
    case V::kRButton: return "RMB";
    case V::kMButton: return "MMB";
    case V::kXButton1: return "M4";
    case V::kXButton2: return "M5";
    default: break;
  }
#ifdef _WIN32
  // The unshifted character this key types on the current layout. The top bit marks a dead key.
  UINT ch = ::MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_CHAR) & 0xFFFF;
  if (ch > 32 && ch != 127) return Utf8(char32_t(::towupper(wchar_t(ch))));
#endif
  const uint16_t code = uint16_t(vk);
  if (code >= uint16_t(V::kF1) && code <= uint16_t(V::kF24)) return "F" + std::to_string(code - uint16_t(V::kF1) + 1);
  if (code >= uint16_t(V::kNumpad0) && code <= uint16_t(V::kNumpad0) + 9)
    return "Num" + std::to_string(code - uint16_t(V::kNumpad0));
  if ((code >= '0' && code <= '9') || (code >= 'A' && code <= 'Z')) return std::string(1, char(code));
  switch (vk) {
    case V::kBack: return "Bksp";
    case V::kTab: return "Tab";
    case V::kReturn: return "Enter";
    case V::kShift: return "Shift";
    case V::kLShift: return "LShift";
    case V::kRShift: return "RShift";
    case V::kControl: return "Ctrl";
    case V::kLControl: return "LCtrl";
    case V::kRControl: return "RCtrl";
    case V::kMenu: return "Alt";
    case V::kLMenu: return "LAlt";
    case V::kRMenu: return "RAlt";
    case V::kPause: return "Pause";
    case V::kCapital: return "Caps";
    case V::kEscape: return "Esc";
    case V::kSpace: return "Space";
    case V::kPrior: return "PgUp";
    case V::kNext: return "PgDn";
    case V::kEnd: return "End";
    case V::kHome: return "Home";
    case V::kLeft: return "←";
    case V::kUp: return "↑";
    case V::kRight: return "→";
    case V::kDown: return "↓";
    case V::kSnapshot: return "PrtSc";
    case V::kInsert: return "Ins";
    case V::kDelete: return "Del";
    case V::kMultiply: return "Num*";
    case V::kAdd: return "Num+";
    case V::kSubtract: return "Num-";
    case V::kDecimal: return "Num.";
    case V::kDivide: return "Num/";
    case V::kNumLock: return "Num";
    case V::kScroll: return "Scrl";
    case V::kOem1: return ";";
    case V::kOemPlus: return "+";
    case V::kOemComma: return ",";
    case V::kOemMinus: return "-";
    case V::kOemPeriod: return ".";
    case V::kOem2: return "/";
    case V::kOem3: return "`";
    case V::kOem4: return "[";
    case V::kOem5: return "\\";
    case V::kOem6: return "]";
    case V::kOem7: return "'";
    default: return "?";
  }
}

// The key for the first of these targets the map binds. A target with several
// keys shows the plainest one (E rather than Shift).
inline std::optional<std::string> KeyFor(const std::vector<input_detail::Mapping>& binds,
                                         std::initializer_list<std::string_view> targets) {
  std::vector<std::string> labels;
  for (const auto& bind : binds) {
    for (std::string_view target : targets) {
      input_detail::Mapping wanted;
      input_detail::FillGuestTarget(wanted, target);
      if (bind.button == wanted.button && bind.trigger == wanted.trigger && bind.axis == wanted.axis &&
          bind.axis_sign == wanted.axis_sign) {
        labels.push_back(KeyLabel(rex::ui::VirtualKey(bind.vk)));
        break;
      }
    }
  }
  if (labels.empty()) return std::nullopt;
  for (const std::string& label : labels)
    if (CodepointCount(label) == 1) return label;
  return labels.front();
}

// The abyx sheet's columns. An unbound button keeps its own letter.
inline std::vector<std::string> SheetLabels(const std::vector<input_detail::Mapping>& binds) {
  std::vector<std::string> labels;
  for (const char* button : {"A", "B", "Y", "X"}) labels.push_back(KeyFor(binds, {button}).value_or(button));
  return labels;
}

// The label for each single-button icon, by bank entry name. Unbound buttons are
// left out and keep their art.
inline std::map<std::string, IconLabel> IconLabels(const std::vector<input_detail::Mapping>& binds,
                                                   bool mouse_look) {
  constexpr std::string_view kFolder = "Art\\GUI\\Controller\\";
  constexpr std::string_view kSmall = "Art\\GUI\\Controller\\24x24\\";
  using Keys = std::optional<std::vector<std::string>>;
  // Every key, or nothing if one of them is unbound.
  auto keys = [&](std::initializer_list<std::string_view> targets) -> Keys {
    std::vector<std::string> out;
    for (std::string_view target : targets) {
      auto key = KeyFor(binds, {target});
      if (!key) return std::nullopt;
      out.push_back(*key);
    }
    return out;
  };
  auto mouse = [&](std::string_view direction) -> Keys {
    if (!mouse_look) return std::nullopt;
    return std::vector<std::string>{std::string(kMouseLabel) + std::string(direction)};
  };

  // Several keys are drawn as small keys in the shape of WASD or the arrow keys.
  Keys move = keys({"StickUp", "StickLeft", "StickDown", "StickRight"});
  Keys sideways = keys({"StickLeft", "StickRight"});
  Keys dpad = keys({"Up", "Left", "Down", "Right"});

  std::map<std::string, IconLabel> icons;
  auto both = [&](std::string_view big, std::string_view small_name, const Keys& label) {
    if (!label) return;
    icons[std::string(kFolder) + std::string(big) + ".tex"] = {*label, 64};
    icons[std::string(kSmall) + std::string(small_name) + ".tex"] = {*label, 32};
  };
  auto small_only = [&](std::string_view name, const Keys& label) {
    if (label) icons[std::string(kSmall) + std::string(name) + ".tex"] = {*label, 32};
  };

  both("LeftTrigger", "LeftTrigger", keys({"LT"}));
  both("RightTrigger", "RightTrigger", keys({"RT"}));
  both("LeftButton", "LeftButton", keys({"LB"}));
  both("RightButton", "RightButton", keys({"RB"}));
  both("back", "Back", keys({"Back"}));
  both("start", "Start", keys({"Start"}));
  both("DPad", "DPad", dpad);
  both("Left", "Left", move);
  both("LeftUp", "LeftUp", keys({"StickUp"}));
  both("LeftDown", "LeftDown", keys({"StickDown"}));
  both("LeftLeft", "LeftLeft", keys({"StickLeft"}));
  both("LeftRight", "LeftRight", keys({"StickRight"}));
  both("Right", "Right", mouse(""));
  both("RightUp", "RightUp", mouse("\u2191"));
  both("RightLeft", "RightLeft", mouse("\u2190"));
  both("RightRight", "RightRight", mouse("\u2192"));
  small_only("RightDown", mouse("\u2193"));
  // The animated stick is for moving left and right, as on the skill pages.
  small_only("AnimLeft", sideways);
  small_only("AnimLeftLeft", keys({"StickLeft"}));
  small_only("AnimLeftRight", keys({"StickRight"}));
  small_only("AnimRight", mouse(""));
  small_only("AnimRightUp", mouse("\u2191"));
  small_only("AnimRightDown", mouse("\u2193"));
  if (Keys lt = keys({"LT"})) icons[std::string(kFolder) + "icon_button_lt.tex"] = {*lt, 64};
  // The large RightDown is stored at half the size of its neighbours.
  if (Keys mouse_down = mouse("\u2193")) icons[std::string(kFolder) + "RightDown.tex"] = {*mouse_down, 32};
  return icons;
}

// Everything the art needs to know about the map.
inline PromptLabels LabelsFor(const std::vector<input_detail::Mapping>& binds, bool mouse_look) {
  PromptLabels labels{SheetLabels(binds), IconLabels(binds, mouse_look), {}};
  for (const SheetButton& cell : kSheetButtonCells)
    if (auto key = KeyFor(binds, {cell.button})) labels.buttons[cell.button] = *key;
  return labels;
}

}  // namespace fable2::prompts
