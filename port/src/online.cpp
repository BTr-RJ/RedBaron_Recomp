#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#ifdef REDBARON_EOS_ENABLED
#include "../eos/online_service.h"
#endif

REXCVAR_DEFINE_STRING(redbaron_eos_config,"","Network","Local EOS configuration file; empty keeps existing offline/LAN behavior")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace redbaron {
#ifdef REDBARON_EOS_ENABLED
namespace {std::shared_ptr<eos::OnlineService> online;}
#endif
void ConfigureOnline() {
  const auto path=REXCVAR_GET(redbaron_eos_config);
  if(path.empty())return;
#ifdef REDBARON_EOS_ENABLED
  eos::OnlineOptions options;options.config=std::filesystem::absolute(path);
  options.player_name=rex::cvar::GetFlagByName("redbaron_player_name");
  options.log=[](const std::string& message){REXLOG_INFO("RedBaron online: {}",message);};
  online=std::make_shared<eos::OnlineService>(options);
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
  if(online)online->Stop();
  rex::system::xam::LanSetOnlineProvider({});online.reset();
#endif
}
}
