// Sync trial: the game app on upstream ReXGlue v0.11.0 (rex::app::TitleApp).
//
// The minimum of src/main.cpp and src/core/fable_2_app.h that needs no fork-only
// SDK API: the game's own settings, its paths, its config file and logging.
// Everything else is stubbed; see "Stubs" in the trial report.
#include <rex/app/title_app.h>
#include <rex/module.h>
#include <rex/logging/log_category.h>
#include <rex/path_settings.h>
#include <rex/platform/file.h>
#include <rex/platform/process.h>
#include <rex/setting.h>
#include <rex/settings_registry.h>

#include <filesystem>
#include <string>
#include <system_error>

#include "generated/default/fable_2_init.h"

#include "fable2_config.h"

namespace sync_trial {
rex::IModule* GetTrialPadModule();
}

// The cvars of src/main.cpp as upstream settings under the scope "fable2".
// The old bare names stay usable through aliases (see OnConfigure).
class Fable2Settings : public rex::SettingOwner {
 public:
  Fable2Settings() : SettingOwner("fable2") {}

  REX_SETTING(std::string, keyboard_gamepad_map, "",
              "Map host keyboard keys / mouse buttons to guest gamepad input (Key:Button,...)");
  REX_SETTING(bool, mouse_look, true, "Map mouse movement to the guest right stick");
  REX_SETTING(rex::i32, mouse_look_scale, 256, "Mouse-look sensitivity",
              rex::Range{1, 4096});
  REX_SETTING(bool, mouse_look_smoothing, true, "Turn the mouse into an even camera turn");
  REX_SETTING(rex::i32, mouse_look_window_ms, 0, "Mouse-look velocity window in milliseconds",
              rex::Range{0, 500});
  REX_SETTING(bool, mouse_look_diag, false, "Write mouse-look diagnostics");
  REX_SETTING(std::string, mouse_look_mode, "direct", "\"direct\" or \"stick\"");
  REX_SETTING(rex::f64, mouse_look_sens_x, 0.06, "Direct mouse camera: yaw degrees per count");
  REX_SETTING(rex::f64, mouse_look_sens_y, 0.06, "Direct mouse camera: up/down sensitivity");
  REX_SETTING(bool, mouse_controller_priority, true,
              "Ignore mouse buttons and movement while a controller is in use");
  REX_SETTING(bool, mouse_look_invert_y, false, "Direct mouse camera: invert up/down");
  REX_SETTING(rex::f64, mouse_look_direct_smoothing, 0.0,
              "Direct mouse camera: share of the movement held back", rex::Range{0.0, 0.9});
  REX_SETTING(std::string, mouse_unlock_key, "F4", "Key that toggles the debug menu");
  REX_SETTING(bool, skipHash, false, "Skip the default.xex SHA-256 integrity check");
};

class Fable2App : public rex::app::TitleApp {
 public:
  Fable2App() : TitleApp("fable_2", kPPCImageConfig) {}

 protected:
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    if (auto r = settings.Register(settings_); !r) return r;
    for (const char* name :
         {"keyboard_gamepad_map", "mouse_look", "mouse_look_scale", "mouse_look_smoothing",
          "mouse_look_window_ms", "mouse_look_diag", "mouse_look_mode", "mouse_look_sens_x",
          "mouse_look_sens_y", "mouse_controller_priority", "mouse_look_invert_y",
          "mouse_look_direct_smoothing", "mouse_unlock_key", "skipHash"}) {
      settings.AddAlias(name, std::string("fable2.") + name);
    }

    // Paths, as Fable2App::OnConfigurePaths: content next to the exe unless
    // --game_data_root says otherwise; saves and caches inside the exe folder.
    const std::filesystem::path exe_dir = rex::platform::GetExecutableFolder();
    if (std::filesystem::is_regular_file(exe_dir / "default.xex")) {
      (void)settings.SetText(rex::SettingLayer::kTitle, "title.game_root", exe_dir.string());
    }
    std::error_code ec;
    std::filesystem::create_directories(exe_dir / "saves", ec);
    std::filesystem::create_directories(exe_dir / "cache", ec);
    (void)REX_PATHS().user_root.Set(exe_dir / "saves");
    (void)REX_PATHS().cache_root.Set(exe_dir / "cache");

    // fable2_config.toml, as Fable2App::OnPostInitLogging. The seeds go to the
    // title layer, so fable_2.toml, REX_ variables and the command line still win.
    fable2::config::Load(exe_dir / "fable2_config.toml");
    const auto& cfg = fable2::config::Get();
    (void)settings_.keyboard_gamepad_map.Set(cfg.keyboard_gamepad_map);
    (void)settings_.mouse_look.Set(cfg.mouse_look);
    (void)settings_.mouse_look_scale.Set(cfg.mouse_look_scale);
    (void)settings_.mouse_look_smoothing.Set(cfg.mouse_look_smoothing);
    (void)settings_.mouse_look_window_ms.Set(cfg.mouse_look_window_ms);
    (void)settings_.mouse_look_diag.Set(cfg.mouse_look_diag);
    (void)settings_.mouse_look_mode.Set(cfg.mouse_look_mode);
    (void)settings_.mouse_look_sens_x.Set(cfg.mouse_look_sens_x);
    (void)settings_.mouse_look_sens_y.Set(cfg.mouse_look_sens_y);
    (void)settings_.mouse_look_invert_y.Set(cfg.mouse_look_invert_y);
    (void)settings_.mouse_look_direct_smoothing.Set(cfg.mouse_look_direct_smoothing);
    return {};
  }

  rex::Result<void> OnInit() override {
    REXSYS_INFO("[native-build] configuration={} linked_sdk=upstream-0.11.0 (sync trial)",
                FABLE2_NATIVE_CONFIGURATION);
    REXSYS_INFO("[sync-trial] game root {}, user root {}",
                rex::platform::GetExecutableFolder().string(),
                REX_PATHS().user_root.Get().string());
    return {};
  }

  rex::Result<void> OnTitleLaunch(rex::kernel::TitleRuntime& title) override {
    // Stubbed for the trial: game patches, address scan, mouse look and keycap
    // prompt setup (Fable2App::OnPostLoadXexImage).
    REXSYS_INFO("[sync-trial] title launch; game patches stubbed");
    return TitleApp::OnTitleLaunch(title);
  }

 void OnRegisterModules(std::vector<rex::IModule*>& modules) override {
    modules.push_back(sync_trial::GetTrialPadModule());
  }

 private:
  Fable2Settings settings_;
};

REX_TITLE_APP(Fable2App)
