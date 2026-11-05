// Synthetic standalone test for what the keyboard button prompts show
// (src/input/button_prompt_labels.h). Links the SDK's real keybinds.cpp, like
// test_keyboard_gamepad_map.cpp.

#include <rex/cvar.h>

namespace rex::cvar {
std::optional<size_t> RegisterFlag(FlagEntry) { return {}; }
}

namespace fable2::f5lua {
void poll_f5();
}

#include "src/input/button_prompt_labels.h"
#include "src/core/fable2_config.h"

#include <cstdio>
#include <string>

namespace {

int failures = 0;

void Expect(bool ok, const char* what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

}  // namespace

int main() {
  using namespace fable2::prompts;
  using fable2::input_detail::ParseMap;

  auto defaults = ParseMap(fable2::config::kDefaultKeyboardGamepadMap);
  auto sheet = SheetLabels(defaults);
  Expect(sheet.size() == 4, "the sheet has four columns");
  Expect(sheet[0] == "E", "A prefers the plain key E over Shift");
  Expect(sheet[1] == "B", "B prefers the plain key B over MMB");
  Expect(sheet[2] == "RMB" && sheet[3] == "LMB", "Y and X show the mouse buttons");

  auto sparse = SheetLabels(ParseMap("Space:a,Control:X"));
  Expect(sparse[0] == "Space", "targets match any case");
  Expect(sparse[3] == "Ctrl", "long key names are shortened");
  Expect(sparse[2] == "Y", "an unbound button keeps its letter");

  auto labels = LabelsFor(defaults, true);
  Expect(labels.buttons["A"] == "E" && labels.buttons["B"] == "B", "sheet cells use the same keys");
  Expect(labels.buttons["Back"] == "M", "Back matches the map's Select");
  Expect(labels.buttons["Start"] == "Esc", "Start matches the map's Pause");

  using Keys = std::vector<std::string>;
  auto icons = labels.icons;
  Expect(icons["Art\\GUI\\Controller\\LeftTrigger.tex"].keys == Keys{"Q"}, "LT shows its key");
  Expect(icons["Art\\GUI\\Controller\\LeftTrigger.tex"].size == 64, "large icons are 64 pixels");
  Expect(icons["Art\\GUI\\Controller\\24x24\\LeftTrigger.tex"].size == 32, "small icons are 32 pixels");
  Expect(icons["Art\\GUI\\Controller\\Left.tex"].keys == Keys{"W", "A", "S", "D"},
         "the left stick shows all four keys");
  Expect(icons["Art\\GUI\\Controller\\DPad.tex"].keys == Keys{"↑", "←", "↓", "→"}, "the D-pad shows arrows");
  Expect(icons["Art\\GUI\\Controller\\24x24\\AnimLeft.tex"].keys == Keys{"A", "D"},
         "the animated stick shows the keys for left and right");
  Expect(icons["Art\\GUI\\Controller\\RightUp.tex"].keys == Keys{"Mouse↑"}, "the right stick shows the mouse");
  Expect(icons["Art\\GUI\\Controller\\RightDown.tex"].size == 32, "the large RightDown keeps its stored size");

  auto no_mouse = IconLabels(defaults, false);
  Expect(!no_mouse.contains("Art\\GUI\\Controller\\Right.tex"), "without mouse look the right stick keeps its art");
  Expect(IconLabels(ParseMap("E:A"), true).count("Art\\GUI\\Controller\\Left.tex") == 0,
         "a partly bound stick keeps its art");

  if (failures) return 1;
  std::puts("button prompt labels: all checks passed");
  return 0;
}
