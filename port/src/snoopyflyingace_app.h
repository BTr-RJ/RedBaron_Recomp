// snoopyflyingace - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/filesystem.h>
#include <rex/input/flags.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/system/flags.h>

class SnoopyflyingaceApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<SnoopyflyingaceApp>(new SnoopyflyingaceApp(ctx, "snoopyflyingace",
        PPCImageConfig));
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty()) {
      return;
    }

    // Workspace layout: build_cpp_release/snoopyflyingace.exe and game/.
    // Resolve from the executable, so double-click and shortcuts work from
    // any working directory. Explicit runtime paths retain precedence.
    const auto game_dir = rex::filesystem::GetExecutableFolder().parent_path() / "game";
    std::error_code ec;
    if (std::filesystem::is_regular_file(game_dir / "default.xex", ec)) {
      paths.game_data_root = game_dir;
    }
  }

  void OnPreSetup(rex::RuntimeConfig& /*config*/) override {
    // Resolve mappings beside the EXE, independent of shortcut/terminal cwd.
    // An empty setting intentionally disables the external controller DB.
    std::filesystem::path mappings_file = REXCVAR_GET(hid_mappings_file);
    if (!mappings_file.empty() && mappings_file.is_relative()) {
      mappings_file = rex::filesystem::GetExecutableFolder() / mappings_file;
      REXCVAR_SET(hid_mappings_file, mappings_file.lexically_normal().string());
    }
    REXLOG_INFO("Snoopy input: backend={}, keyboard/mouse={}, mouse motion={}, mappings={}",
                REXCVAR_GET(input_backend), rex::cvar::GetFlagByName("mnk_mode"),
                rex::cvar::GetFlagByName("mnk_mouse"), REXCVAR_GET(hid_mappings_file));

    // sub_8283F2A0 reads XamContentGetLicenseMask through sub_823A0510
    // and tests bit 0. Enable the base game before any guest code runs;
    // retain other license bits supplied by the runtime configuration.
    constexpr uint32_t kFullGameLicense = 0x1;
    const uint32_t previous_mask = REXCVAR_GET(license_mask);
    REXCVAR_SET(license_mask, previous_mask | kFullGameLicense);
    REXLOG_INFO("Snoopy full-game license: mask 0x{:08X} -> 0x{:08X}",
                previous_mask, REXCVAR_GET(license_mask));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
};
