#include "lobby_join.h"
#include "directory_mapping.h"
#include "lobby_directory.h"
namespace redbaron::eos {
bool JoinSelected(uint16_t address,const DirectoryMapping& mapping,LobbyDirectory& directory,LobbyJoin& join) {
  if(join.state()!=LobbyJoin::State::Idle)return false;
  const auto* id=mapping.LobbyAt(address);if(!id)return false;
  auto details=directory.TakeDetails(*id);if(!details)return false;
  if(join.Start(*id,details))return true;
  EOS_LobbyDetails_Release(details);return false;
}
}
