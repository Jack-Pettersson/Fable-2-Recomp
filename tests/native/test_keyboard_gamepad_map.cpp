// Synthetic standalone test for the keyboard/mouse -> guest gamepad map
// parsing (src/input/input_actions.h). No game, GPU, SDL or audio device
// needed: it links the SDK's real keybinds.cpp (rex::ui::ParseVirtualKey)
// plus a stub for the one SDK cvar symbol that keybinds.cpp references.
//
// Covers:
//   - the requested mouse-button config ("LMB:X,RMB:Y,MMB:B,Shift:A,...")
//     parses fully and every entry lands on the expected VirtualKey +
//     guest button/trigger/stick target;
//   - mouse-button key names: LMB/RMB/MMB via the SDK table, case-insensitive
//     spellings, and XMB1/XMB2 side buttons (VK_XBUTTON1/2);
//   - malformed / unknown entries are skipped without killing the rest;
//   - guest-target aliases (LSHOULDER, View, ltrigger, ...) still resolve;
//   - held keys resolve to pad output, and mouse movement to the right stick.

#include <rex/cvar.h>  // FlagEntry (needed for the stub below)

// The single SDK symbol the real keybinds.cpp references. The test never
// registers a cvar, so an empty stub is enough to satisfy the linker.
namespace rex::cvar {
std::optional<size_t> RegisterFlag(FlagEntry) { return {}; }
}

#include "src/input/input_actions.h"
// The built-in default map (src/core/fable2_config.h) must parse fully too.
#include "src/core/fable2_config.h"

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

using B = rex::input::X_INPUT_GAMEPAD_BUTTON;
using M = fable2::input_detail::Mapping;
using Action = fable2::input_detail::Action;
using fable2::input_detail::kStickDiagonal;
using fable2::input_detail::kStickMax;
using fable2::input_detail::MouseDeltaToStick;
using fable2::input_detail::ResolveBindings;
using fable2::input_detail::FillGuestTarget;
using fable2::input_detail::ParseKey;
using fable2::input_detail::ParseMap;

static int failures = 0;
#define CHECK(cond)                                                          \
  do {                                                                       \
    if (!(cond)) {                                                           \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
      ++failures;                                                            \
    }                                                                        \
  } while (0)

// Find a binding by VirtualKey code (returns nullopt when absent).
static std::optional<M> Find(const std::vector<M>& map, uint16_t vk) {
  for (const auto& m : map) {
    if (m.vk == vk) return m;
  }
  return std::nullopt;
}

static void CheckBinding(const std::vector<M>& map, uint16_t vk, uint16_t button,
                         char trigger = 0, Action action = Action::kNone) {
  auto m = Find(map, vk);
  CHECK(m.has_value());
  if (!m) return;
  CHECK(m->button == button);
  CHECK(m->trigger == trigger);
  CHECK(m->action == action);
}

