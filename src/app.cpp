// Web of Shadows PS5 application integration.
#include "wos_recomp_init.h"
#include "import_dialog.h"
#include "graphics_backend.h"

#include <memory>
#include <string>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/rex_app.h>

class WosPs5App final : public rex::ReXApp {
 public:
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
    return std::unique_ptr<WosPs5App>(new WosPs5App(context));
  }

 private:
  explicit WosPs5App(rex::ui::WindowedAppContext& context)
      : rex::ReXApp(context, "wos_recomp", PPCImageConfig) {}

  void OnConfigurePaths(rex::PathConfig& paths) override {
    paths.game_data_root = "/app0/gamedata";
    paths.user_data_root = "/app0/userdata";
    paths.cache_root = "/app0/cache";
    paths.config_path = "/app0/wos_recomp.toml";
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
#if WOS_COMMAND_RING_SPIN_LIMIT != 500
    rex::cvar::SetFlagByName("wos_command_ring_spin_limit", std::to_string(WOS_COMMAND_RING_SPIN_LIMIT));
    REXLOG_INFO("Web of Shadows: command-ring spin limit {}", WOS_COMMAND_RING_SPIN_LIMIT);
#endif
#if WOS_REUSE_TEXTURE_BINDINGS
    rex::cvar::SetFlagByName("wos_reuse_texture_bindings", "true");
    REXLOG_INFO("Web of Shadows: exact texture descriptor reuse enabled");
#endif
#if WOS_FRAME_COST_DIAGNOSTICS
    rex::cvar::SetFlagByName("wos_command_wait_costs", "true");
    rex::cvar::SetFlagByName("wos_frame_costs", "true");
    REXLOG_INFO("Web of Shadows: coarse host renderer diagnostics enabled; timings overlap");
#endif
    // Construct the statically linked Vulkan backend directly. PS5 titles cannot
    // load the desktop rexgpu-xenos DLL/shared library.
    config.gpu_plugin.clear();
    config.graphics = wos::CreateGraphicsSystem();
    rex::cvar::SetFlagByName("mnk_mode", "false");
    rex::cvar::SetFlagByName("fullscreen", "true");
    const auto set_default = [](std::string_view name, std::string_view value) {
      if (rex::cvar::GetFlagSource(name) == rex::cvar::Source::kDefault)
        rex::cvar::SetFlagByName(name, value);
      return rex::cvar::GetFlagByName(name);
    };
    const auto background_shaders = set_default("async_shader_compilation", "true");
    // The upstream WoS app selects the D3D12 ROV path. Use Vulkan's equivalent
    // manual EDRAM/blending path, supported by the console's shader interlock.
    const auto render_target_path = set_default("render_target_path_vulkan", "fsi");
    REXLOG_INFO("Web of Shadows: PS5 Vulkan backend, controller input");
    // Match the upstream game's no-readback memory policy. Avoid downloading
    // shader exports to the CPU and invalidating physical page state each frame.
    const auto memexport_readback = set_default("readback_memexport", "false");
    const auto page_state_refresh = set_default("clear_memory_page_state", "false");
    const auto anisotropic_override = set_default("anisotropic_override", "0");
    REXLOG_INFO("Web of Shadows: background shader setup {}", background_shaders);
    REXLOG_INFO("Web of Shadows: render-target path {}", render_target_path);
    REXLOG_INFO("Web of Shadows: memexport readback {}, page refresh {}, anisotropic override {}",
                memexport_readback, page_state_refresh, anisotropic_override);
  }

  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& paths, std::function<void(rex::PathConfig)> resume) override {
    wos::ShowImporter(imgui_drawer(), paths, std::move(resume));
    return std::nullopt;
  }
};

REX_DEFINE_APP(wos_recomp, WosPs5App::Create)
