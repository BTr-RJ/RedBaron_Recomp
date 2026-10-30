#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/ppc.h>
#include <rex/ui/imgui_dialog.h>
#include <imgui.h>
#include <atomic>
#include <mutex>
#include <optional>
#include <fstream>
#ifdef _WIN32
#include <Windows.h>
#endif
#include <algorithm>
#include <cstdio>
#include <cstring>
#ifdef REDBARON_EOS_ENABLED
#include "../eos/online_service.h"
#include "../eos/invitation_record.h"
#endif

REXCVAR_DEFINE_STRING(redbaron_eos_config,"","Network","Local EOS configuration file; empty keeps existing offline/LAN behavior")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(redbaron_eos_account,true,"Network","Sign in with Epic for friends, private invitations and the social overlay")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(redbaron_eos_enabled,true,"Network","Enable EOS when a local product configuration is installed")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(redbaron_eos_bootstrapped,false,"Network","Internal: this process was started by the Epic bootstrapper")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

extern "C" void __imp__sub_8239B358(PPCContext&,uint8_t*);

namespace redbaron {
#ifdef REDBARON_EOS_ENABLED
namespace {
std::shared_ptr<eos::OnlineService> online;
std::atomic<rex::system::KernelState*> online_kernel{nullptr};
std::mutex invitation_mutex;
std::optional<rex::system::xam::LanRecord> invitation;
std::atomic<bool> show_achievements{false};
std::string JsonText(std::string_view text) {
  std::string out="\"";for(unsigned char c:text) {if(c=='"' || c=='\\'){out+='\\';out+=char(c);}else if(c>=32)out+=char(c);else out+=' ';}return out+'"';
}
void ExportAchievements(rex::Runtime& runtime) {
  const auto all=runtime.kernel_state()->loaded_achievements();if(all.empty())return;
  const auto folder=runtime.user_data_root()/"eos";std::filesystem::create_directories(folder);
  std::ofstream out(folder/"achievement-catalog.json",std::ios::binary);
  out<<"{\"titleId\":\"584109B5\",\"achievements\":[";bool first=true;
  for(const auto& a:all) {
    if(!first)out<<',';first=false;
    char id[24];std::snprintf(id,sizeof(id),"SFA_%08X",a.id);
    out<<"{\"id\":"<<a.id<<",\"eosId\":"<<JsonText(id)<<",\"label\":"<<JsonText(a.label)
       <<",\"description\":"<<JsonText(a.description)<<",\"lockedDescription\":"<<JsonText(a.unachieved_description)
       <<",\"gamerscore\":"<<a.gamerscore<<",\"flags\":"<<a.flags<<",\"icon\":"<<JsonText(a.icon_path)<<'}';
  }
  out<<"]}";
}
class OnlineDialogs final:public rex::ui::ImGuiDialog {
 public:
  explicit OnlineDialogs(rex::ui::ImGuiDrawer* drawer):ImGuiDialog(drawer){}
  void OnDraw(ImGuiIO&) override {
    if(!exported_ && rex::Runtime::instance() && online_kernel.load() && !online_kernel.load()->loaded_achievements().empty()) {
      ExportAchievements(*rex::Runtime::instance());exported_=true;
    }
    if(show_achievements.exchange(false))visible_=true;
    if(!visible_)return;
    auto* kernel=online_kernel.load();if(!kernel)return;
    ImGui::SetNextWindowSize(ImVec2(680,520),ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Achievements",&visible_)) {
      const auto all=kernel->loaded_achievements();size_t unlocked=0;
      for(const auto& a:all)if(kernel->IsAchievementUnlocked(a.id))++unlocked;
      ImGui::Text("%zu / %zu unlocked",unlocked,all.size());
      ImGui::TextWrapped("Progress is saved locally. EOS synchronization retries automatically when online and the achievement catalog is configured.");
      ImGui::Separator();ImGui::BeginChild("AchievementList");
      for(const auto& a:all) {
        const bool done=kernel->IsAchievementUnlocked(a.id);
        ImGui::TextWrapped("%s %s (%u G)",done?"[Unlocked]":"[Locked]",a.label.c_str(),a.gamerscore);
        ImGui::TextWrapped("%s",(done?a.description:a.unachieved_description).c_str());ImGui::Separator();
      }
      ImGui::EndChild();
    }
    ImGui::End();
  }
 private:bool visible_=false,exported_=false;
};
}
#endif
void ConfigureOnline() {
  if(!REXCVAR_GET(redbaron_eos_enabled))return;
  auto path=REXCVAR_GET(redbaron_eos_config);
  if(path.empty()) {
    const auto local=rex::filesystem::GetExecutableFolder()/"eos"/"config.ini";
    if(std::filesystem::is_regular_file(local))path=local.string();
  }
  if(path.empty())return;
#ifdef REDBARON_EOS_ENABLED
#ifdef _WIN32
  if(REXCVAR_GET(redbaron_eos_account) && !REXCVAR_GET(redbaron_eos_bootstrapped)) {
    const auto bootstrap=rex::filesystem::GetExecutableFolder()/"RedBaron_Online.exe";
    if(!std::filesystem::is_regular_file(bootstrap))throw std::runtime_error("Epic social components are missing; install the updated online components");
    const wchar_t* tail=GetCommandLineW();
    if(*tail==L'"'){++tail;while(*tail && *tail!=L'"')++tail;if(*tail)++tail;}
    else while(*tail && *tail!=L' ')++tail;
    std::wstring command=L"\""+bootstrap.wstring()+L"\" "+tail+L" --redbaron_eos_bootstrapped=true";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(!CreateProcessW(bootstrap.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,bootstrap.parent_path().c_str(),&startup,&process))
      throw std::runtime_error("Could not start the Epic bootstrapper");
    CloseHandle(process.hThread);CloseHandle(process.hProcess);std::exit(0);
  }
#endif
  eos::OnlineOptions options;options.config=std::filesystem::absolute(path);
  options.player_name=rex::cvar::GetFlagByName("redbaron_player_name");
  options.epic_account=REXCVAR_GET(redbaron_eos_account);
  options.invite_accepted=[](const auto& record) {
    {std::lock_guard lock(invitation_mutex);invitation=record;}
    if(auto* kernel=online_kernel.load())kernel->BroadcastNotification(0x02000002,0);
  };
  options.achievements=[] {
    std::map<uint32_t,bool> progress;
    if(auto* kernel=online_kernel.load())for(const auto& a:kernel->loaded_achievements())progress[a.id]=kernel->IsAchievementUnlocked(a.id);
    return progress;
  };
  options.restore_achievement=[](uint32_t id) {
    if(auto* kernel=online_kernel.load())if(kernel->achievements().FindAchievement(id))kernel->UnlockAchievement(id);
  };
  options.show_achievements=[]{show_achievements=true;};
  options.log=[](const std::string& message){REXLOG_INFO("RedBaron online: {}",message);};
  try {online=std::make_shared<eos::OnlineService>(options);}
  catch(const std::exception& error) {
    REXLOG_ERROR("RedBaron online unavailable: {}",error.what());
#ifdef _WIN32
    const std::string message=std::string("Online sign-in could not start. Offline play remains available.\n\n")+error.what();
    MessageBoxA(nullptr,message.c_str(),"RedBaron_Recomp - Epic Online Services",MB_OK|MB_ICONINFORMATION);
#endif
    return;
  }
  const auto id=online->network_id();
  rex::cvar::SetFlagByName("redbaron_lan","true");
  rex::cvar::SetFlagByName("redbaron_p2p_namespace",std::to_string(options.name_space));
  rex::cvar::SetFlagByName("redbaron_p2p_id",std::to_string(id));
  rex::cvar::SetFlagByName("redbaron_lan_bind","127."+std::to_string(options.name_space)+"."+std::to_string(id>>8)+"."+std::to_string(id&255));
  rex::cvar::SetFlagByName("redbaron_lan_peer","127."+std::to_string(options.name_space)+".0.0");
  rex::system::xam::LanSetOnlineProvider(online);
#else
  throw std::runtime_error("This build does not include the EOS online provider");
#endif
}
void ShutdownOnline() {
#ifdef REDBARON_EOS_ENABLED
  online_kernel=nullptr;
  if(online)online->Stop();
  rex::system::xam::LanSetOnlineProvider({});online.reset();
#endif
}
void BindOnlineRuntime(rex::Runtime& runtime) {
#ifdef REDBARON_EOS_ENABLED
  online_kernel=runtime.kernel_state();ExportAchievements(runtime);
  std::lock_guard lock(invitation_mutex);
  if(invitation)runtime.kernel_state()->BroadcastNotification(0x02000002,0);
#endif
}
std::unique_ptr<rex::ui::ImGuiDialog> CreateOnlineDialogs(rex::ui::ImGuiDrawer* drawer) {
#ifdef REDBARON_EOS_ENABLED
  return std::make_unique<OnlineDialogs>(drawer);
#else
  return nullptr;
#endif
}
bool ReadAcceptedInvitation(PPCContext& ctx,uint8_t* base) {
#ifdef REDBARON_EOS_ENABLED
  if(!online)return false;
  std::lock_guard lock(invitation_mutex);
  if(!invitation){ctx.r3.u64=1168;return true;}
  const auto user=ctx.r3.u32,address=ctx.r4.u32;
  auto* kernel=online_kernel.load();auto* memory=kernel?kernel->memory():nullptr;
  if(user>=4 || !memory || !address || uint64_t(address)+84>0x100000000ull ||
     !memory->LookupHeap(address) || !memory->LookupHeap(address+83)) {ctx.r3.u64=87;return true;}
  const auto packed=eos::PackNativeInvitation(*invitation,rex::system::xam::LanUserId(user));
  if(!packed){invitation.reset();ctx.r3.u64=87;return true;}
  std::copy(packed->begin(),packed->end(),memory->TranslateVirtual(address));
  invitation.reset();ctx.r3.u64=0;return true;
#else
  return false;
#endif
}
}
// Strong authored override of the SDK wrapper's weak entry. Keep the generated
// implementation callable for offline/LAN. The original title consumes 84 bytes.
extern "C" void sub_8239B358(PPCContext& ctx,uint8_t* base) {
  if(!redbaron::ReadAcceptedInvitation(ctx,base))__imp__sub_8239B358(ctx,base);
}
