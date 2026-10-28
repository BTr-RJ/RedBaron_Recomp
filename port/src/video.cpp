#include <algorithm>
#include <stdexcept>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>

REXCVAR_DEFINE_UINT32(redbaron_fps, 0, "Video",
                     "Engine frame-rate target; 0 preserves the original game setting")
    .allowed({"0", "30", "60", "90", "120", "144", "165", "240"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace redbaron {
namespace {
PPCFunc* original_frame_interval = nullptr;

void FrameInterval(PPCContext& ctx, uint8_t* base) {
  original_frame_interval(ctx, base);
  // The engine requests seconds per frame through its display configuration
  // vtable. Keep its wall-clock delta calculation, physics and audio clocks.
  // This is deliberately not a system-time multiplier.
  ctx.f1.f64 = double(1.0f / float(REXCVAR_GET(redbaron_fps)));
}
}  // namespace

void ConfigureVideoTiming() {
  const auto fps = REXCVAR_GET(redbaron_fps);
  if (!fps) return;
  // The guest display may wait on virtual vblank independently of DXGI VSync.
  rex::cvar::SetFlagByName("video_mode_refresh_rate", std::to_string(std::max(60u, fps)));
  rex::cvar::SetFlagByName("vsync", "true");
}

void InstallVideoHooks(rex::Runtime& runtime) {
  const auto fps = REXCVAR_GET(redbaron_fps);
  if (!fps) return;
  auto* dispatcher = runtime.function_dispatcher();
  // Snoopy Flying Ace 584109B5: native display frame-interval getter.
  constexpr uint32_t kFrameInterval = 0x8294BB20;
  original_frame_interval = dispatcher->GetFunction(kFrameInterval);
  if (!original_frame_interval ||
      !dispatcher->SetFunction(kFrameInterval, FrameInterval)) {
    throw std::runtime_error("Could not install the engine frame-rate target.");
  }
  REXLOG_INFO("RedBaron engine target: {} FPS; original simulation clock preserved", fps);
}
}  // namespace redbaron
