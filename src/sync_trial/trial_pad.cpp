// Sync trial: a pad driven by a text file, standing in for the fork's remote-control pad.
//
// Upstream's keyboard pad ignores keys unless the window has focus. This pad does not, so a
// script can drive the game while another window is in front. The file <exe>/trial_pad.txt
// holds the buttons held down, space separated (A B X Y START BACK UP DOWN LEFT RIGHT LB RB).
// Select it with --hid.drivers=trialpad,sdl3.
#include <rex/driver_module.h>
#include <rex/driver_registration.h>
#include <rex/hid/input_driver.h>
#include <rex/logging/log_category.h>
#include <rex/module.h>
#include <rex/platform/file.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <sstream>
#include <string>

namespace sync_trial {

using namespace rex;
using namespace rex::hid;

constexpr PadDeviceID kTrialPad{0x54524941'4C504144ull};

class TrialPadDriver final : public IInputDriver {
 public:
  std::string_view GetName() const override { return "trialpad"; }
  Result<void> Initialize(IDriverContext&) override {
    path_ = rex::platform::GetExecutableFolder() / "trial_pad.txt";
    REXSYS_INFO("[trialpad] driver initialized, reading {}", path_.string());
    return {};
  }
  void Shutdown() override {}
  u64 GetDeviceRevision() const override { return 1; }
  void EnumerateDevices(std::vector<PadDevice>& out) override {
    REXSYS_DEBUG("[trialpad] enumerate");
    PadDevice d;
    d.id = kTrialPad;
    d.name = "Sync trial pad";
    d.info.type = PadType::kX360;
    out.push_back(d);
  }
  Result<PadState> GetDeviceState(PadDeviceID) override {
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    if (now - last_read_ > std::chrono::milliseconds(15)) {
      last_read_ = now;
      state_ = {};
      std::ifstream f(path_);
      std::string word;
      while (f >> word) {
        if (word == "A") state_.buttons |= kPadA;
        else if (word == "B") state_.buttons |= kPadB;
        else if (word == "X") state_.buttons |= kPadX;
        else if (word == "Y") state_.buttons |= kPadY;
        else if (word == "START") state_.buttons |= kPadStart;
        else if (word == "BACK") state_.buttons |= kPadBack;
        else if (word == "UP") state_.buttons |= kPadDPadUp;
        else if (word == "DOWN") state_.buttons |= kPadDPadDown;
        else if (word == "LEFT") state_.buttons |= kPadDPadLeft;
        else if (word == "RIGHT") state_.buttons |= kPadDPadRight;
        else if (word == "LB") state_.buttons |= kPadLeftShoulder;
        else if (word == "RB") state_.buttons |= kPadRightShoulder;
      }
    }
    if (state_.buttons != last_logged_) {
      last_logged_ = state_.buttons;
      REXSYS_INFO("[trialpad] buttons {:#06x}", state_.buttons);
    }
    return state_;
  }
  Result<PadCapabilities> GetDeviceCapabilities(PadDeviceID) override {
    PadCapabilities caps;
    caps.supported.buttons = 0xF3FF;
    caps.supported.left_trigger = caps.supported.right_trigger = 0xFF;
    caps.supported.thumb_lx = caps.supported.thumb_ly = 0x7FFF;
    caps.supported.thumb_rx = caps.supported.thumb_ry = 0x7FFF;
    return caps;
  }
  Result<void> SetDeviceVibration(PadDeviceID, const PadVibration&) override { return {}; }

 private:
  std::filesystem::path path_;
  std::mutex mutex_;
  std::chrono::steady_clock::time_point last_read_{};
  PadState state_{};
  u16 last_logged_ = 0;
};

std::unique_ptr<IDriver> CreateTrialPad() {
  return std::make_unique<TrialPadDriver>();
}

constexpr std::array<DriverRegistration, 1> kDrivers = {
    DriverRegistration{DriverType::kInput, "trialpad", {}, &CreateTrialPad}};

class TrialPadModule final : public IModule, public IDriverModule {
 public:
  REX_MODULE_SERVES(::rex::IDriverModule)
  std::string_view GetName() const override { return "fable2-trialpad"; }
  Result<void> Connect(IInterfaceFactory&) override { return {}; }
  void Disconnect() override {}
  Result<void> Init() override { return {}; }
  void Shutdown() override {}
  IInterface* QueryInterface(std::string_view name, u32 version) override {
    return FindModuleInterface<IDriverModule>(*this, name, version);
  }
  std::span<const DriverRegistration> GetDrivers() const override { return kDrivers; }
};

IModule* GetTrialPadModule() {
  static TrialPadModule module;
  return &module;
}

}  // namespace sync_trial
