#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/ui/imgui_dialog.h>

REXCVAR_DEFINE_BOOL(redbaron_show_fps, false, "Video",
                    "Show the in-game FPS counter")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(redbaron_skip_splash, true, "Video",
                    "Skip the incorrectly rendered startup images")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_UINT32(
    redbaron_fps, 0, "Video",
    "Engine frame-rate target; 0 preserves the original game setting")
    .allowed({"0", "30", "60", "90", "120", "144", "165", "240"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace redbaron {
namespace {
PPCFunc *original_frame_interval = nullptr;
PPCFunc *original_splash_update = nullptr;
PPCFunc *original_display_tick = nullptr;
std::atomic<uint64_t> game_frames{0};
PPCFunc *original_world_tick = nullptr;
PPCFunc *original_physics_begin = nullptr;
PPCFunc *original_simulate = nullptr;
PPCFunc *original_set_timing = nullptr;
rex::runtime::FunctionDispatcher *video_dispatcher = nullptr;
std::atomic<double> world_delta{1.0 / 30.0};
thread_local double physics_delta = 1.0 / 30.0;

void SkipSplash(PPCContext &ctx, uint8_t *base) {
  // The startup object's current image and image count are at these offsets.
  // Keep the original loading and transition logic running.
  const auto object = ctx.r3.u32;
  const auto address = [base](uint32_t guest) {
#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)
    return base + guest + (guest >= 0xE0000000u ? 0x1000u : 0u);
#else
    return base + guest;
#endif
  };
  // Copy the value in guest byte order unchanged.
  *reinterpret_cast<volatile uint32_t *>(address(object + 10120)) =
      *reinterpret_cast<volatile uint32_t *>(address(object + 10124));
  original_splash_update(ctx, base);
}

void DisplayTick(PPCContext &ctx, uint8_t *base) {
  original_display_tick(ctx, base);
  game_frames.fetch_add(1, std::memory_order_relaxed);
}

class FpsCounter final : public rex::ui::ImGuiDialog {
public:
  explicit FpsCounter(rex::ui::ImGuiDrawer *drawer) : ImGuiDialog(drawer) {}

private:
  void OnDraw(ImGuiIO &io) override {
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - start_).count();
    if (seconds >= .5) {
      const auto frames = game_frames.load(std::memory_order_relaxed);
      fps_ = float((frames - previous_) / seconds);
      previous_ = frames;
      start_ = now;
    }
    // Keep the original player names and split-screen timers unobstructed.
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * .5f, io.DisplaySize.y - 8),
                            ImGuiCond_Always, ImVec2(.5f, 1));
    ImGui::SetNextWindowBgAlpha(.75f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    constexpr auto flags = ImGuiWindowFlags_NoDecoration |
                           ImGuiWindowFlags_AlwaysAutoResize |
                           ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                           ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::Begin("##redbaron_fps", nullptr, flags)) {
      const bool hdr = rex::cvar::GetFlagByName("redbaron_hdr_active") == "true";
      ImGui::Text("%.0f FPS  |  %.1f ms  |  %s", fps_, fps_ > 0 ? 1000.f / fps_ : 0.f,
                  hdr ? "HDR10" : "SDR");
    }
    ImGui::End();
    ImGui::PopStyleVar();
  }
  std::chrono::steady_clock::time_point start_ =
      std::chrono::steady_clock::now();
  uint64_t previous_ = 0;
  float fps_ = 0;
};

void WorldTick(PPCContext &ctx, uint8_t *base) {
  // This is the game's pre-update, before its scenes dispatch physics work.
  // Use the engine's measured delta, including pauses and slow frames.
  world_delta.store(std::clamp(ctx.f1.f64, 0.000001, 0.1),
                    std::memory_order_relaxed);
  original_world_tick(ctx, base);
}

void PhysicsTiming(PPCContext &ctx, uint8_t *base) {
  if (ctx.lr == 0x8281D72C) {
    // Keep the fixed-step PhysX solver, but fit an integer number of equal
    // substeps inside this frame. The original always used two 1/60 s steps.
    const auto steps = std::max(1u, uint32_t(std::ceil(physics_delta * 60.0)));
    ctx.f1.f64 = double(float(physics_delta / steps));
    ctx.r5.u32 = steps;
  }
  original_set_timing(ctx, base);
}

void PhysicsSimulate(PPCContext &ctx, uint8_t *base) {
  if (ctx.lr == 0x8281D744) {
    ctx.f1.f64 = double(float(physics_delta));
    if (rex::cvar::GetFlagByName("video_diagnostics") == "true") {
      static auto start = std::chrono::steady_clock::now();
      static double simulation_seconds = 0;
      static unsigned frames = 0;
      simulation_seconds += ctx.f1.f64;
      ++frames;
      const auto now = std::chrono::steady_clock::now();
      const double wall = std::chrono::duration<double>(now - start).count();
      if (wall >= 5) {
        REXLOG_INFO("RedBaron physics: {} updates, {:.3f} simulation seconds / "
                    "{:.3f} wall seconds",
                    frames, simulation_seconds, wall);
        start = now;
        simulation_seconds = 0;
        frames = 0;
      }
    }
  }
  original_simulate(ctx, base);
}

void PhysicsBegin(PPCContext &ctx, uint8_t *base) {
  physics_delta = world_delta.load(std::memory_order_relaxed);
  const uint32_t scene =
      rex::memory::load_and_swap<uint32_t>(base + ctx.r3.u32 + 28352);
  if (scene && !original_simulate) {
    const uint32_t table = rex::memory::load_and_swap<uint32_t>(base + scene);
    const uint32_t timing =
        rex::memory::load_and_swap<uint32_t>(base + table + 328);
    const uint32_t simulate =
        rex::memory::load_and_swap<uint32_t>(base + table + 560);
    original_set_timing = video_dispatcher->GetFunction(timing);
    original_simulate = video_dispatcher->GetFunction(simulate);
    if (!original_set_timing || !original_simulate ||
        !video_dispatcher->SetFunction(timing, PhysicsTiming) ||
        !video_dispatcher->SetFunction(simulate, PhysicsSimulate)) {
      throw std::runtime_error(
          "Could not install the physics time-step correction.");
    }
    REXLOG_INFO(
        "RedBaron physics correction: timing=0x{:08X}, simulate=0x{:08X}",
        timing, simulate);
  }
  original_physics_begin(ctx, base);
}

void FrameInterval(PPCContext &ctx, uint8_t *base) {
  original_frame_interval(ctx, base);
  // The engine requests seconds per frame through its display configuration
  // vtable. Physics time is corrected separately at the actual simulate call.
  ctx.f1.f64 = double(1.0f / float(REXCVAR_GET(redbaron_fps)));
}
} // namespace

