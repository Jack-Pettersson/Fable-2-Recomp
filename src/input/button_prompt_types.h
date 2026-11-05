// fable_2 - What the keyboard button prompts show, shared by the labels and the art

#pragma once

#include <atomic>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace fable2::prompts {

inline constexpr std::string_view kTexturesBank = "gui_textures.bnk";
inline constexpr std::string_view kHeadersBank = "gui_texture_headers.bnk";

// Set by Prepare() (button_prompt_files.h) when the patched banks are in place.
inline std::atomic<bool>& Active() {
  static std::atomic<bool> active{false};
  return active;
}

inline constexpr std::string_view kMouseLabel = "Mouse";

// One key, two side by side, or four in the shape of WASD (up, left, down, right).
struct IconLabel {
  std::vector<std::string> keys;
  int size;
};

// What the prompts show (see button_prompt_labels.h).
struct PromptLabels {
  std::vector<std::string> sheet;             // abyx columns: A, B, Y, X
  std::map<std::string, IconLabel> icons;     // by bank entry name
  std::map<std::string, std::string> buttons; // bound buttons on kSheetButtonCells, by button
};

// Buttons drawn on sheets of other art: the cell is centred on the button and
// big enough for the key to cover it.
struct SheetButton {
  const char* sheet;
  const char* button;
  double x, y, width, height;
};

inline constexpr SheetButton kSheetButtonCells[] = {
    {"Art\\GUI\\Frames\\frames_elements.tex", "A", 35, 394.5, 55, 55},
    {"Art\\GUI\\Frames\\frames_elements.tex", "B", 68, 219, 58, 58},
    {"Art\\GUI\\Persistant\\hud.tex", "Back", 416, 180.5, 55, 42},
    {"Art\\GUI\\Persistant\\hud.tex", "Start", 416, 220, 55, 42},
};

}  // namespace fable2::prompts
