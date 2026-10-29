#include "selection_join.h"
#include <stdexcept>
#include <utility>
namespace redbaron::eos {
SelectionJoin::SelectionJoin(redbaron::net::SelectionGateway& gateway,redbaron::net::SocketBridge& bridge,
  redbaron::net::Wire& wire,DirectoryMapping& mapping,LobbyDirectory& directory,LobbyJoin& join,PreparePeer prepare)
  :mapping_(mapping),directory_(directory),join_(join),prepare_(std::move(prepare)),
   admission_(gateway,bridge,wire,[this](uint16_t room){
     if(cancel_pending_ || !prepare_)return false;
     return JoinSelected(room,mapping_,directory_,join_);
   }) {
  if(!prepare_)throw std::invalid_argument("EOS peer preparation callback is required");
}
void SelectionJoin::Cancel() {
  admission_.Reset();prepared_=false;
  if(join_.state()==LobbyJoin::State::Joining)cancel_pending_=true;
  else if(join_.state()==LobbyJoin::State::Joined) {cancel_pending_=true;join_.Leave();}
  else if(join_.state()==LobbyJoin::State::Leaving)cancel_pending_=true;
  else {if(join_.state()==LobbyJoin::State::Failed)join_.Reset();cancel_pending_=false;}
}
void SelectionJoin::Poll() {
  if(cancel_pending_) {
    if(join_.state()==LobbyJoin::State::Joined)join_.Leave();
    else if(join_.state()==LobbyJoin::State::Failed){join_.Reset();cancel_pending_=false;}
    else if(join_.state()==LobbyJoin::State::Idle)cancel_pending_=false;
    return;
  }
  admission_.Poll();
  if(admission_.state()==redbaron::net::SelectionAdmission::State::Failed) {
    if(join_.state()==LobbyJoin::State::Joining)cancel_pending_=true;
    else if(join_.state()==LobbyJoin::State::Joined){cancel_pending_=true;join_.Leave();}
    return;
  }
  if(admission_.state()!=redbaron::net::SelectionAdmission::State::Waiting)return;
  if(join_.state()==LobbyJoin::State::Failed) {admission_.Complete(false);return;}
  if(join_.state()==LobbyJoin::State::Joined && !prepared_) {
    if(join_.error()!=EOS_EResult::EOS_Success || !prepare_(join_.lobby_id())) {
      admission_.Complete(false);cancel_pending_=true;join_.Leave();return;
    }
    prepared_=true;admission_.Complete(true);
  }
}
}