std::unique_ptr<rex::ui::ImGuiDialog>
CreateFpsCounter(rex::ui::ImGuiDrawer *drawer) {
  return REXCVAR_GET(redbaron_show_fps) ? std::make_unique<FpsCounter>(drawer)
                                        : nullptr;
}

void ConfigureVideoTiming() {
  const auto fps = REXCVAR_GET(redbaron_fps);
  if (!fps)
    return;
  // The guest display may wait on virtual vblank independently of DXGI VSync.
  rex::cvar::SetFlagByName("video_mode_refresh_rate",
                           std::to_string(std::max(60u, fps)));
  rex::cvar::SetFlagByName("vsync", "true");
}

void InstallVideoHooks(rex::Runtime &runtime) {
  const auto fps = REXCVAR_GET(redbaron_fps);
  auto *dispatcher = runtime.function_dispatcher();
  if (REXCVAR_GET(redbaron_skip_splash)) {
    original_splash_update = dispatcher->GetFunction(0x829AE1D8);
    if (!original_splash_update || !dispatcher->SetFunction(0x829AE1D8, SkipSplash))
      throw std::runtime_error("Could not skip the startup images.");
  }
  if (REXCVAR_GET(redbaron_show_fps)) {
    original_display_tick = dispatcher->GetFunction(0x828D6548);
    if (!original_display_tick ||
        !dispatcher->SetFunction(0x828D6548, DisplayTick))
      throw std::runtime_error("Could not install the FPS counter.");
  }
  if (!fps)
    return;
  video_dispatcher = dispatcher;
  // Snoopy Flying Ace 584109B5: native display frame-interval getter.
  constexpr uint32_t kFrameInterval = 0x8294BB20;
  original_frame_interval = dispatcher->GetFunction(kFrameInterval);
  if (!original_frame_interval ||
      !dispatcher->SetFunction(kFrameInterval, FrameInterval)) {
    throw std::runtime_error("Could not install the engine frame-rate target.");
  }
  original_world_tick = dispatcher->GetFunction(0x82976FA8);
  original_physics_begin = dispatcher->GetFunction(0x8281D6C8);
  if (!original_world_tick || !original_physics_begin ||
      !dispatcher->SetFunction(0x82976FA8, WorldTick) ||
      !dispatcher->SetFunction(0x8281D6C8, PhysicsBegin)) {
    throw std::runtime_error(
        "Could not install the game simulation timing hooks.");
  }
  REXLOG_INFO(
      "RedBaron engine target: {} FPS; physics uses measured game delta", fps);
}
} // namespace redbaron
