#include "lobby_publisher.h"
#include "lobby_record.h"
#include "session_record.h"
#include "../network/session_record.h"
#include <stdexcept>

namespace redbaron::eos {
LobbyPublisher::LobbyPublisher(EOS_HLobby lobby,EOS_ProductUserId owner,std::string id)
    : lobby_(lobby),owner_(owner),lobby_id_(std::move(id)) {
  if(!lobby || !EOS_ProductUserId_IsValid(owner) || lobby_id_.empty())
    throw std::invalid_argument("Invalid lobby publisher configuration");
}
bool LobbyPublisher::Offer(std::string_view record) {
  if(!record.empty() && !redbaron::net::ValidSessionRecord(record))return false;
  desired_=record;return true;
}
void LobbyPublisher::Poll() {
  if(in_flight_ || error_!=EOS_EResult::EOS_Success || desired_==published_)return;
  submitted_=desired_;
  submitted_count_=submitted_.empty() ? 0 : EncodeSessionRecord(submitted_).size();
  in_flight_=true;
  const auto result=submitted_.empty()
    ? ClearLobbyRecord(lobby_,owner_,lobby_id_.c_str(),published_count_,this,Updated)
    : PublishLobbyRecord(lobby_,owner_,lobby_id_.c_str(),submitted_,published_count_,this,Updated);
  if(result!=EOS_EResult::EOS_Success) {in_flight_=false;error_=result;}
}
void EOS_CALL LobbyPublisher::Updated(const EOS_Lobby_UpdateLobbyCallbackInfo* data) {
  auto& self=*static_cast<LobbyPublisher*>(data->ClientData);
  self.in_flight_=false;self.error_=data->ResultCode;
  if(self.error_==EOS_EResult::EOS_Success) {
    self.published_=self.submitted_;self.published_count_=self.submitted_count_;
  }
}
}