int main() {
  using V = rex::ui::VirtualKey;

  // 1) The requested mouse-button config, verbatim, must parse fully:
  //    LMB:X,RMB:Y,MMB:B,Shift:A,Space:A,E:A,Q:LT,Tab:RB,R:RT,Z:LB,B:B,
  //    W:StickUp,S:StickDown,A:StickLeft,D:StickRight,Escape:Pause,M:Select,
  //    Up:Up,Down:Down,Left:Right? no - Left:Left,Right:Right,F1:Up,F2:Down,
  //    F3:Left
  const std::string kRequested =
      "LMB:X,RMB:Y,MMB:B,Shift:A,Space:A,E:A,Q:LT,Tab:RB,R:RT,Z:LB,B:B,"
      "W:StickUp,S:StickDown,A:StickLeft,D:StickRight,Escape:Pause,M:Select,"
      "Up:Up,Down:Down,Left:Left,Right:Right,F1:Up,F2:Down,F3:Left";
  auto map = ParseMap(kRequested);
  CHECK(map.size() == 24);  // every entry valid (duplicate targets are fine)

  // Mouse buttons -> guest face buttons.
  CheckBinding(map, static_cast<uint16_t>(V::kLButton), B::X_INPUT_GAMEPAD_X);
  CheckBinding(map, static_cast<uint16_t>(V::kRButton), B::X_INPUT_GAMEPAD_Y);
  CheckBinding(map, static_cast<uint16_t>(V::kMButton), B::X_INPUT_GAMEPAD_B);
  // Keyboard keys.
  CheckBinding(map, static_cast<uint16_t>(V::kShift), B::X_INPUT_GAMEPAD_A);
  CheckBinding(map, static_cast<uint16_t>(V::kSpace), B::X_INPUT_GAMEPAD_A);
  CheckBinding(map, static_cast<uint16_t>(V::kE), B::X_INPUT_GAMEPAD_A);
  CheckBinding(map, static_cast<uint16_t>(V::kQ), 0, 'L');
  CheckBinding(map, static_cast<uint16_t>(V::kTab), B::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
  CheckBinding(map, static_cast<uint16_t>(V::kR), 0, 'R');
  CheckBinding(map, static_cast<uint16_t>(V::kZ), B::X_INPUT_GAMEPAD_LEFT_SHOULDER);
  CheckBinding(map, static_cast<uint16_t>(V::kB), B::X_INPUT_GAMEPAD_B);
  // Movement actions.
  CheckBinding(map, static_cast<uint16_t>(V::kW), 0, 0, Action::kMoveForward);
  CheckBinding(map, static_cast<uint16_t>(V::kS), 0, 0, Action::kMoveBack);
  CheckBinding(map, static_cast<uint16_t>(V::kA), 0, 0, Action::kMoveLeft);
  CheckBinding(map, static_cast<uint16_t>(V::kD), 0, 0, Action::kMoveRight);
  // Dpad / menu / back.
  CheckBinding(map, static_cast<uint16_t>(V::kEscape), B::X_INPUT_GAMEPAD_START);
  CheckBinding(map, static_cast<uint16_t>(V::kM), B::X_INPUT_GAMEPAD_BACK);
  CheckBinding(map, static_cast<uint16_t>(V::kUp), B::X_INPUT_GAMEPAD_DPAD_UP);
  CheckBinding(map, static_cast<uint16_t>(V::kDown), B::X_INPUT_GAMEPAD_DPAD_DOWN);
  CheckBinding(map, static_cast<uint16_t>(V::kLeft), B::X_INPUT_GAMEPAD_DPAD_LEFT);
  CheckBinding(map, static_cast<uint16_t>(V::kRight), B::X_INPUT_GAMEPAD_DPAD_RIGHT);
  CheckBinding(map, static_cast<uint16_t>(V::kF1), B::X_INPUT_GAMEPAD_DPAD_UP);
  CheckBinding(map, static_cast<uint16_t>(V::kF2), B::X_INPUT_GAMEPAD_DPAD_DOWN);
  CheckBinding(map, static_cast<uint16_t>(V::kF3), B::X_INPUT_GAMEPAD_DPAD_LEFT);

  // 2) Mouse-button key names: SDK table (case-sensitive) and the local
  //    case-insensitive / side-button fallback.
  CHECK(ParseKey("LMB") == V::kLButton);
  CHECK(ParseKey("RMB") == V::kRButton);
  CHECK(ParseKey("MMB") == V::kMButton);
  CHECK(ParseKey("lmb") == V::kLButton);
  CHECK(ParseKey("rmb") == V::kRButton);
  CHECK(ParseKey("mmb") == V::kMButton);
  CHECK(ParseKey(" XMB1 ") == V::kXButton1);  // side button 1, stray spaces ok
  CHECK(ParseKey("xmb2") == V::kXButton2);    // side button 2
  CHECK(ParseKey("XButton1") == V::kXButton1);
  CHECK(ParseKey("SideButton2") == V::kXButton2);
  auto side = ParseMap("XMB1:A,XMB2:LT");
  CHECK(side.size() == 2);
  CheckBinding(side, static_cast<uint16_t>(V::kXButton1), B::X_INPUT_GAMEPAD_A);
  CheckBinding(side, static_cast<uint16_t>(V::kXButton2), 0, 'L');

  // 3) Malformed / unknown entries are skipped, the rest still parse.
  auto bad = ParseMap("NotAKey:A,LMB:NotAButton,,E:A,Space:");
  CHECK(bad.size() == 1);
  if (bad.size() == 1) {
    CheckBinding(bad, static_cast<uint16_t>(V::kE), B::X_INPUT_GAMEPAD_A);
  }
  CHECK(ParseMap("").empty());
  CHECK(ParseMap(",,,").empty());

  // 3b) The built-in default map (kDefaultKeyboardGamepadMap) parses fully
  //     and lands every entry on the intended target.
  auto def = ParseMap(fable2::config::kDefaultKeyboardGamepadMap);
  CHECK(def.size() == 20);
  CheckBinding(def, static_cast<uint16_t>(V::kLButton), B::X_INPUT_GAMEPAD_X);
  CheckBinding(def, static_cast<uint16_t>(V::kRButton), B::X_INPUT_GAMEPAD_Y);
  CheckBinding(def, static_cast<uint16_t>(V::kMButton), B::X_INPUT_GAMEPAD_B);
  CheckBinding(def, static_cast<uint16_t>(V::kShift), B::X_INPUT_GAMEPAD_A);
  CheckBinding(def, static_cast<uint16_t>(V::kE), B::X_INPUT_GAMEPAD_A);
  CheckBinding(def, static_cast<uint16_t>(V::kB), B::X_INPUT_GAMEPAD_B);
  CheckBinding(def, static_cast<uint16_t>(V::kQ), 0, 'L');
  CheckBinding(def, static_cast<uint16_t>(V::kTab), B::X_INPUT_GAMEPAD_RIGHT_SHOULDER);
  CheckBinding(def, static_cast<uint16_t>(V::kR), 0, 'R');
  CheckBinding(def, static_cast<uint16_t>(V::kZ), B::X_INPUT_GAMEPAD_LEFT_SHOULDER);
  CheckBinding(def, static_cast<uint16_t>(V::kW), 0, 0, Action::kMoveForward);
  CheckBinding(def, static_cast<uint16_t>(V::kS), 0, 0, Action::kMoveBack);
  CheckBinding(def, static_cast<uint16_t>(V::kA), 0, 0, Action::kMoveLeft);
  CheckBinding(def, static_cast<uint16_t>(V::kD), 0, 0, Action::kMoveRight);
  CheckBinding(def, static_cast<uint16_t>(V::kEscape), B::X_INPUT_GAMEPAD_START);
  CheckBinding(def, static_cast<uint16_t>(V::kM), B::X_INPUT_GAMEPAD_BACK);
  CheckBinding(def, static_cast<uint16_t>(V::kUp), B::X_INPUT_GAMEPAD_DPAD_UP);
  CheckBinding(def, static_cast<uint16_t>(V::kDown), B::X_INPUT_GAMEPAD_DPAD_DOWN);
  CheckBinding(def, static_cast<uint16_t>(V::kLeft), B::X_INPUT_GAMEPAD_DPAD_LEFT);
  CheckBinding(def, static_cast<uint16_t>(V::kRight), B::X_INPUT_GAMEPAD_DPAD_RIGHT);

  // 4) Guest-target aliases still resolve.
  M m;
  FillGuestTarget(m, "lShoulder");
  CHECK(m.button == B::X_INPUT_GAMEPAD_LEFT_SHOULDER);
  FillGuestTarget(m, "VIEW");
  CHECK(m.button == B::X_INPUT_GAMEPAD_BACK);
  FillGuestTarget(m, "ltrigger");
  CHECK(m.trigger == 'L');
  FillGuestTarget(m, "StIckRighT");
  CHECK(m.action == Action::kMoveRight);
  FillGuestTarget(m, "moveforward");
  CHECK(m.action == Action::kMoveForward);
  FillGuestTarget(m, "Bogus");
  CHECK(!m.valid());

  // 5) Held keys -> pad output.
  const auto wasd = ParseMap("W:MoveForward,S:MoveBack,A:MoveLeft,D:MoveRight,Up:StickUp,E:A,Q:LT,R:RT");
  auto press = [&](std::initializer_list<V> keys) {
    return ResolveBindings(wasd, [&](uint16_t vk) {
      for (V k : keys) {
        if (static_cast<uint16_t>(k) == vk) return true;
      }
      return false;
    });
  };
  auto idle = press({});
  CHECK(idle.buttons == 0 && idle.left_trigger == 0 && idle.right_trigger == 0);
  CHECK(idle.lx == 0 && idle.ly == 0);
  auto forward = press({V::kW});
  CHECK(forward.lx == 0 && forward.ly == kStickMax);
  auto left = press({V::kA});
  CHECK(left.lx == -kStickMax && left.ly == 0);
  auto both_forward = press({V::kW, V::kUp});  // two keys, one direction
  CHECK(both_forward.ly == kStickMax);
  auto cancel = press({V::kW, V::kS});  // opposing keys cancel
  CHECK(cancel.lx == 0 && cancel.ly == 0);
  auto diagonal = press({V::kS, V::kD});  // stays inside the stick's circle
  CHECK(diagonal.lx == kStickDiagonal && diagonal.ly == -kStickDiagonal);
  CHECK(int64_t(diagonal.lx) * diagonal.lx + int64_t(diagonal.ly) * diagonal.ly <=
        int64_t(kStickMax) * kStickMax);
  auto actions = press({V::kE, V::kQ, V::kR, V::kW});
  CHECK(actions.buttons == B::X_INPUT_GAMEPAD_A);
  CHECK(actions.left_trigger == 0xFF && actions.right_trigger == 0xFF);
  CHECK(actions.ly == kStickMax);

  // 6) Mouse -> right stick: scaled, Y inverted, clamped to the stick range.
  auto still = MouseDeltaToStick(0, 0, 256);
  CHECK(still.x == 0 && still.y == 0);
  auto moved = MouseDeltaToStick(10, -2, 256);
  CHECK(moved.x == 2560 && moved.y == 512);
  auto flick = MouseDeltaToStick(-5000, 5000, 256);
  CHECK(flick.x == -kStickMax && flick.y == -kStickMax);

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("PASS: keyboard/mouse gamepad map parsing\n");
  return 0;
}
