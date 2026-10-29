#include "local_input.h"
#include "pad_mapping.h"

#include <rex/cvar.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/input/nop/nop_input_driver.h>
#include <rex/input/sdl/sdl_input_driver.h>
#include <rex/input/flags.h>
#include <rex/runtime.h>
#ifdef REDBARON_INPUT_FEED
#include "../tests/input_feed.h"
#endif

REXCVAR_DEFINE_UINT32(redbaron_keyboard_player, 0, "Input",
                     "Keyboard/mouse player: 0 shares player 1; 1..4 reserves a slot")
    .range(0, 4)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

REXCVAR_DEFINE_STRING(redbaron_pad_slots,"auto;auto;auto;auto","Input","Per-player controller GUID#occurrence, auto or none")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
#define PAD_FLAG(n) REXCVAR_DEFINE_STRING(redbaron_pad_map_p##n,"","Input","Controller action mapping for player " #n).lifecycle(rex::cvar::Lifecycle::kInitOnly)
PAD_FLAG(1); PAD_FLAG(2); PAD_FLAG(3); PAD_FLAG(4);

namespace redbaron {
using rex::X_STATUS;
using rex::X_RESULT;

namespace {
constexpr uint16_t kButtons[]={0x1000,0x2000,0x4000,0x8000,0x0100,0x0200,0x0020,0x0010,0x0040,0x0080,0x0001,0x0002,0x0004,0x0008};
class MappedPadDriver final:public rex::input::InputDriver {
 public:
  MappedPadDriver(LocalDeviceAssignment* routing,std::array<PadMap,4> maps)
      :InputDriver(nullptr,0),routing_(routing),maps_(std::move(maps)),driver_(nullptr,0) {
    driver_.set_is_active_callback([this]{return is_active();});
  }
  X_STATUS Setup() override {return driver_.Setup();}
  void OnWindowAvailable(rex::ui::Window* window) override {driver_.OnWindowAvailable(window);}
  void EnumerateDevices(std::vector<rex::input::DeviceInfo>& out) override {driver_.EnumerateDevices(out);}
  X_RESULT GetDeviceState(rex::input::DeviceId id,rex::input::X_INPUT_STATE* out) override {
    auto result=driver_.GetDeviceState(id,out);
    auto player=routing_->PlayerForDevice(id);
    if(result!=X_ERROR_SUCCESS || player>=4 || !out) return result;
    const auto original=out->gamepad;
    PadValues values{};
    for(size_t i=0;i<std::size(kButtons);++i) values[i]=(static_cast<uint16_t>(original.buttons)&kButtons[i])?32767:0;
    values[14]=original.left_trigger*32767/255;
    values[15]=original.right_trigger*32767/255;
    const auto axis=[&](int v,size_t positive,size_t negative){values[positive]=std::max(0,v);values[negative]=std::max(0,-v);};
    axis(original.thumb_ly,16,17); axis(original.thumb_lx,19,18);
    axis(original.thumb_ry,20,21); axis(original.thumb_rx,23,22);
    auto mapped=RemapPad(values,maps_[player]);
    uint16_t buttons=static_cast<uint16_t>(original.buttons)&0x0400;
    for(size_t i=0;i<std::size(kButtons);++i) if(mapped[i]>16383) buttons|=kButtons[i];
    out->gamepad.buttons=buttons;
    out->gamepad.left_trigger=static_cast<uint8_t>(std::min(255,(mapped[14]*255+32766)/32767));
    out->gamepad.right_trigger=static_cast<uint8_t>(std::min(255,(mapped[15]*255+32766)/32767));
    const auto stick=[&](size_t p,size_t n){return static_cast<int16_t>(std::clamp(mapped[p]-mapped[n],-32768,32767));};
    out->gamepad.thumb_ly=stick(16,17); out->gamepad.thumb_lx=stick(19,18);
    out->gamepad.thumb_ry=stick(20,21); out->gamepad.thumb_rx=stick(23,22);
    return result;
  }
  X_RESULT GetDeviceCapabilities(rex::input::DeviceId id,uint32_t flags,rex::input::X_INPUT_CAPABILITIES* caps) override {return driver_.GetDeviceCapabilities(id,flags,caps);}
  X_RESULT SetDeviceVibration(rex::input::DeviceId id,rex::input::X_INPUT_VIBRATION* v) override {return driver_.SetDeviceVibration(id,v);}
  // The title uses XamInputGetState, not keystroke events. Preserve the SDK path.
  X_RESULT GetDeviceKeystroke(rex::input::DeviceId id,uint32_t flags,rex::input::X_INPUT_KEYSTROKE* key) override {return driver_.GetDeviceKeystroke(id,flags,key);}
 private:
  LocalDeviceAssignment* routing_;
  std::array<PadMap,4> maps_;
  rex::input::sdl::SDLInputDriver driver_;
};

std::unique_ptr<LocalDeviceAssignment> MakeRouting() {
  uint32_t keyboard=REXCVAR_GET(redbaron_keyboard_player);
  if(!rex::cvar::Query<bool>("mnk_mode")) keyboard=0;
  auto policy=std::make_unique<LocalDeviceAssignment>(keyboard,[](const rex::input::DeviceInfo& d,uint32_t slot){
    if(slot<4) REXLOG_INFO("RedBaron input: {} -> player {}",d.name,slot+1);
    else REXLOG_WARN("RedBaron input: {} is unassigned",d.name);
  });
  std::array<std::string,4> selections;
  // Keep the owning cvar string alive while parsing views.
  const auto owned=REXCVAR_GET(redbaron_pad_slots);
  std::string_view remaining=owned;
  bool valid=true;
  for(size_t i=0;i<4;++i) {
    auto pos=remaining.find(';');
    selections[i]=remaining.substr(0,pos);
    if(i<3) {if(pos==remaining.npos){valid=false;break;} remaining.remove_prefix(pos+1);}
    else if(pos!=remaining.npos) valid=false;
  }
  for(size_t i=0;i<4 && valid;++i) {
    if(selections[i].empty()) valid=false;
    if(selections[i]!="auto" && selections[i]!="none")
      for(size_t j=0;j<i;++j) if(selections[j]==selections[i]) valid=false;
  }
  if(valid) policy->SetSelections(std::move(selections));
  else REXLOG_WARN("Invalid redbaron_pad_slots; using default routing");
  return policy;
}
} // namespace

void ConfigureLauncherInput(rex::RuntimeConfig& config) {
#ifdef REDBARON_INPUT_FEED
  const auto feed = REXCVAR_GET(redbaron_test_input);
  if (!feed.empty()) {
    config.input_factory = [feed](bool) -> std::unique_ptr<rex::system::IInputSystem> {
      auto input = std::make_unique<rex::input::InputSystem>(nullptr);
      input->AddDriver(std::make_unique<TestInputFeed>(feed));
      input->SetDeviceAssignment(std::make_unique<LocalDeviceAssignment>(0));
      return input;
    };
    return;
  }
#endif
  // The launcher selects SDL. Existing manually selected XInput remains intact.
  if(REXCVAR_GET(input_backend)!="sdl") return;
  config.input_factory=[](bool tool_mode)->std::unique_ptr<rex::system::IInputSystem> {
    auto input=std::make_unique<rex::input::InputSystem>(nullptr);
    auto routing=MakeRouting();
    if(!tool_mode) {
      std::array<PadMap,4> maps;
      for(size_t i=0;i<4;++i) {
        maps[i]=IdentityPadMap();
        if(!ParsePadMap(rex::cvar::GetFlagByName("redbaron_pad_map_p"+std::to_string(i+1)),maps[i]))
          REXLOG_WARN("Invalid controller mapping for player {}; using defaults",i+1);
      }
      auto pad=std::make_unique<MappedPadDriver>(routing.get(),maps);
      if(pad->Setup()==X_STATUS_SUCCESS) input->AddDriver(std::move(pad));
      auto mnk=std::make_unique<rex::input::mnk::MnkInputDriver>(nullptr,0);
      if(mnk->Setup()==X_STATUS_SUCCESS) input->AddDriver(std::move(mnk));
    }
    input->AddDriver(std::make_unique<rex::input::nop::NopInputDriver>(nullptr,tool_mode?0:1));
    input->SetDeviceAssignment(std::move(routing));
    REXLOG_INFO("RedBaron launcher mappings active (SDL), with separate local profiles");
    return input;
  };
}

void ConfigureLocalInput(rex::input::InputSystem& input) {
  if(REXCVAR_GET(input_backend)=="sdl") return; // factory installed the shared routing policy
  uint32_t keyboard_player = REXCVAR_GET(redbaron_keyboard_player);
  if (keyboard_player && !rex::cvar::Query<bool>("mnk_mode")) {
    REXLOG_WARN("RedBaron input: keyboard slot ignored because mnk_mode is disabled");
    keyboard_player = 0;
  }
  REXLOG_INFO("RedBaron input: keyboard_player={} (0=shared)", keyboard_player);
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
