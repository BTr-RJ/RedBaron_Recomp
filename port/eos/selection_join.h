#pragma once
#include "directory_mapping.h"
#include "lobby_directory.h"
#include "lobby_join.h"
#include "../network/selection_admission.h"
#include <functional>

namespace redbaron::eos {
// Binds a local game selection to EOS admission. The caller supplies peer
// initialization after join; it must authenticate lobby membership and make
// the host available on Wire slot 0 before queued sockets can be adopted.
class SelectionJoin {
 public:
  using PreparePeer=std::function<bool(const std::string&)>;
  SelectionJoin(redbaron::net::SelectionGateway&,redbaron::net::SocketBridge&,redbaron::net::Wire&,
                DirectoryMapping&,LobbyDirectory&,LobbyJoin&,PreparePeer);
  void Poll();
  void Cancel();
  redbaron::net::SelectionAdmission::State state() const{return admission_.state();}
 private:
  DirectoryMapping& mapping_;LobbyDirectory& directory_;LobbyJoin& join_;
  PreparePeer prepare_;redbaron::net::SelectionAdmission admission_;
  bool cancel_pending_=false,prepared_=false;
};
}
