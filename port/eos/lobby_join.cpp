#include "lobby_join.h"
#include <stdexcept>
#include <utility>
namespace redbaron::eos {
LobbyJoin::LobbyJoin(EOS_HLobby lobby,EOS_ProductUserId user,LobbyJoinApi api,bool presence):lobby_(lobby),user_(user),api_(api),presence_(presence) {
  if(!lobby || !EOS_ProductUserId_IsValid(user) || !api.join || !api.leave || !api.release)
    throw std::invalid_argument("Invalid lobby join configuration");
}
LobbyJoin::~LobbyJoin(){if(details_)api_.release(details_);}
bool LobbyJoin::Start(std::string id,EOS_HLobbyDetails details) {
  if(state_!=State::Idle || id.empty() || id.size()>EOS_LOBBY_MAX_LOBBYIDOVERRIDE_LENGTH || !details)return false;
  id_=std::move(id);details_=details;error_=EOS_EResult::EOS_Success;state_=State::Joining;
  EOS_Lobby_JoinLobbyOptions options{};options.ApiVersion=EOS_LOBBY_JOINLOBBY_API_LATEST;
  options.LobbyDetailsHandle=details_;options.LocalUserId=user_;
  options.bPresenceEnabled=presence_?EOS_TRUE:EOS_FALSE;
  api_.join(lobby_,&options,this,Joined);return true;
}
void EOS_CALL LobbyJoin::Joined(const EOS_Lobby_JoinLobbyCallbackInfo* data) {
  auto& self=*static_cast<LobbyJoin*>(data->ClientData);
  if(self.details_){self.api_.release(self.details_);self.details_=nullptr;}
  self.error_=data->ResultCode;
  if(data->ResultCode==EOS_EResult::EOS_Success && data->LobbyId && *data->LobbyId) {
    if(self.id_!=data->LobbyId) {self.id_=data->LobbyId;self.error_=EOS_EResult::EOS_UnexpectedError;}
    self.state_=State::Joined; // The actual lobby must still be left on mismatch.
  } else {
    if(data->ResultCode==EOS_EResult::EOS_Success)self.error_=EOS_EResult::EOS_UnexpectedError;
    self.state_=State::Failed;
  }
}
bool LobbyJoin::Leave() {
  if(state_!=State::Joined)return false;
  state_=State::Leaving;
  EOS_Lobby_LeaveLobbyOptions options{};options.ApiVersion=EOS_LOBBY_LEAVELOBBY_API_LATEST;
  options.LocalUserId=user_;options.LobbyId=id_.c_str();api_.leave(lobby_,&options,this,Left);return true;
}
bool LobbyJoin::Reset() {
  if(state_!=State::Failed)return false;
  state_=State::Idle;error_=EOS_EResult::EOS_Success;id_.clear();return true;
}
void EOS_CALL LobbyJoin::Left(const EOS_Lobby_LeaveLobbyCallbackInfo* data) {
  auto& self=*static_cast<LobbyJoin*>(data->ClientData);self.error_=data->ResultCode;
  if(self.error_==EOS_EResult::EOS_Success || self.error_==EOS_EResult::EOS_NotFound){self.state_=State::Idle;self.id_.clear();}
  else self.state_=State::Joined;
}
}
