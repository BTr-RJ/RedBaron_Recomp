#include "local_input.h"

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>

REXCVAR_DEFINE_UINT32(redbaron_keyboard_player, 0, "Input",
                     "Keyboard/mouse player: 0 shares player 1; 1..4 reserves a slot")
    .range(0, 4)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace redbaron {

void ConfigureLocalInput(rex::input::InputSystem& input) {
  uint32_t keyboard_player = REXCVAR_GET(redbaron_keyboard_player);
  if (keyboard_player && !rex::cvar::Query<bool>("mnk_mode")) {
    REXLOG_WARN("RedBaron input: keyboard slot ignored because mnk_mode is disabled");
    keyboard_player = 0;
  }
  REXLOG_INFO("RedBaron input: keyboard_player={} (0=shared); routing only, "
              "additional local profiles are not implemented yet", keyboard_player);
  input.SetDeviceAssignment(std::make_unique<LocalDeviceAssignment>(
      keyboard_player, [](const rex::input::DeviceInfo& device, uint32_t slot) {
        if (slot < rex::input::kMaxGuestUsers) {
          REXLOG_INFO("RedBaron input: {} -> player {} ({})", device.name, slot + 1,
                      device.synthetic ? "virtual input" : "gamepad");
        } else {
          REXLOG_WARN("RedBaron input: {} has no free player slot", device.name);
        }
      }));
}

}  // namespace redbaron
